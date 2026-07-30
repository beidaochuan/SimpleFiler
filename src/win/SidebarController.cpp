#include "win/SidebarController.h"

#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <iterator>

namespace sf::win {
namespace {

std::wstring LeafName(const std::wstring &path) {
  std::filesystem::path value(path);
  if (!value.filename().empty())
    return value.filename().wstring();
  return path;
}

std::vector<std::string> SplitKeywords(const std::wstring &text) {
  std::vector<std::string> result;
  std::wstring current;
  const auto flush = [&result, &current] {
    if (!current.empty()) {
      result.push_back(WideToUtf8(current));
      current.clear();
    }
  };
  for (const wchar_t character : text) {
    if (std::iswspace(static_cast<wint_t>(character)) != 0 ||
        character == L',' || character == L';' || character == L'、') {
      flush();
    } else {
      current.push_back(character);
    }
  }
  flush();
  return result;
}

std::wstring JoinKeywords(const std::vector<std::string> &keywords) {
  std::wstring result;
  for (const std::string &keyword : keywords) {
    if (!result.empty())
      result.push_back(L' ');
    result += Utf8ToWide(keyword);
  }
  return result;
}

} // namespace

void SidebarController::AddBookmarkForPath(
    HWND sidebar, AppSettings &settings, const std::wstring &path,
    const PromptTextFn &promptText, const SaveSettingsFn &saveSettings) {
  if (path.empty())
    return;
  const std::wstring name =
      promptText(L"ブックマーク追加", L"表示名", LeafName(path));
  if (name.empty())
    return;
  const std::wstring alias =
      promptText(L"ブックマーク追加", L"ffのエイリアス（省略可）", {});
  const std::wstring keywords = promptText(
      L"ブックマーク追加", L"検索キーワード（空白区切り、省略可）", {});
  settings.bookmarks.push_back(
      {MakeStableId(), WideToUtf8(name), WideToUtf8(path), WideToUtf8(alias),
       SplitKeywords(keywords)});
  RebuildSidebar(sidebar, settings);
  saveSettings();
}

void SidebarController::AddLinkedFolder(
    HWND window, HWND sidebar, AppSettings &settings,
    const PromptTextFn &promptText, const SaveSettingsFn &saveSettings) {
  const std::wstring path = PickFolder(window, L"フォルダーリンクを登録");
  if (path.empty())
    return;
  AddBookmarkForPath(sidebar, settings, path, promptText, saveSettings);
}

void SidebarController::AddLink(HWND window, HWND sidebar,
                                AppSettings &settings, bool application,
                                const PromptTextFn &promptText,
                                const SaveSettingsFn &saveSettings) {
  std::array<wchar_t, 32768> file{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window;
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.lpstrTitle =
      application ? L"アプリリンクを登録" : L"ファイルリンクを登録";
  dialog.lpstrFilter =
      application ? L"アプリケーション (*.exe)\0*.exe\0すべてのファイル\0*.*\0"
                  : L"すべてのファイル\0*.*\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetOpenFileNameW(&dialog))
    return;
  const std::wstring name =
      promptText(L"リンク追加", L"表示名", LeafName(file.data()));
  if (name.empty())
    return;
  std::wstring arguments;
  std::wstring workingDirectory;
  if (application) {
    arguments = promptText(L"アプリリンク", L"引数（省略可）", {});
    workingDirectory =
        promptText(L"アプリリンク", L"作業フォルダー（省略可）",
                   std::filesystem::path(file.data()).parent_path().wstring());
  }
  const std::wstring alias = promptText(
      L"リンク追加",
      application ? L"aaのエイリアス（省略可）"
                  : L"エイリアス（省略可）",
      {});
  const std::wstring keywords = promptText(
      L"リンク追加", L"検索キーワード（空白区切り、省略可）", {});
  const bool runAsAdministrator =
      application &&
      MessageBoxW(window, L"通常の起動を管理者権限にしますか？",
                  L"アプリリンク", MB_ICONQUESTION | MB_YESNO |
                                        MB_DEFBUTTON2) == IDYES;
  settings.links.push_back(
      {MakeStableId(), application ? LinkType::Application : LinkType::File,
       WideToUtf8(name), WideToUtf8(MakeAppPath(file.data())),
       WideToUtf8(arguments), WideToUtf8(MakeAppPath(workingDirectory)),
       WideToUtf8(alias), SplitKeywords(keywords), runAsAdministrator});
  RebuildSidebar(sidebar, settings);
  saveSettings();
}

void SidebarController::RebuildSidebar(HWND sidebar,
                                       const AppSettings &settings) {
  SendMessageW(sidebar, LB_RESETCONTENT, 0, 0);
  sidebarMap_.clear();
  for (const Bookmark &bookmark : settings.bookmarks) {
    const std::wstring path = Utf8ToWide(bookmark.path);
    const std::wstring prefix = IsDirectory(path) ? L"★ " : L"⚠ ";
    SendMessageW(
        sidebar, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>((prefix + Utf8ToWide(bookmark.name)).c_str()));
    sidebarMap_.push_back({true, bookmark.id});
  }
  for (const RegisteredLink &link : settings.links) {
    const std::wstring target = ResolveAppPath(Utf8ToWide(link.target));
    const std::wstring prefix =
        GetFileAttributesW(ToExtendedPath(target).c_str()) !=
                INVALID_FILE_ATTRIBUTES
            ? L"↗ "
            : L"⚠ ";
    SendMessageW(
        sidebar, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>((prefix + Utf8ToWide(link.name)).c_str()));
    sidebarMap_.push_back({false, link.id});
  }
}

void SidebarController::ActivateSidebarItem(
    HWND window, HWND sidebar, const AppSettings &settings, bool administrator,
    const NavigateFn &navigate, const LaunchApplicationFn &launchApplication,
    const NotifyFn &notify) const {
  const int selected =
      static_cast<int>(SendMessageW(sidebar, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const EntryReference &reference = sidebarMap_[selected];
  if (reference.bookmark) {
    const auto bookmark =
        std::find_if(settings.bookmarks.begin(), settings.bookmarks.end(),
                     [&reference](const Bookmark &entry) {
                       return entry.id == reference.id;
                     });
    if (bookmark != settings.bookmarks.end())
      navigate(Utf8ToWide(bookmark->path));
    return;
  }
  const auto link =
      std::find_if(settings.links.begin(), settings.links.end(),
                   [&reference](const RegisteredLink &entry) {
                     return entry.id == reference.id;
                   });
  if (link == settings.links.end())
    return;
  if (link->type == LinkType::Application) {
    launchApplication(link->id, administrator);
    return;
  }
  const std::wstring target = ResolveAppPath(Utf8ToWide(link->target));
  if (GetFileAttributesW(ToExtendedPath(target).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    notify(L"登録先が見つかりません: " + target, true);
    return;
  }
  if (!OpenPath(window, target, Utf8ToWide(link->arguments),
                ResolveAppPath(Utf8ToWide(link->workingDirectory)))) {
    const DWORD error = GetLastError();
    if (error != ERROR_CANCELLED)
      notify(L"リンク先を開けません: " + WindowsErrorMessage(error), true);
  }
}

void SidebarController::EditSidebarItem(
    HWND window, HWND sidebar, AppSettings &settings,
    const PromptTextFn &promptText, const SaveSettingsFn &saveSettings) {
  const int selected =
      static_cast<int>(SendMessageW(sidebar, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;

  const EntryReference reference = sidebarMap_[selected];
  int restoredSelection = -1;
  if (reference.bookmark) {
    const auto found =
        std::find_if(settings.bookmarks.begin(), settings.bookmarks.end(),
                     [&reference](const Bookmark &entry) {
                       return entry.id == reference.id;
                     });
    if (found == settings.bookmarks.end())
      return;
    restoredSelection = static_cast<int>(
        std::distance(settings.bookmarks.begin(), found));
    Bookmark &bookmark = *found;
    const std::wstring name = promptText(
        L"フォルダー登録を編集", L"表示名", Utf8ToWide(bookmark.name));
    if (name.empty())
      return;
    const std::wstring path =
        promptText(L"フォルダー登録を編集", L"フォルダーパス",
                   Utf8ToWide(bookmark.path));
    if (path.empty())
      return;
    const std::wstring alias =
        promptText(L"フォルダー登録を編集", L"ffのエイリアス（省略可）",
                   Utf8ToWide(bookmark.alias));
    const std::wstring keywords = promptText(
        L"フォルダー登録を編集", L"検索キーワード（空白区切り）",
        JoinKeywords(bookmark.keywords));
    bookmark.name = WideToUtf8(name);
    bookmark.path = WideToUtf8(path);
    bookmark.alias = WideToUtf8(alias);
    bookmark.keywords = SplitKeywords(keywords);
  } else {
    const auto found =
        std::find_if(settings.links.begin(), settings.links.end(),
                     [&reference](const RegisteredLink &entry) {
                       return entry.id == reference.id;
                     });
    if (found == settings.links.end())
      return;
    restoredSelection =
        static_cast<int>(settings.bookmarks.size() +
                         static_cast<std::size_t>(
                             std::distance(settings.links.begin(), found)));
    RegisteredLink &link = *found;
    const std::wstring name = promptText(
        L"リンク登録を編集", L"表示名", Utf8ToWide(link.name));
    if (name.empty())
      return;
    const std::wstring target = promptText(
        L"リンク登録を編集", L"対象パス", Utf8ToWide(link.target));
    if (target.empty())
      return;
    std::wstring arguments = Utf8ToWide(link.arguments);
    std::wstring workingDirectory = Utf8ToWide(link.workingDirectory);
    if (link.type == LinkType::Application) {
      arguments = promptText(L"アプリ登録を編集", L"引数テンプレート",
                             arguments);
      workingDirectory =
          promptText(L"アプリ登録を編集", L"作業フォルダー（省略可）",
                     workingDirectory);
    }
    const std::wstring alias = promptText(
        L"リンク登録を編集",
        link.type == LinkType::Application ? L"aaのエイリアス（省略可）"
                                           : L"エイリアス（省略可）",
        Utf8ToWide(link.alias));
    const std::wstring keywords = promptText(
        L"リンク登録を編集", L"検索キーワード（空白区切り）",
        JoinKeywords(link.keywords));
    bool runAsAdministrator = link.runAsAdministrator;
    if (link.type == LinkType::Application) {
      runAsAdministrator =
          MessageBoxW(window, L"通常の起動を管理者権限にしますか？",
                      L"アプリ登録を編集",
                      MB_ICONQUESTION | MB_YESNO |
                          (link.runAsAdministrator ? MB_DEFBUTTON1
                                                   : MB_DEFBUTTON2)) == IDYES;
    }
    link.name = WideToUtf8(name);
    link.target = WideToUtf8(MakeAppPath(target));
    link.arguments = WideToUtf8(arguments);
    link.workingDirectory = WideToUtf8(MakeAppPath(workingDirectory));
    link.alias = WideToUtf8(alias);
    link.keywords = SplitKeywords(keywords);
    link.runAsAdministrator = runAsAdministrator;
  }
  RebuildSidebar(sidebar, settings);
  saveSettings();
  SendMessageW(sidebar, LB_SETCURSEL, restoredSelection, 0);
}

void SidebarController::RemoveSidebarItem(
    HWND sidebar, AppSettings &settings,
    const SaveSettingsFn &saveSettings) {
  const int selected =
      static_cast<int>(SendMessageW(sidebar, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const EntryReference reference = sidebarMap_[selected];
  if (reference.bookmark) {
    const auto found =
        std::find_if(settings.bookmarks.begin(), settings.bookmarks.end(),
                     [&reference](const Bookmark &entry) {
                       return entry.id == reference.id;
                     });
    if (found == settings.bookmarks.end())
      return;
    settings.bookmarks.erase(found);
  } else {
    const auto found =
        std::find_if(settings.links.begin(), settings.links.end(),
                     [&reference](const RegisteredLink &entry) {
                       return entry.id == reference.id;
                     });
    if (found == settings.links.end())
      return;
    settings.links.erase(found);
  }
  RebuildSidebar(sidebar, settings);
  saveSettings();
}

void SidebarController::MoveSidebarItem(
    HWND sidebar, AppSettings &settings, bool up,
    const SaveSettingsFn &saveSettings) {
  const int selected =
      static_cast<int>(SendMessageW(sidebar, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const EntryReference reference = sidebarMap_[selected];
  const auto indexForId = [&reference](const auto &entries) {
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [&reference](const auto &entry) { return entry.id == reference.id; });
    return found == entries.end()
               ? entries.size()
               : static_cast<std::size_t>(std::distance(entries.begin(), found));
  };
  const std::size_t index = reference.bookmark
                                ? indexForId(settings.bookmarks)
                                : indexForId(settings.links);
  const std::size_t sectionSize =
      reference.bookmark ? settings.bookmarks.size() : settings.links.size();
  if (index >= sectionSize || (up ? index == 0 : index + 1 >= sectionSize))
    return;
  const std::size_t otherIndex = up ? index - 1 : index + 1;
  if (reference.bookmark) {
    std::swap(settings.bookmarks[index], settings.bookmarks[otherIndex]);
  } else {
    std::swap(settings.links[index], settings.links[otherIndex]);
  }
  RebuildSidebar(sidebar, settings);
  saveSettings();
  const std::size_t sidebarIndex =
      reference.bookmark ? otherIndex
                         : settings.bookmarks.size() + otherIndex;
  SendMessageW(sidebar, LB_SETCURSEL, sidebarIndex, 0);
}

void SidebarController::ShowContextMenu(
    HWND window, HWND sidebar, POINT screenPoint, const AppSettings &settings,
    const SidebarMenuIds &ids) const {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ids.open, L"開く");
  const int selected =
      static_cast<int>(SendMessageW(sidebar, LB_GETCURSEL, 0, 0));
  bool canRunAsAdmin = false;
  bool canMoveUp = false;
  bool canMoveDown = false;
  if (selected >= 0 && selected < static_cast<int>(sidebarMap_.size())) {
    const EntryReference &reference = sidebarMap_[selected];
    if (reference.bookmark) {
      const auto found =
          std::find_if(settings.bookmarks.begin(), settings.bookmarks.end(),
                       [&reference](const Bookmark &entry) {
                         return entry.id == reference.id;
                       });
      if (found != settings.bookmarks.end()) {
        const std::size_t index = static_cast<std::size_t>(
            std::distance(settings.bookmarks.begin(), found));
        canMoveUp = index > 0;
        canMoveDown = index + 1 < settings.bookmarks.size();
      }
    } else {
      const auto found =
          std::find_if(settings.links.begin(), settings.links.end(),
                       [&reference](const RegisteredLink &entry) {
                         return entry.id == reference.id;
                       });
      if (found != settings.links.end()) {
        const std::size_t index = static_cast<std::size_t>(
            std::distance(settings.links.begin(), found));
        canRunAsAdmin = found->type == LinkType::Application;
        canMoveUp = index > 0;
        canMoveDown = index + 1 < settings.links.size();
      }
    }
  }
  AppendMenuW(menu, MF_STRING | (canRunAsAdmin ? 0 : MF_GRAYED),
              ids.openAsAdministrator, L"管理者として実行");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ids.edit, L"登録を編集");
  AppendMenuW(menu, MF_STRING, ids.remove, L"登録を削除");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (canMoveUp ? 0 : MF_GRAYED), ids.moveUp,
              L"上へ移動");
  AppendMenuW(menu, MF_STRING | (canMoveDown ? 0 : MF_GRAYED), ids.moveDown,
              L"下へ移動");
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0,
                 window, nullptr);
  DestroyMenu(menu);
}

} // namespace sf::win
