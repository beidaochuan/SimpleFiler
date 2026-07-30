#pragma once

#include "win/AsyncTaskTracker.h"

#include <windows.h>

#include <string>
#include <thread>
#include <vector>

namespace sf::win {

enum class FileOperationKind { Copy, Move, Delete, Rename };

struct OperationResult final {
  OperationId operationId = 0;
  HRESULT result = E_FAIL;
  bool aborted = false;
};

[[nodiscard]] bool PutFilesOnClipboard(HWND owner,
                                       const std::vector<std::wstring> &paths,
                                       bool cut);
[[nodiscard]] std::vector<std::wstring> ReadFilesFromClipboard(bool *cut);
[[nodiscard]] std::jthread PasteFilesAsync(HWND notifyWindow,
                                           OperationId operationId,
                                           const std::wstring &destination);
[[nodiscard]] std::jthread
TransferFilesAsync(HWND notifyWindow, OperationId operationId,
                   std::vector<std::wstring> paths, std::wstring destination,
                   bool move);
[[nodiscard]] std::jthread
DeleteFilesAsync(HWND notifyWindow, OperationId operationId,
                 std::vector<std::wstring> paths, bool permanent);
[[nodiscard]] std::jthread RenameFileAsync(HWND notifyWindow,
                                           OperationId operationId,
                                           std::wstring path,
                                           std::wstring newName);
[[nodiscard]] std::jthread CreateFolderAsync(HWND notifyWindow,
                                             OperationId operationId,
                                             std::wstring parent,
                                             std::wstring name);

[[nodiscard]] bool OpenPath(HWND owner, const std::wstring &path,
                            const std::wstring &arguments = {},
                            const std::wstring &workingDirectory = {},
                            bool runAsAdministrator = false);
void ShowProperties(HWND owner, const std::wstring &path);

} // namespace sf::win
