#pragma once

#include "win/TerminalLauncher.h"

#include <windows.h>

#include <functional>
#include <string>

namespace sf::win {

struct TerminalMenuIds final {
  UINT commandPrompt = 0;
  UINT commandPromptAdmin = 0;
  UINT powerShell = 0;
  UINT powerShellAdmin = 0;
};

class TerminalController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;

  TerminalController() = default;

  void ShowTerminalMenu(HWND window, HWND sourceButton, bool enabled,
                        const TerminalMenuIds &ids) const;
  void LaunchSelectedTerminal(HWND window, const std::wstring &directory,
                              TerminalKind kind, bool administrator,
                              const NotifyFn &notify) const;
};

} // namespace sf::win
