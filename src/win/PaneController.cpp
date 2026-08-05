#include "win/PaneController.h"

#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace sf::win {

PaneController::~PaneController() {
  for (Pane &pane : panes_) {
    if (pane.worker)
      pane.worker->request_stop();
    for (Pane::RetiredWorker &retired : pane.retiredWorkers)
      retired.thread->request_stop();
  }
}

std::wstring PaneController::AddressDisplayText(const Pane &pane) {
  if (pane.driveView)
    return L"PC";
  if (pane.searchMode)
    return L"検索: " + pane.searchRoot;
  return pane.path;
}

void PaneController::AttachControls(int pane, HWND address, HWND list) {
  if (!IsValidPane(pane))
    return;
  panes_[pane].address = address;
  panes_[pane].list = list;
}

HWND PaneController::AddressHandle(int pane) const {
  return IsValidPane(pane) ? panes_[pane].address : nullptr;
}

HWND PaneController::ListHandle(int pane) const {
  return IsValidPane(pane) ? panes_[pane].list : nullptr;
}

int PaneController::PaneIndexFromControl(HWND control, int fallback) const {
  if (control == panes_[0].list || control == panes_[0].address)
    return 0;
  if (control == panes_[1].list || control == panes_[1].address)
    return 1;
  return fallback;
}

void PaneController::ApplySettings(int pane, const PaneSettings &settings) {
  if (!IsValidPane(pane))
    return;
  Pane &target = panes_[pane];
  target.showHidden = settings.showHidden;
  target.sortColumn = settings.sortColumn == 1 ? 0 : settings.sortColumn;
  target.sortAscending = settings.sortAscending;
}

void PaneController::WriteSettings(int pane, PaneSettings &settings) const {
  if (!IsValidPane(pane))
    return;
  const Pane &source = panes_[pane];
  settings.path = WideToUtf8(source.path);
  settings.sortColumn = source.sortColumn;
  settings.sortAscending = source.sortAscending;
  settings.showHidden = source.showHidden;
}

void PaneController::Navigate(HWND window, int paneIndex,
                              const std::wstring &inputPath, bool addHistory,
                              const NotifyFn &notify,
                              const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  if (inputPath.empty()) {
    ShowDrives(window, paneIndex, addHistory, notify, searchState);
    return;
  }
  std::error_code error;
  std::filesystem::path path(inputPath);
  if (std::filesystem::is_regular_file(path, error)) {
    if (!OpenPath(window, path.wstring()))
      notify(L"ファイルを開けません", true);
    return;
  }
  if (!IsDirectory(path.wstring())) {
    notify(L"フォルダーを開けません: " + path.wstring(), true);
    return;
  }
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (!error)
    path = absolute.lexically_normal();

  Pane &pane = panes_[paneIndex];
  RetireWorker(pane);
  pane.pendingSelectionPath.clear();
  pane.path = path.wstring();
  pane.searchMode = false;
  pane.searchRoot.clear();
  pane.searchQuery.clear();
  pane.busy = true;
  pane.driveView = false;
  pane.items.clear();
  ++pane.generation;
  ListView_SetItemCountEx(pane.list, 0, LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, AddressDisplayText(pane).c_str());
  searchState(paneIndex, pane.searchMode, pane.busy);
  if (addHistory) {
    if (!pane.history.empty() && pane.historyIndex + 1 < pane.history.size()) {
      pane.history.erase(pane.history.begin() +
                             static_cast<std::ptrdiff_t>(pane.historyIndex + 1),
                         pane.history.end());
    }
    if (pane.history.empty() || pane.history.back() != pane.path) {
      pane.history.push_back(pane.path);
      pane.historyIndex = pane.history.size() - 1;
    }
  }
  const std::uint64_t generation = pane.generation;
  const bool showHidden = pane.showHidden;
  pane.worker = std::make_unique<std::jthread>(
      [target = window, paneIndex, generation, path = pane.path,
       showHidden](std::stop_token token) {
        EnumerateDirectory(target, paneIndex, generation, path, showHidden,
                           token);
      });
  notify(L"読み込み中: " + pane.path, false);
}

