#pragma once

#include <windows.h>

#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace sf::win {

struct FileItem final {
  std::wstring path;
  std::wstring name;
  std::uint64_t size = 0;
  FILETIME modified{};
  DWORD attributes = 0;
  int iconIndex = 0;

  [[nodiscard]] bool IsDirectory() const noexcept {
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  }
};

struct EnumerationBatch final {
  int pane = 0;
  std::uint64_t generation = 0;
  bool replace = false;
  std::vector<FileItem> items;
};

struct EnumerationDone final {
  int pane = 0;
  std::uint64_t generation = 0;
  DWORD error = ERROR_SUCCESS;
  std::size_t itemCount = 0;
};

void EnumerateDirectory(HWND target, int pane, std::uint64_t generation,
                        std::wstring path, bool showHidden,
                        std::stop_token stopToken);

void SearchDirectory(HWND target, int pane, std::uint64_t generation,
                     std::wstring root, std::wstring query, bool showHidden,
                     std::stop_token stopToken);

[[nodiscard]] std::wstring FormatFileSize(std::uint64_t size);
[[nodiscard]] std::wstring FormatFileTime(const FILETIME &time);

} // namespace sf::win
