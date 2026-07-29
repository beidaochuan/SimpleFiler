#pragma once

#include "core/CommandQuery.h"
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
  enum class CommandSuggestionKind {
    Folder,
    Application,
    Terminal,
  };

  struct CommandSuggestion final {
    CommandSuggestionKind kind = CommandSuggestionKind::Folder;
    std::size_t sourceIndex = 0;
    bool administrator = false;
    int score = 0;
    std::wstring label;
    std::wstring detail;
  };

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
  void ApplyDpi(UINT dpi);
  void UpdateActivePaneVisuals();
  void CreateAccelerators();
  void InitializeFromSettings(const std::wstring &initialPath);
  void VerifySettingsWritable();
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
  void TransferSelectionToOtherPane(bool move);
  void Paste();
  void DeleteSelection(bool permanent);
  void NewFolder();
  void ShowSelectedProperties();
  void AddCurrentBookmark();
  void AddLinkedFolder();
  void AddBookmarkForPath(const std::wstring &path);
  void AddLink(bool application);
  void RebuildSidebar();
  void ActivateSidebarItem(bool administrator = false);
  void EditSidebarItem();
  void RemoveSidebarItem();
  void ShowTerminalMenu(HWND sourceButton);
  void LaunchSelectedTerminal(TerminalKind kind, bool administrator);
  void ShowFileMenu(POINT screenPoint);
  void ShowLinkMenu(HWND sourceButton);
  void CreateZipFromSelection();
  void ExtractSelectedZip();
  void RebuildCommandSuggestions();
  void MoveCommandSelection(int delta);
  void AcceptCommandSuggestion(bool control = false, bool shift = false);
  void DismissCommandSuggestions(bool clearInput);
  bool HandleCommandPrefixCharacter(wchar_t character, HWND source);
  void AddCommandRegistration();
  void LaunchRegisteredApplication(std::size_t index, bool administrator,
                                   bool passSelection);

  [[nodiscard]] std::vector<std::wstring> SelectedPaths() const;
  [[nodiscard]] int PaneIndexFromControl(HWND control) const;
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
  HWND status_ = nullptr;
  HACCEL accelerators_ = nullptr;
  HBRUSH activePaneBrush_ = nullptr;
  HFONT uiFont_ = nullptr;
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  Pane panes_[2];
  int activePane_ = 0;
  bool twoPanes_ = true;
  bool sidebarVisible_ = true;
  bool draggingSplitter_ = false;
  bool settingsWritable_ = true;
  std::size_t pendingFileOperations_ = 0;
  std::size_t pendingZipOperations_ = 0;
  double splitRatio_ = 0.5;
  std::vector<std::pair<bool, std::size_t>> sidebarMap_;
  std::vector<CommandSuggestion> commandSuggestionItems_;
  std::wstring commandPrefixBuffer_;
  HWND commandPrefixSource_ = nullptr;
  ULONGLONG commandPrefixTick_ = 0;
  AppSettings settings_;
  SettingsStore settingsStore_;
};

} // namespace sf::win