void PaneController::ShowDrives(HWND, int paneIndex, bool addHistory,
                                const NotifyFn &notify,
                                const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  RetireWorker(pane);
  pane.pendingSelectionPath.clear();
  pane.path.clear();
  pane.searchRoot.clear();
  pane.searchMode = false;
  pane.searchQuery.clear();
  pane.busy = false;
  pane.driveView = true;
  pane.items.clear();
  ++pane.generation;
  std::array<wchar_t, 512> drives{};
  const DWORD length =
      GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data());
  for (const wchar_t *drive = drives.data(); length != 0 && *drive != L'\0';
       drive += wcslen(drive) + 1) {
    FileItem item;
    item.path = drive;
    item.name = drive;
    item.attributes = FILE_ATTRIBUTE_DIRECTORY;
    SHFILEINFOW info{};
    SHGetFileInfoW(drive, 0, &info, sizeof(info),
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    item.iconIndex = info.iIcon;
    pane.items.push_back(std::move(item));
  }
  ListView_SetItemCountEx(pane.list, static_cast<int>(pane.items.size()),
                          LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, AddressDisplayText(pane).c_str());
  searchState(paneIndex, pane.searchMode, pane.busy);
  if (addHistory) {
    if (!pane.history.empty() && pane.historyIndex + 1 < pane.history.size()) {
      pane.history.erase(pane.history.begin() +
                             static_cast<std::ptrdiff_t>(pane.historyIndex + 1),
                         pane.history.end());
    }
    if (!pane.history.empty() && pane.history.back().empty()) {
      pane.historyIndex = pane.history.size() - 1;
      notify(std::format(L"{} ドライブ", pane.items.size()), false);
      return;
    }
    pane.history.push_back({});
    pane.historyIndex = pane.history.size() - 1;
  }
  notify(std::format(L"{} ドライブ", pane.items.size()), false);
}

void PaneController::NavigateHistory(HWND window, int paneIndex, int delta,
                                     const NotifyFn &notify,
                                     const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  if (pane.history.empty())
    return;
  const auto next = static_cast<std::ptrdiff_t>(pane.historyIndex) + delta;
  if (next < 0 || next >= static_cast<std::ptrdiff_t>(pane.history.size()))
    return;
  pane.historyIndex = static_cast<std::size_t>(next);
  const std::wstring path = pane.history[pane.historyIndex];
  if (path.empty())
    ShowDrives(window, paneIndex, false, notify, searchState);
  else
    Navigate(window, paneIndex, path, false, notify, searchState);
}

void PaneController::NavigateUp(HWND window, int paneIndex,
                                const NotifyFn &notify,
                                const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  if (pane.driveView || pane.path.empty())
    return;
  const std::wstring childPath = pane.path;
  const std::filesystem::path parent =
      std::filesystem::path(pane.path).parent_path();
  if (parent == pane.path || parent.empty()) {
    ShowDrives(window, paneIndex, true, notify, searchState);
    pane.pendingSelectionPath = childPath;
    RestorePendingSelection(paneIndex);
  } else {
    Navigate(window, paneIndex, parent.wstring(), true, notify, searchState);
    pane.pendingSelectionPath = childPath;
  }
}

void PaneController::RefreshPane(HWND window, int paneIndex,
                                 const NotifyFn &notify,
                                 const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  const Pane &pane = panes_[paneIndex];
  if (pane.driveView)
    ShowDrives(window, paneIndex, false, notify, searchState);
  else if (pane.searchMode)
    StartSearch(window, paneIndex, pane.searchQuery, notify, searchState);
  else
    Navigate(window, paneIndex, pane.path, false, notify, searchState);
}

void PaneController::StartSearch(HWND window, int paneIndex,
                                 const std::wstring &query,
                                 const NotifyFn &notify,
                                 const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  if (query.empty()) {
    if (pane.searchMode)
      Navigate(window, paneIndex, pane.searchRoot, false, notify, searchState);
    return;
  }
  if (pane.driveView || pane.path.empty()) {
    notify(L"検索するフォルダーを開いてください", true);
    return;
  }
  RetireWorker(pane);
  pane.pendingSelectionPath.clear();
  pane.searchRoot = pane.searchMode ? pane.searchRoot : pane.path;
  pane.searchQuery = query;
  pane.searchMode = true;
  pane.busy = true;
  pane.items.clear();
  ++pane.generation;
  ListView_SetItemCountEx(pane.list, 0, LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, AddressDisplayText(pane).c_str());
  const std::uint64_t generation = pane.generation;
  const bool showHidden = pane.showHidden;
  pane.worker = std::make_unique<std::jthread>(
      [target = window, paneIndex, generation, root = pane.searchRoot, query,
       showHidden](std::stop_token token) {
        SearchDirectory(target, paneIndex, generation, root, query, showHidden,
                        token);
      });
  searchState(paneIndex, pane.searchMode, pane.busy);
  notify(L"検索中: " + query, false);
}

