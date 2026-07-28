#include "win/FileEnumerator.h"

#include "win/AppMessages.h"
#include "win/WinUtils.h"

#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <string_view>

namespace sf::win {
namespace {

constexpr std::size_t kBatchSize = 256;

std::wstring JoinPath(const std::wstring &parent, const wchar_t *child) {
  if (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) {
    return parent + child;
  }
  return parent + L"\\" + child;
}

int IconIndexFor(const WIN32_FIND_DATAW &data) {
  SHFILEINFOW info{};
  const UINT flags =
      SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  if (SHGetFileInfoW(data.cFileName, data.dwFileAttributes, &info, sizeof(info),
                     flags) == 0) {
    return 0;
  }
  return info.iIcon;
}

FileItem MakeItem(const std::wstring &parent, const WIN32_FIND_DATAW &data) {
  FileItem item;
  item.name = data.cFileName;
  item.path = JoinPath(parent, data.cFileName);
  item.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32U) |
              data.nFileSizeLow;
  item.modified = data.ftLastWriteTime;
  item.attributes = data.dwFileAttributes;
  item.iconIndex = IconIndexFor(data);
  return item;
}

bool IsVisible(const WIN32_FIND_DATAW &data, bool showHidden) {
  if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
    return false;
  }
  return showHidden || (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == 0;
}

bool PostBatch(HWND target, int pane, std::uint64_t generation,
               std::vector<FileItem> &items, bool replace = false) {
  if (items.empty() && !replace)
    return true;
  auto *batch =
      new EnumerationBatch{pane, generation, replace, std::move(items)};
  items.clear();
  if (!PostMessageW(target, kMessageEnumerationBatch, 0,
                    reinterpret_cast<LPARAM>(batch))) {
    delete batch;
    return false;
  }
  return true;
}

void PostDone(HWND target, int pane, std::uint64_t generation, DWORD error,
              std::size_t count) {
  auto *result = new EnumerationDone{pane, generation, error, count};
  if (!PostMessageW(target, kMessageEnumerationDone, 0,
                    reinterpret_cast<LPARAM>(result))) {
    delete result;
  }
}

bool ContainsInsensitive(const std::wstring &value, const std::wstring &query) {
  return std::search(value.begin(), value.end(), query.begin(), query.end(),
                     [](wchar_t left, wchar_t right) {
                       return std::towlower(left) == std::towlower(right);
                     }) != value.end();
}

DWORD EnumerateForSearch(HWND target, int pane, std::uint64_t generation,
                         const std::wstring &directory,
                         const std::wstring &query, bool showHidden,
                         std::stop_token stopToken,
                         std::vector<FileItem> &batch, std::size_t &total) {
  if (stopToken.stop_requested())
    return ERROR_CANCELLED;
  const std::wstring pattern = JoinPath(ToExtendedPath(directory), L"*");
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                 FindExSearchNameMatch, nullptr,
                                 FIND_FIRST_EX_LARGE_FETCH);
  if (find == INVALID_HANDLE_VALUE &&
      GetLastError() == ERROR_INVALID_PARAMETER) {
    find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                            FindExSearchNameMatch, nullptr, 0);
  }
  if (find == INVALID_HANDLE_VALUE)
    return GetLastError();

  DWORD error = ERROR_SUCCESS;
  do {
    if (stopToken.stop_requested()) {
      error = ERROR_CANCELLED;
      break;
    }
    if (!IsVisible(data, showHidden))
      continue;
    FileItem item = MakeItem(directory, data);
    if (ContainsInsensitive(item.name, query)) {
      batch.push_back(item);
      ++total;
      if (batch.size() >= kBatchSize &&
          !PostBatch(target, pane, generation, batch)) {
        error = ERROR_CANCELLED;
        break;
      }
    }
    if (item.IsDirectory() &&
        (item.attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
      const DWORD childError =
          EnumerateForSearch(target, pane, generation, item.path, query,
                             showHidden, stopToken, batch, total);
      if (childError == ERROR_CANCELLED) {
        error = childError;
        break;
      }
    }
  } while (FindNextFileW(find, &data));
  if (error == ERROR_SUCCESS) {
    const DWORD last = GetLastError();
    if (last != ERROR_NO_MORE_FILES)
      error = last;
  }
  FindClose(find);
  return error;
}

} // namespace

void EnumerateDirectory(HWND target, int pane, std::uint64_t generation,
                        std::wstring path, bool showHidden,
                        std::stop_token stopToken) {
  std::vector<FileItem> batch;
  batch.reserve(kBatchSize);
  std::size_t total = 0;
  const std::wstring pattern = JoinPath(ToExtendedPath(path), L"*");
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                 FindExSearchNameMatch, nullptr,
                                 FIND_FIRST_EX_LARGE_FETCH);
  if (find == INVALID_HANDLE_VALUE &&
      GetLastError() == ERROR_INVALID_PARAMETER) {
    find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                            FindExSearchNameMatch, nullptr, 0);
  }
  if (find == INVALID_HANDLE_VALUE) {
    PostDone(target, pane, generation, GetLastError(), 0);
    return;
  }

  DWORD error = ERROR_SUCCESS;
  do {
    if (stopToken.stop_requested()) {
      error = ERROR_CANCELLED;
      break;
    }
    if (!IsVisible(data, showHidden))
      continue;
    batch.push_back(MakeItem(path, data));
    ++total;
    if (batch.size() >= kBatchSize &&
        !PostBatch(target, pane, generation, batch)) {
      error = ERROR_CANCELLED;
      break;
    }
  } while (FindNextFileW(find, &data));
  if (error == ERROR_SUCCESS) {
    const DWORD last = GetLastError();
    if (last != ERROR_NO_MORE_FILES)
      error = last;
  }
  FindClose(find);
  if (error != ERROR_CANCELLED)
    PostBatch(target, pane, generation, batch);
  PostDone(target, pane, generation, error, total);
}

void SearchDirectory(HWND target, int pane, std::uint64_t generation,
                     std::wstring root, std::wstring query, bool showHidden,
                     std::stop_token stopToken) {
  std::vector<FileItem> batch;
  batch.reserve(kBatchSize);
  std::size_t total = 0;
  const DWORD error = EnumerateForSearch(target, pane, generation, root, query,
                                         showHidden, stopToken, batch, total);
  if (error != ERROR_CANCELLED)
    PostBatch(target, pane, generation, batch);
  PostDone(target, pane, generation, error, total);
}

std::wstring FormatFileSize(std::uint64_t size) {
  constexpr const wchar_t *units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  double value = static_cast<double>(size);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < std::size(units)) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0)
    return std::format(L"{} {}", size, units[unit]);
  return std::format(L"{:.1f} {}", value, units[unit]);
}

std::wstring FormatFileTime(const FILETIME &time) {
  FILETIME local{};
  SYSTEMTIME system{};
  if (!FileTimeToLocalFileTime(&time, &local) ||
      !FileTimeToSystemTime(&local, &system)) {
    return {};
  }
  return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}", system.wYear,
                     system.wMonth, system.wDay, system.wHour, system.wMinute);
}

} // namespace sf::win
