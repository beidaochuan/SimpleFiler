#include "win/TerminalController.h"

#include "win/WinUtils.h"

namespace sf::win {

void TerminalController::ShowTerminalMenu(
    HWND window, HWND sourceButton, bool enabled,
    const TerminalMenuIds &ids) const {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ids.commandPrompt, L"CMDをここで開く");
  AppendMenuW(menu, MF_STRING, ids.commandPromptAdmin,
              L"管理者としてCMDをここで開く");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ids.powerShell, L"PowerShellをここで開く");
  AppendMenuW(menu, MF_STRING, ids.powerShellAdmin,
              L"管理者としてPowerShellをここで開く");
  if (!enabled) {
    EnableMenuItem(menu, ids.commandPrompt, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, ids.commandPromptAdmin, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, ids.powerShell, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, ids.powerShellAdmin, MF_BYCOMMAND | MF_GRAYED);
  }
  RECT rectangle{};
  GetWindowRect(sourceButton, &rectangle);
  TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rectangle.left,
                 rectangle.bottom, 0, window, nullptr);
  DestroyMenu(menu);
}

void TerminalController::LaunchSelectedTerminal(
    HWND window, const std::wstring &directory, TerminalKind kind,
    bool administrator, const NotifyFn &notify) const {
  const TerminalLaunchResult result =
      LaunchTerminal(window, directory, kind, administrator);
  if (result.launched || result.cancelled) {
    if (result.cancelled)
      notify(L"管理者起動をキャンセルしました", false);
    return;
  }
  notify(L"端末を起動できません: " + WindowsErrorMessage(result.error), true);
}

} // namespace sf::win
