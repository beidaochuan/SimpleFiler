#include "core/Settings.h"
#include "core/TerminalCommand.h"
#include "win/AppMessages.h"
#include "win/ShellOperations.h"

#include <objbase.h>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace {

std::optional<sf::win::OperationResult> operationResult;

LRESULT CALLBACK TestWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  if (message == sf::win::kMessageOperationDone) {
    std::unique_ptr<sf::win::OperationResult> result(
        reinterpret_cast<sf::win::OperationResult *>(lParam));
    operationResult = *result;
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

bool WaitForOperation(sf::win::OperationId expectedId,
                      std::chrono::seconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!operationResult && std::chrono::steady_clock::now() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
  }
  return operationResult && operationResult->operationId == expectedId &&
         SUCCEEDED(operationResult->result) &&
         !operationResult->aborted;
}

void ResetOperation() {
  operationResult.reset();
}

bool WriteTestFile(const std::filesystem::path &path, std::string text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
  return static_cast<bool>(stream);
}

bool RunCmdInDirectory(const std::filesystem::path &directory) {
  std::wstring parameters =
      sf::BuildCmdUncParameters(directory.wstring());
  const std::size_t keep = parameters.find(L"/K");
  if (keep == std::wstring::npos)
    return false;
  parameters.replace(keep, 2, L"/C");
  parameters += L" & type nul > cmd-marker.txt";
  std::wstring commandLine = L"cmd.exe " + parameters;

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    return false;
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
  DWORD exitCode = 1;
  if (wait == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0 && exitCode == 0 &&
         std::filesystem::exists(directory / L"cmd-marker.txt");
}

} // namespace

