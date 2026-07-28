#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sf::win {

enum class FileOperationKind { Copy, Move, Delete, Rename };

struct OperationResult final {
  HRESULT result = E_FAIL;
  bool aborted = false;
};

[[nodiscard]] bool PutFilesOnClipboard(HWND owner,
                                       const std::vector<std::wstring> &paths,
                                       bool cut);
[[nodiscard]] std::vector<std::wstring> ReadFilesFromClipboard(bool *cut);
void PasteFilesAsync(HWND notifyWindow, const std::wstring &destination);
void TransferFilesAsync(HWND notifyWindow, std::vector<std::wstring> paths,
                        std::wstring destination, bool move);
void DeleteFilesAsync(HWND notifyWindow, std::vector<std::wstring> paths,
                      bool permanent);
void RenameFileAsync(HWND notifyWindow, std::wstring path,
                     std::wstring newName);
void CreateFolderAsync(HWND notifyWindow, std::wstring parent,
                       std::wstring name);

[[nodiscard]] bool OpenPath(HWND owner, const std::wstring &path,
                            const std::wstring &arguments = {},
                            const std::wstring &workingDirectory = {},
                            bool runAsAdministrator = false);
void ShowProperties(HWND owner, const std::wstring &path);

} // namespace sf::win
