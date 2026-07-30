#include "win/FileOperationController.h"

#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <memory>
#include <utility>

namespace sf::win {

void FileOperationController::CopySelection(
    HWND window, const std::vector<std::wstring> &paths, bool cut,
    const NotifyFn &notify) const {
  if (PutFilesOnClipboard(window, paths, cut))
    notify(cut ? L"切り取りました" : L"コピーしました", false);
}

void FileOperationController::TransferSelectionToOtherPane(
    HWND window, const std::vector<std::wstring> &paths,
    const std::wstring &destination, bool twoPanes, bool move,
    const NotifyFn &notify) {
  if (!twoPanes) {
    notify(L"反対側のペインが表示されていません", true);
    return;
  }
  if (paths.empty())
    return;
  if (destination.empty()) {
    notify(L"反対ペインのコピー先フォルダーがありません", true);
    return;
  }
  ++pendingOperations_;
  TransferFilesAsync(window, paths, destination, move);
  notify(move ? L"反対ペインへの移動を開始しました"
              : L"反対ペインへのコピーを開始しました",
         false);
}

void FileOperationController::Paste(HWND window,
                                    const std::wstring &destination,
                                    const NotifyFn &notify) {
  if (destination.empty())
    return;
  ++pendingOperations_;
  PasteFilesAsync(window, destination);
  notify(L"ファイル操作を開始しました", false);
}

void FileOperationController::DeleteSelection(HWND window,
                                              std::vector<std::wstring> paths,
                                              bool permanent,
                                              const NotifyFn &notify) {
  if (paths.empty())
    return;
  ++pendingOperations_;
  DeleteFilesAsync(window, std::move(paths), permanent);
  notify(L"削除処理を開始しました", false);
}

bool FileOperationController::RenameItem(HWND window, const std::wstring &path,
                                         const std::wstring &newName,
                                         const NotifyFn &notify) {
  if (path.empty() || newName.empty())
    return false;
  if (newName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
    notify(L"名前に使用できない文字があります", true);
    return false;
  }
  ++pendingOperations_;
  RenameFileAsync(window, path, newName);
  return true;
}

void FileOperationController::NewFolder(HWND window, const std::wstring &parent,
                                        const PromptTextFn &promptText,
                                        const NotifyFn &notify) {
  if (parent.empty())
    return;
  const std::wstring name =
      promptText(L"新しいフォルダー", L"フォルダー名", L"新しいフォルダー");
  if (name.empty())
    return;
  if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
    notify(L"フォルダー名に使用できない文字があります", true);
    return;
  }
  ++pendingOperations_;
  CreateFolderAsync(window, parent, name);
  notify(L"フォルダーを作成中です", false);
}

void FileOperationController::ShowSelectedProperties(
    HWND window, const std::vector<std::wstring> &paths) const {
  if (!paths.empty())
    ShowProperties(window, paths.front());
}

void FileOperationController::HandleOperationDone(
    LPARAM lParam, const NotifyFn &notify, const RefreshPaneFn &refreshPane) {
  std::unique_ptr<OperationResult> result(
      reinterpret_cast<OperationResult *>(lParam));
  if (pendingOperations_ > 0)
    --pendingOperations_;
  if (result->aborted) {
    notify(L"ファイル操作をキャンセルしました", false);
  } else if (FAILED(result->result)) {
    notify(L"ファイル操作に失敗しました: " +
               WindowsErrorMessage(HRESULT_CODE(result->result)),
           true);
  } else {
    notify(L"ファイル操作が完了しました", false);
    refreshPane(0);
    refreshPane(1);
  }
}

} // namespace sf::win
