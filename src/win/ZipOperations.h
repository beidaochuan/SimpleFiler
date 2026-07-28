#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sf::win {

struct ZipResult final {
  bool success = false;
  std::wstring message;
};

void CreateZipAsync(HWND notifyWindow, std::vector<std::wstring> sources,
                    std::wstring outputPath);
void ExtractZipAsync(HWND notifyWindow, std::wstring archivePath,
                     std::wstring destination);

} // namespace sf::win
