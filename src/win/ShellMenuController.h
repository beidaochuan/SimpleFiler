#pragma once

#include "core/Settings.h"

#include <windows.h>

#include <shobjidl.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

struct ShellMenuIds final {
  UINT addFolderLink = 0;
  UINT addFileLink = 0;
  UINT addApplicationLink = 0;
  UINT paste = 0;
  UINT newFolder = 0;
  UINT open = 0;
  UINT copy = 0;
  UINT cut = 0;
  UINT rename = 0;
  UINT deleteItem = 0;
  UINT properties = 0;
  UINT registeredApplicationBase = 0;
  UINT shellMenuFirst = 0;
  UINT shellMenuLast = 0;
};

class ShellMenuController final {
public:
  using RefreshPaneFn = std::function<void()>;
  using OpenSelectedFn = std::function<void()>;
  using BeginRenameFn = std::function<void()>;
  using LaunchApplicationFn = std::function<void(std::size_t index)>;

  ShellMenuController() = default;
  ~ShellMenuController();

  ShellMenuController(const ShellMenuController &) = delete;
  ShellMenuController &operator=(const ShellMenuController &) = delete;

  void ShowLinkMenu(HWND window, HWND sourceButton,
                    const ShellMenuIds &ids) const;
  void ShowFileMenu(HWND window, POINT screenPoint,
                    const std::vector<std::wstring> &paths,
                    const std::wstring &backgroundFolder, bool driveView,
                    const AppSettings &settings, const ShellMenuIds &ids,
                    const RefreshPaneFn &refreshPane,
                    const OpenSelectedFn &openSelected,
                    const BeginRenameFn &beginRename,
                    const LaunchApplicationFn &launchApplication);
  [[nodiscard]] bool HandleMenuMessage(UINT message, WPARAM wParam,
                                       LPARAM lParam, LRESULT &result) const;

private:
  void AppendFallbackBackgroundMenu(HWND window, POINT screenPoint,
                                    const ShellMenuIds &ids) const;
  void ShowBackgroundShellMenu(HWND window, const std::wstring &folderPath,
                               POINT screenPoint, const ShellMenuIds &ids,
                               const RefreshPaneFn &refreshPane);
  [[nodiscard]] bool
  ShowItemShellMenu(HWND window, const std::vector<std::wstring> &paths,
                    POINT screenPoint, const ShellMenuIds &ids,
                    const RefreshPaneFn &refreshPane,
                    const OpenSelectedFn &openSelected,
                    const BeginRenameFn &beginRename);
  void ClearCachedBackgroundMenu();

  IContextMenu2 *activeShellMenu2_ = nullptr;
  IContextMenu3 *activeShellMenu3_ = nullptr;
  std::wstring cachedBackgroundMenuFolder_;
  IContextMenu *cachedBackgroundMenu_ = nullptr;
};

} // namespace sf::win