void PaneController::CancelSearch(int paneIndex, const NotifyFn &notify,
                                  const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  if (!pane.searchMode || !pane.busy)
    return;
  RetireWorker(pane);
  ++pane.generation;
  pane.busy = false;
  searchState(paneIndex, pane.searchMode, pane.busy);
  notify(L"検索を中止しました", false);
}

void PaneController::ToggleShowHidden(HWND window, int paneIndex,
                                      const NotifyFn &notify,
                                      const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  panes_[paneIndex].showHidden = !panes_[paneIndex].showHidden;
  RefreshPane(window, paneIndex, notify, searchState);
}

void PaneController::HandleEnumerationBatch(LPARAM lParam) {
  std::unique_ptr<EnumerationBatch> batch(
      reinterpret_cast<EnumerationBatch *>(lParam));
  if (!IsValidPane(batch->pane) ||
      batch->generation != panes_[batch->pane].generation) {
    return;
  }
  Pane &pane = panes_[batch->pane];
  if (batch->replace)
    pane.items.clear();
  pane.items.insert(pane.items.end(),
                    std::make_move_iterator(batch->items.begin()),
                    std::make_move_iterator(batch->items.end()));
  ListView_SetItemCountEx(pane.list, static_cast<int>(pane.items.size()),
                          LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
  InvalidateRect(pane.list, nullptr, FALSE);
}

void PaneController::HandleEnumerationDone(LPARAM lParam,
                                           const NotifyFn &notify,
                                           const SearchStateFn &searchState) {
  std::unique_ptr<EnumerationDone> done(
      reinterpret_cast<EnumerationDone *>(lParam));
  if (IsValidPane(done->pane))
    FinishWorker(panes_[done->pane], done->generation);
  if (!IsValidPane(done->pane) ||
      done->generation != panes_[done->pane].generation) {
    return;
  }
  Pane &pane = panes_[done->pane];
  pane.busy = false;
  searchState(done->pane, pane.searchMode, pane.busy);
  SortPane(done->pane);
  RestorePendingSelection(done->pane);
  if (done->error == ERROR_SUCCESS) {
    notify(std::format(L"{} 項目", done->itemCount), false);
  } else if (done->error != ERROR_CANCELLED) {
    notify(L"読み込みに失敗しました: " + WindowsErrorMessage(done->error),
           true);
  }
}

void PaneController::OpenSelected(HWND window, int paneIndex,
                                  const NotifyFn &notify,
                                  const SearchStateFn &searchState) {
  if (!IsValidPane(paneIndex))
    return;
  Pane &pane = panes_[paneIndex];
  const int index = ListView_GetNextItem(pane.list, -1, LVNI_SELECTED);
  if (index < 0 || index >= static_cast<int>(pane.items.size()))
    return;
  const FileItem &item = pane.items[index];
  if (item.IsDirectory())
    Navigate(window, paneIndex, item.path, true, notify, searchState);
  else if (!OpenPath(window, item.path))
    notify(L"ファイルを開けません", true);
}

void PaneController::BeginRename(int paneIndex) const {
  if (!IsValidPane(paneIndex))
    return;
  const Pane &pane = panes_[paneIndex];
  const int index = ListView_GetNextItem(pane.list, -1, LVNI_SELECTED);
  if (index >= 0)
    ListView_EditLabel(pane.list, index);
}

void PaneController::RestoreAddressText(int paneIndex) const {
  if (!IsValidPane(paneIndex))
    return;
  const Pane &pane = panes_[paneIndex];
  SetWindowTextW(pane.address, AddressDisplayText(pane).c_str());
}

void PaneController::SelectAll(int paneIndex) const {
  if (!IsValidPane(paneIndex))
    return;
  ListView_SetItemState(panes_[paneIndex].list, -1, LVIS_SELECTED,
                        LVIS_SELECTED);
}

void PaneController::SelectContextItem(int paneIndex, int item) const {
  if (!IsValidPane(paneIndex))
    return;
  const HWND list = panes_[paneIndex].list;
  if (item >= 0) {
    const UINT state = ListView_GetItemState(list, item, LVIS_SELECTED);
    if ((state & LVIS_SELECTED) == 0)
      ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, item, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list, item);
  } else {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list, -1);
  }
}

void PaneController::HandleColumnClick(int paneIndex, int subItem) {
  if (!IsValidPane(paneIndex) || subItem < 0 || subItem > 2)
    return;
  constexpr std::array<int, 3> sortColumns{0, 2, 3};
  Pane &pane = panes_[paneIndex];
  const int sortColumn = sortColumns[subItem];
  if (pane.sortColumn == sortColumn)
    pane.sortAscending = !pane.sortAscending;
  else {
    pane.sortColumn = sortColumn;
    pane.sortAscending = true;
  }
  SortPane(paneIndex);
}

