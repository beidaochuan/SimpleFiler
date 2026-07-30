#include "win/ZipController.h"

#include "win/WinUtils.h"
#include "win/ZipOperations.h"

#include <commdlg.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>

namespace sf::win {
namespace {

bool HasZipExtension(const std::wstring &path) {
  const wchar_t *extension = PathFindExtensionW(path.c_str());
  return extension != nullptr && _wcsicmp(extension, L".zip") == 0;
}

} // namespace

void ZipController::CreateZipFromSelection(
    HWND window, const std::vector<std::wstring> &paths,
    const NotifyFn &notify) {
  if (paths.empty())
    return;
  std::array<wchar_t, 32768> output{};
  wcscpy_s(output.data(), output.size(), L"archive.zip");
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window;
  dialog.lpstrFile = output.data();
  dialog.nMaxFile = static_cast<DWORD>(output.size());
  dialog.lpstrFilter = L"ZIPアーカイブ (*.zip)\0*.zip\0";
  dialog.lpstrDefExt = L"zip";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&dialog)) {
    std::error_code absoluteError;
    const std::filesystem::path destination =
        std::filesystem::absolute(output.data(), absoluteError);
    if (absoluteError) {
      notify(L"ZIP出力先を解決できません", true);
      return;
    }
    const bool overwritesSource = std::any_of(
        paths.begin(), paths.end(), [&destination](const std::wstring &source) {
          std::error_code error;
          return std::filesystem::equivalent(destination, source, error);
        });
    if (overwritesSource) {
      notify(L"選択したファイル自身をZIP出力先にはできません", true);
      return;
    }
    const OperationId operationId = tasks_.NextId();
    tasks_.Track(operationId,
                 CreateZipAsync(window, operationId, paths, output.data()));
    notify(L"ZIPを作成中です", false);
  }
}

void ZipController::ExtractSelectedZip(
    HWND window, const std::vector<std::wstring> &paths,
    const NotifyFn &notify) {
  if (paths.size() != 1 || !HasZipExtension(paths.front()))
    return;
  const std::wstring destination = PickFolder(window, L"展開先を選択");
  if (destination.empty())
    return;
  const OperationId operationId = tasks_.NextId();
  tasks_.Track(operationId,
               ExtractZipAsync(window, operationId, paths.front(), destination));
  notify(L"ZIPを展開中です", false);
}

void ZipController::HandleZipDone(LPARAM lParam, const NotifyFn &notify,
                                  const RefreshPaneFn &refreshPane) {
  std::unique_ptr<ZipResult> result(reinterpret_cast<ZipResult *>(lParam));
  if (!tasks_.Complete(result->operationId))
    return;
  if (result->success) {
    notify(result->message, false);
    refreshPane(0);
    refreshPane(1);
  } else {
    notify(result->message, true);
  }
}

} // namespace sf::win
