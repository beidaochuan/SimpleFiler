#pragma once

#include <windows.h>

#include <string>

namespace sf::win {

enum class TerminalKind { CommandPrompt, PowerShell };

struct TerminalLaunchResult final {
  bool launched = false;
  bool cancelled = false;
  DWORD error = ERROR_SUCCESS;
  std::wstring executable;
};

[[nodiscard]] TerminalLaunchResult LaunchTerminal(HWND owner,
                                                  const std::wstring &directory,
                                                  TerminalKind kind,
                                                  bool runAsAdministrator);

[[nodiscard]] std::wstring FindPowerShell();

} // namespace sf::win