int PaneController::FindItem(int paneIndex, const NMLVFINDITEMW &find) const {
  if (!IsValidPane(paneIndex) || find.lvfi.psz == nullptr ||
      (find.lvfi.flags & LVFI_NEARESTXY) != 0) {
    return -1;
  }
  const Pane &pane = panes_[paneIndex];
  const int itemCount = static_cast<int>(pane.items.size());
  if (itemCount == 0)
    return -1;

  const std::wstring_view query(find.lvfi.psz);
  if (query.empty())
    return -1;
  const bool partial =
      (find.lvfi.flags & (LVFI_PARTIAL | LVFI_SUBSTRING)) != 0;
  const auto matches = [&query, partial](const FileItem &item) {
    if ((!partial && item.name.size() != query.size()) ||
        (partial && item.name.size() < query.size())) {
      return false;
    }
    return CompareStringOrdinal(
               item.name.data(), static_cast<int>(partial ? query.size()
                                                          : item.name.size()),
               query.data(), static_cast<int>(query.size()), TRUE) ==
           CSTR_EQUAL;
  };
  const auto findInRange = [&pane, &matches](int begin, int end) {
    for (int index = begin; index < end; ++index) {
      if (matches(pane.items[index]))
        return index;
    }
    return -1;
  };

  const int start = std::clamp(find.iStart, 0, itemCount);
  const int found = findInRange(start, itemCount);
  if (found >= 0 || (find.lvfi.flags & LVFI_WRAP) == 0 || start == 0)
    return found;
  return findInRange(0, start);
}

void PaneController::PopulateDisplayInfo(int paneIndex,
                                         NMLVDISPINFOW &display) const {
  if (!IsValidPane(paneIndex))
    return;
  const int itemIndex = display.item.iItem;
  const Pane &pane = panes_[paneIndex];
  if (itemIndex < 0 || itemIndex >= static_cast<int>(pane.items.size()))
    return;
  const FileItem &item = pane.items[itemIndex];
  if ((display.item.mask & LVIF_IMAGE) != 0)
    display.item.iImage = item.iconIndex;
  if ((display.item.mask & LVIF_TEXT) == 0)
    return;
  thread_local std::wstring text;
  switch (display.item.iSubItem) {
  case 0:
    text = item.name;
    break;
  case 1:
    text = item.IsDirectory() ? L"" : FormatFileSize(item.size);
    break;
  case 2:
    text = FormatFileTime(item.modified);
    break;
  default:
    text.clear();
    break;
  }
  display.item.pszText = text.data();
}

std::vector<std::wstring> PaneController::SelectedPaths(int paneIndex) const {
  std::vector<std::wstring> paths;
  if (!IsValidPane(paneIndex))
    return paths;
  const Pane &pane = panes_[paneIndex];
  int index = -1;
  while ((index = ListView_GetNextItem(pane.list, index, LVNI_SELECTED)) >= 0) {
    if (index < static_cast<int>(pane.items.size()))
      paths.push_back(pane.items[index].path);
  }
  return paths;
}

std::wstring PaneController::EffectivePath(int pane) const {
  if (!IsValidPane(pane))
    return {};
  return panes_[pane].searchMode ? panes_[pane].searchRoot : panes_[pane].path;
}

std::wstring PaneController::SearchQuery(int pane) const {
  return IsValidPane(pane) ? panes_[pane].searchQuery : std::wstring{};
}

std::wstring PaneController::ItemPath(int pane, int item) const {
  if (!IsValidPane(pane) || item < 0 ||
      item >= static_cast<int>(panes_[pane].items.size())) {
    return {};
  }
  return panes_[pane].items[item].path;
}

void PaneController::SetCutPaths(std::vector<std::wstring> paths) {
  cutPaths_ = std::move(paths);
  for (const Pane &pane : panes_)
    InvalidateRect(pane.list, nullptr, FALSE);
}

void PaneController::ClearCutPaths() {
  if (cutPaths_.empty())
    return;
  cutPaths_.clear();
  for (const Pane &pane : panes_)
    InvalidateRect(pane.list, nullptr, FALSE);
}

bool PaneController::HasCutPaths() const noexcept { return !cutPaths_.empty(); }

