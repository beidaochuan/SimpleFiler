#pragma once

#include "win/AsyncTaskTracker.h"

#include <windows.h>

#include <string>
#include <thread>
#include <vector>

namespace sf::win {

struct ZipResult final {
  OperationId operationId = 0;
  bool success = false;
  std::wstring message;
};

[[nodiscard]] std::jthread
CreateZipAsync(HWND notifyWindow, OperationId operationId,
               std::vector<std::wstring> sources, std::wstring outputPath);
[[nodiscard]] std::jthread
ExtractZipAsync(HWND notifyWindow, OperationId operationId,
                std::wstring archivePath, std::wstring destination);

} // namespace sf::win
