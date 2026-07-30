#pragma once

#include "core/Settings.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sf::win {

struct SidebarMenuIds final {
  UINT open = 0;
  UINT openAsAdministrator = 0;
  UINT edit = 0;
  UINT remove = 0;
  UINT moveUp = 0;
  UINT moveDown = 0;
};

class SidebarController final {
public:
  using PromptTextFn = std::function<std::wstring(
      const std::wstring &title, const std::wstring &label,
      const std::wstring &initial)>;
  using SaveSettingsFn = std::function<void()>;
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using NavigateFn = std::function<void(const std::wstring &path)>;
  using LaunchApplicationFn =
      std::function<void(std::size_t index, bool administrator)>;

  SidebarController() = default;

  void AddBookmarkForPath(HWND sidebar, AppSettings &settings,
                          const std::wstring &path,
                          const PromptTextFn &promptText,
                          const SaveSettingsFn &saveSettings);
  void AddLinkedFolder(HWND window, HWND sidebar, AppSettings &settings,
                       const PromptTextFn &promptText,
                       const SaveSettingsFn &saveSettings);
  void AddLink(HWND window, HWND sidebar, AppSettings &settings,
               bool application, const PromptTextFn &promptText,
               const SaveSettingsFn &saveSettings);
  void RebuildSidebar(HWND sidebar, const AppSettings &settings);
  void ActivateSidebarItem(HWND window, HWND sidebar,
                           const AppSettings &settings, bool administrator,
                           const NavigateFn &navigate,
                           const LaunchApplicationFn &launchApplication,
                           const NotifyFn &notify) const;
  void EditSidebarItem(HWND window, HWND sidebar, AppSettings &settings,
                       const PromptTextFn &promptText,
                       const SaveSettingsFn &saveSettings);
  void RemoveSidebarItem(HWND sidebar, AppSettings &settings,
                         const SaveSettingsFn &saveSettings);
  void MoveSidebarItem(HWND sidebar, AppSettings &settings, bool up,
                       const SaveSettingsFn &saveSettings);
  void ShowContextMenu(HWND window, HWND sidebar, POINT screenPoint,
                       const AppSettings &settings,
                       const SidebarMenuIds &ids) const;

private:
  std::vector<std::pair<bool, std::size_t>> sidebarMap_;
};

} // namespace sf::win