bool PaneController::IsItemCut(int pane, int item) const {
  if (cutPaths_.empty())
    return false;
  const std::wstring path = ItemPath(pane, item);
  if (path.empty())
    return false;
  return std::find(cutPaths_.begin(), cutPaths_.end(), path) !=
         cutPaths_.end();
}

bool PaneController::IsSearchMode(int pane) const {
  return IsValidPane(pane) && panes_[pane].searchMode;
}

bool PaneController::IsBusy(int pane) const {
  return IsValidPane(pane) && panes_[pane].busy;
}

bool PaneController::IsDriveView(int pane) const {
  return IsValidPane(pane) && panes_[pane].driveView;
}

bool PaneController::HasPath(int pane) const {
  return IsValidPane(pane) && !panes_[pane].path.empty();
}

bool PaneController::IsValidPane(int pane) noexcept {
  return pane >= 0 && pane < 2;
}

void PaneController::RestorePendingSelection(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  if (pane.pendingSelectionPath.empty())
    return;

  const auto found =
      std::find_if(pane.items.begin(), pane.items.end(),
                   [&pane](const FileItem &item) {
                     return _wcsicmp(item.path.c_str(),
                                     pane.pendingSelectionPath.c_str()) == 0;
                   });
  pane.pendingSelectionPath.clear();
  if (found == pane.items.end())
    return;

  const int index =
      static_cast<int>(std::distance(pane.items.begin(), found));
  ListView_SetItemState(pane.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_SetItemState(pane.list, index, LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
  ListView_SetSelectionMark(pane.list, index);
  ListView_EnsureVisible(pane.list, index, FALSE);
}

void PaneController::SortPane(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  std::unordered_set<std::wstring> selectedPaths;
  int selectedIndex = -1;
  while ((selectedIndex = ListView_GetNextItem(pane.list, selectedIndex,
                                               LVNI_SELECTED)) >= 0) {
    if (selectedIndex < static_cast<int>(pane.items.size()))
      selectedPaths.insert(pane.items[selectedIndex].path);
  }
  std::wstring focusedPath;
  const int focusedIndex = ListView_GetNextItem(pane.list, -1, LVNI_FOCUSED);
  if (focusedIndex >= 0 && focusedIndex < static_cast<int>(pane.items.size())) {
    focusedPath = pane.items[focusedIndex].path;
  }

  const int column = pane.sortColumn;
  const bool ascending = pane.sortAscending;
  std::stable_sort(
      pane.items.begin(), pane.items.end(),
      [column, ascending](const FileItem &left, const FileItem &right) {
        int comparison = 0;
        if (left.IsDirectory() != right.IsDirectory()) {
          comparison = left.IsDirectory() ? -1 : 1;
        } else if (column == 2) {
          comparison = left.size < right.size   ? -1
                       : left.size > right.size ? 1
                                                : 0;
        } else if (column == 3) {
          comparison = CompareFileTime(&left.modified, &right.modified);
        } else {
          comparison = _wcsicmp(left.name.c_str(), right.name.c_str());
        }
        return ascending ? comparison < 0 : comparison > 0;
      });
  ListView_SetItemState(pane.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  for (int index = 0; index < static_cast<int>(pane.items.size()); ++index) {
    UINT state = 0;
    if (selectedPaths.contains(pane.items[index].path))
      state |= LVIS_SELECTED;
    if (!focusedPath.empty() && pane.items[index].path == focusedPath)
      state |= LVIS_FOCUSED;
    if (state != 0) {
      ListView_SetItemState(pane.list, index, state,
                            LVIS_SELECTED | LVIS_FOCUSED);
    }
  }
  if (!pane.items.empty()) {
    ListView_RedrawItems(pane.list, 0, static_cast<int>(pane.items.size()) - 1);
  }
}

void PaneController::RetireWorker(Pane &pane) {
  if (!pane.worker)
    return;
  pane.worker->request_stop();
  pane.retiredWorkers.push_back(
      Pane::RetiredWorker{pane.generation, std::move(pane.worker)});
}

void PaneController::FinishWorker(Pane &pane, std::uint64_t generation) {
  if (generation == pane.generation && pane.worker) {
    pane.worker->join();
    pane.worker.reset();
    return;
  }
  const auto found =
      std::find_if(pane.retiredWorkers.begin(), pane.retiredWorkers.end(),
                   [generation](const Pane::RetiredWorker &worker) {
                     return worker.generation == generation;
                   });
  if (found != pane.retiredWorkers.end()) {
    found->thread->join();
    pane.retiredWorkers.erase(found);
  }
}

} // namespace sf::win
