#pragma once

#include "core/Settings.h"
#include "win/CommandController.h"
#include "win/FileOperationController.h"
#include "win/PaneController.h"
#include "win/ShellMenuController.h"
#include "win/SidebarController.h"
#include "win/TerminalController.h"
#include "win/ZipController.h"

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace sf::win {

class App final {
public:
  explicit App(HINSTANCE instance);
  ~App();

  int Run(int showCommand, const std::wstring &initialPath);

private:
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  bool RegisterClasses();
  bool CreateMainWindow(int showCommand);
  void CreateControls();
  void CreatePaneControls(int pane);
  void LayoutControls(int width, int height);
  void ApplyDpi(UINT dpi);
  void UpdateActivePaneVisuals();
  void UpdatePaneSearchState(int pane, bool searchMode, bool busy);
  void RestorePaneFocusIfNeeded();
  void CreateAccelerators();
  void InitializeFromSettings(const std::wstring &initialPath);
  void VerifySettingsWritable();
  void SaveSettings();

  void NavigatePane(int pane, const std::wstring &path, bool addHistory = true);
  void RefreshPaneView(int pane);
  void StartPaneSearch(int pane, const std::wstring &query);
  void OpenPaneSelection(int pane);
  void LaunchRegisteredApplication(std::size_t index, bool administrator,
                                   bool passSelection);
  void AcceptCommandSuggestion(bool control, bool shift);

  void ShowAboutDialog();

  [[nodiscard]] AppArgumentContext
  BuildAppArgumentContext(bool includeSelection) const;
  [[nodiscard]] bool HandleOwnerDraw(WPARAM wParam, LPARAM lParam,
                                     LRESULT &result);
  [[nodiscard]] bool HandleControlColor(UINT message, WPARAM wParam,
                                        LPARAM lParam, LRESULT &result);
  [[nodiscard]] bool HandleSplitterMessage(UINT message, WPARAM wParam,
                                           LPARAM lParam, LRESULT &result);
  LRESULT HandleCommand(WPARAM wParam, LPARAM lParam);
  LRESULT HandleNotify(LPARAM lParam);
  [[nodiscard]] bool HandleAppMessage(UINT message, WPARAM wParam,
                                      LPARAM lParam, LRESULT &result);
  [[nodiscard]] std::wstring PromptText(const std::wstring &title,
                                        const std::wstring &label,
                                        const std::wstring &initial = {}) const;
  void Notify(const std::wstring &message, bool error = false);

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND toolbar_[7]{};
  HWND searchEdit_ = nullptr;
  HWND commandSuggestions_ = nullptr;
  HWND sidebar_ = nullptr;
  HWND sidebarTitle_ = nullptr;
  HWND status_ = nullptr;
  HACCEL accelerators_ = nullptr;
  HBRUSH backgroundBrush_ = nullptr;
  HBRUSH surfaceBrush_ = nullptr;
  HBRUSH sidebarBrush_ = nullptr;
  HBRUSH activePaneBrush_ = nullptr;
  HFONT uiFont_ = nullptr;
  HFONT sectionFont_ = nullptr;
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  RECT sidebarCardRect_{};
  RECT paneCardRects_[2]{};
  int activePane_ = 0;
  bool twoPanes_ = true;
  bool sidebarVisible_ = true;
  bool draggingSplitter_ = false;
  bool settingsWritable_ = true;
  CommandController commandController_;
  FileOperationController fileOperationController_;
  PaneController paneController_;
  SidebarController sidebarController_;
  ShellMenuController shellMenuController_;
  ZipController zipController_;
  TerminalController terminalController_;
  double splitRatio_ = 0.5;
  AppSettings settings_;
  SettingsStore settingsStore_;
};

} // namespace sf::win
