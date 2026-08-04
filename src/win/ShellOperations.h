#pragma once

#include "win/AsyncTaskTracker.h"

#include <windows.h>

#include <functional>
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

// A duplicate name conflict occurs when copying a file to the same folder it
// already lives in. CopyOnce creates a single "- コピー" duplicate for that
// file; ApplyToAll suppresses further prompts for the rest of the batch;
// Cancel aborts the whole paste/transfer batch.
enum class DuplicateConflictChoice { CopyOnce, ApplyToAll, Cancel };

using ConflictConfirmFn = std::function<DuplicateConflictChoice(
    HWND owner, const std::wstring &fileName)>;

// Shows a TaskDialogIndirect prompt asking whether to create a same-folder
// duplicate of fileName.
[[nodiscard]] DuplicateConflictChoice
ShowDuplicateConflictDialog(HWND owner, const std::wstring &fileName);

[[nodiscard]] bool PutFilesOnClipboard(HWND owner,
                                       const std::vector<std::wstring> &paths,
                                       bool cut);
[[nodiscard]] std::vector<std::wstring> ReadFilesFromClipboard(bool *cut);
[[nodiscard]] std::jthread
PasteFilesAsync(HWND notifyWindow, OperationId operationId,
                const std::wstring &destination,
                ConflictConfirmFn confirmConflict = ShowDuplicateConflictDialog);
[[nodiscard]] std::jthread TransferFilesAsync(
    HWND notifyWindow, OperationId operationId,
    std::vector<std::wstring> paths, std::wstring destination, bool move,
    ConflictConfirmFn confirmConflict = ShowDuplicateConflictDialog);
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