int main() {
  const HRESULT initialized =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) {
    std::cerr << "COM initialization failed\n";
    return 1;
  }

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = TestWindowProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"SimpleFiler.FileOperationTestWindow";
  if (RegisterClassW(&windowClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::cerr << "Test window class registration failed\n";
    CoUninitialize();
    return 1;
  }
  const HWND window =
      CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  if (window == nullptr) {
    std::cerr << "Test message window creation failed\n";
    CoUninitialize();
    return 1;
  }

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("simplefiler-file-operations-" + sf::MakeStableId());
  const std::filesystem::path source = root / L"source";
  const std::filesystem::path destination = root / L"destination";
  std::error_code error;
  std::filesystem::create_directories(source, error);
  std::filesystem::create_directories(destination, error);

  int failures = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };

  const std::filesystem::path cmdDirectory =
      root / L"A&B 100% !bang ^(x)";
  std::filesystem::create_directories(cmdDirectory, error);
  check(RunCmdInDirectory(cmdDirectory),
        "CMD should preserve legal metacharacters in a quoted path");

  const std::filesystem::path copySource = source / L"copy-日本語.txt";
  check(WriteTestFile(copySource, "copy"), "Copy source should be created");
  ResetOperation();
  constexpr sf::win::OperationId copyOperationId = 1;
  std::jthread copyTask = sf::win::TransferFilesAsync(
      window, copyOperationId, {copySource.wstring()}, destination.wstring(),
      false);
  check(WaitForOperation(copyOperationId), "Direct shell copy should complete");
  copyTask.join();
  check(std::filesystem::exists(copySource),
        "Copy should preserve the source file");
  check(std::filesystem::exists(destination / copySource.filename()),
        "Copy should create the destination file");

  const std::filesystem::path moveSource = source / L"move.txt";
  check(WriteTestFile(moveSource, "move"), "Move source should be created");
  ResetOperation();
  constexpr sf::win::OperationId moveOperationId = 2;
  std::jthread moveTask = sf::win::TransferFilesAsync(
      window, moveOperationId, {moveSource.wstring()}, destination.wstring(),
      true);
  check(WaitForOperation(moveOperationId), "Direct shell move should complete");
  moveTask.join();
  check(!std::filesystem::exists(moveSource),
        "Move should remove the source file");
  check(std::filesystem::exists(destination / moveSource.filename()),
        "Move should create the destination file");

  ResetOperation();
  constexpr sf::win::OperationId folderOperationId = 3;
  std::jthread folderTask = sf::win::CreateFolderAsync(
      window, folderOperationId, destination.wstring(), L"new-folder");
  check(WaitForOperation(folderOperationId),
        "Shell folder creation should complete");
  folderTask.join();
  check(std::filesystem::is_directory(destination / L"new-folder"),
        "Shell folder creation should create a directory");

  const std::filesystem::path renameSource = destination / L"rename-before.txt";
  check(WriteTestFile(renameSource, "rename"),
        "Rename source should be created");
  ResetOperation();
  constexpr sf::win::OperationId renameOperationId = 4;
  std::jthread renameTask =
      sf::win::RenameFileAsync(window, renameOperationId,
                               renameSource.wstring(), L"rename-after.txt");
  check(WaitForOperation(renameOperationId), "Shell rename should complete");
  renameTask.join();
  check(!std::filesystem::exists(renameSource) &&
            std::filesystem::exists(destination / L"rename-after.txt"),
        "Shell rename should change the file name");

  const std::filesystem::path duplicateSource = destination / L"dup-日本語.txt";
  check(WriteTestFile(duplicateSource, "dup"),
        "Duplicate source should be created");
  ResetOperation();
  constexpr sf::win::OperationId duplicateOperationId = 5;
  std::jthread duplicateTask = sf::win::TransferFilesAsync(
      window, duplicateOperationId, {duplicateSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::CopyOnce;
      });
  check(WaitForOperation(duplicateOperationId),
        "Same-folder copy should complete instead of failing");
  duplicateTask.join();
  check(std::filesystem::exists(duplicateSource),
        "Same-folder copy should keep the original file");
  check(std::filesystem::exists(destination / L"dup-日本語 - コピー.txt"),
        "Same-folder copy should create a \"- コピー\" duplicate");

  ResetOperation();
  constexpr sf::win::OperationId duplicateAgainOperationId = 6;
  std::jthread duplicateAgainTask = sf::win::TransferFilesAsync(
      window, duplicateAgainOperationId, {duplicateSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::CopyOnce;
      });
  check(WaitForOperation(duplicateAgainOperationId),
        "Repeated same-folder copy should complete");
  duplicateAgainTask.join();
  check(std::filesystem::exists(destination / L"dup-日本語 - コピー (2).txt"),
        "Repeated same-folder copy should number the next duplicate");

  const std::filesystem::path applyAllSource =
      destination / L"apply-all.txt";
  check(WriteTestFile(applyAllSource, "apply-all"),
        "Apply-all source should be created");
  ResetOperation();
  int confirmCalls = 0;
  constexpr sf::win::OperationId applyAllOperationId = 7;
  std::jthread applyAllTask = sf::win::TransferFilesAsync(
      window, applyAllOperationId,
      {applyAllSource.wstring(), applyAllSource.wstring()},
      destination.wstring(), false,
      [&confirmCalls](HWND, const std::wstring &) {
        ++confirmCalls;
        return sf::win::DuplicateConflictChoice::ApplyToAll;
      });
  check(WaitForOperation(applyAllOperationId),
        "Same-folder copy batch with ApplyToAll should complete");
  applyAllTask.join();
  check(confirmCalls == 1,
        "ApplyToAll should suppress the prompt for the rest of the batch");
  check(std::filesystem::exists(destination / L"apply-all - コピー.txt") &&
            std::filesystem::exists(
                destination / L"apply-all - コピー (2).txt"),
        "ApplyToAll should duplicate every same-folder item in the batch");

  const std::filesystem::path cancelSource = destination / L"cancel.txt";
  check(WriteTestFile(cancelSource, "cancel"),
        "Cancel source should be created");
  ResetOperation();
  constexpr sf::win::OperationId cancelOperationId = 8;
  std::jthread cancelTask = sf::win::TransferFilesAsync(
      window, cancelOperationId, {cancelSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::Cancel;
      });
  const bool cancelled =
      WaitForOperation(cancelOperationId) == false && operationResult &&
      operationResult->operationId == cancelOperationId &&
      operationResult->aborted;
  check(cancelled, "Cancelling a same-folder conflict should abort the batch");
  cancelTask.join();
  check(!std::filesystem::exists(destination / L"cancel - コピー.txt"),
        "Cancelling should not create a duplicate for the conflicting file");

  DestroyWindow(window);
  std::filesystem::remove_all(root, error);
  CoUninitialize();
  if (failures == 0)
    std::cout << "Windows file operation tests passed\n";
  return failures == 0 ? 0 : 1;
}
