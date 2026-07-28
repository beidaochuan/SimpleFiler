#include "win/TerminalLauncher.h"

#include "core/TerminalCommand.h"
#include "win/WinUtils.h"

#include <shellapi.h>

namespace sf::win {
namespace {

std::wstring EnvironmentValue(const wchar_t *name) {
  const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
  if (required == 0)
    return {};
  std::wstring value(static_cast<std::size_t>(required), L'\0');
  GetEnvironmentVariableW(name, value.data(), required);
  value.resize(required - 1);
  return value;
}

std::wstring CommandPromptPath() {
  std::wstring path = EnvironmentValue(L"ComSpec");
  if (!path.empty() &&
      GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    return path;
  path = EnvironmentValue(L"SystemRoot") + L"\\System32\\cmd.exe";
  return path;
}

} // namespace

std::wstring FindPowerShell() {
  std::wstring programFiles = EnvironmentValue(L"ProgramW6432");
  if (programFiles.empty())
    programFiles = EnvironmentValue(L"ProgramFiles");
  const std::wstring powerShell7 = programFiles + L"\\PowerShell\\7\\pwsh.exe";
  if (!programFiles.empty() &&
      GetFileAttributesW(powerShell7.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return powerShell7;
  }
  const std::wstring windowsPowerShell =
      EnvironmentValue(L"SystemRoot") +
      L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
  return windowsPowerShell;
}

TerminalLaunchResult LaunchTerminal(HWND owner, const std::wstring &directory,
                                    TerminalKind kind,
                                    bool runAsAdministrator) {
  TerminalLaunchResult result;
  if (!IsDirectory(directory)) {
    result.error = ERROR_DIRECTORY;
    return result;
  }

  result.executable = kind == TerminalKind::CommandPrompt ? CommandPromptPath()
                                                          : FindPowerShell();
  if (GetFileAttributesW(result.executable.c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    result.error = ERROR_FILE_NOT_FOUND;
    return result;
  }

  std::wstring parameters;
  const wchar_t *workingDirectory = directory.c_str();
  if (kind == TerminalKind::CommandPrompt) {
    parameters =
        IsUncPath(directory) ? BuildCmdUncParameters(directory) : L"/D";
    if (IsUncPath(directory))
      workingDirectory = nullptr;
  } else {
    const std::string encoded =
        Utf8ToPowerShellEncodedCommand(WideToUtf8(directory));
    parameters = L"-NoLogo -NoExit -EncodedCommand " + Utf8ToWide(encoded);
    // The encoded Set-Location is authoritative and also supports UNC.
    workingDirectory = nullptr;
  }

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  info.hwnd = owner;
  info.lpVerb = runAsAdministrator ? L"runas" : L"open";
  info.lpFile = result.executable.c_str();
  info.lpParameters = parameters.c_str();
  info.lpDirectory = workingDirectory;
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    result.error = GetLastError();
    result.cancelled = result.error == ERROR_CANCELLED;
    return result;
  }
  if (info.hProcess != nullptr)
    CloseHandle(info.hProcess);
  result.launched = true;
  return result;
}

} // namespace sf::win
