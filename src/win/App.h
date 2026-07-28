#pragma once

#include "core/Settings.h"
#include "win/FileEnumerator.h"
#include "win/TerminalLauncher.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sf::win {

class App final {
public:
  explicit App(HINSTANCE instance);
  ~App();

  int Run(int showCommand, const std::wstring &initialPath);

private:
  struct Pane final {
    struct RetiredWorker final {
      std::uint64_t generation = 0;
      std::unique_ptr<std::jthread> thread;
    };

    HWND address = nullptr;
    HWND list = nullptr;
    std::wstring path;
    std::wstring searchRoot;
    std::wstring searchQuery;
    bool driveView = false;
    bool searchMode = false;
    bool busy = false;
    bool showHidden = false;
    int sortColumn = 0;
    bool sortAscending = true;
    std::vector<std::wstring> history;
    std::size_t historyIndex = 0;
    std::vector<FileItem> items;
    std::uint64_t generation = 0;
    std::unique_ptr<std::jthread> worker;
    std::vector<RetiredWorker> retiredWorkers;
  };

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  bool RegisterClasses();
  bool CreateMainWindow(int showCommand);
  void CreateControls();
  void CreatePaneControls(int pane);
  void LayoutControls(int width, int height);
  void CreateAccelerators();
  void InitializeFromSettings(const std::wstring &initialPath);
  void SaveSettings();

  void Navigate(int pane, const std::wstring &path, bool addHistory = true);
  void NavigateHistory(int delta);
  void NavigateUp();
  void ShowDrives(int pane, bool addHistory = true);
  void RefreshPane(int pane);
  void StartSearch(const std::wstring &query);
  void SortPane(int pane);
  void RetireWorker(Pane &pane);
  void FinishWorker(Pane &pane, std::uint64_t generation);
  void OpenSelected();
  void BeginRename();
  void CopySelection(bool cut);
  void Paste();
  void DeleteSelection(bool permanent);
  void NewFolder();
  void ShowSelectedProperties();
  void AddCurrentBookmark();
  void AddLink(bool application);
  void RebuildSidebar();
  void ActivateSidebarItem(bool administrator = false);
  void RemoveSidebarItem();
  void ShowTerminalMenu(HWND sourceButton);
  void LaunchSelectedTerminal(TerminalKind kind, bool administrator);
  void ShowFileMenu(POINT screenPoint);
  void ShowLinkMenu(HWND sourceButton);
  void CreateZipFromSelection();
  void ExtractSelectedZip();

  [[nodiscard]] std::vector<std::wstring> SelectedPaths() const;
  [[nodiscard]] int PaneIndexFromControl(HWND control) const;
  [[nodiscard]] std::wstring PromptText(const std::wstring &title,
                                        const std::wstring &label,
                                        const std::wstring &initial = {}) const;
  void Notify(const std::wstring &message, bool error = false);

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND toolbar_[11]{};
  HWND searchEdit_ = nullptr;
  HWND sidebar_ = nullptr;
  HWND status_ = nullptr;
  HACCEL accelerators_ = nullptr;
  Pane panes_[2];
  int activePane_ = 0;
  bool twoPanes_ = true;
  bool sidebarVisible_ = true;
  bool draggingSplitter_ = false;
  double splitRatio_ = 0.5;
  std::vector<std::pair<bool, std::size_t>> sidebarMap_;
  AppSettings settings_;
  SettingsStore settingsStore_;
};

} // namespace sf::win
