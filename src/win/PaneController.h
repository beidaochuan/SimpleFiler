#pragma once

#include "core/Settings.h"
#include "win/FileEnumerator.h"

#include <windows.h>

#include <commctrl.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sf::win {

class PaneController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using SearchStateFn =
      std::function<void(int pane, bool searchMode, bool busy)>;

  PaneController() = default;
  ~PaneController();

  PaneController(const PaneController &) = delete;
  PaneController &operator=(const PaneController &) = delete;

  void AttachControls(int pane, HWND address, HWND list);
  [[nodiscard]] HWND AddressHandle(int pane) const;
  [[nodiscard]] HWND ListHandle(int pane) const;
  [[nodiscard]] int PaneIndexFromControl(HWND control, int fallback) const;

  void ApplySettings(int pane, const PaneSettings &settings);
  void WriteSettings(int pane, PaneSettings &settings) const;

  void Navigate(HWND window, int pane, const std::wstring &path,
                bool addHistory, const NotifyFn &notify,
                const SearchStateFn &searchState);
  void ShowDrives(HWND window, int pane, bool addHistory,
                  const NotifyFn &notify, const SearchStateFn &searchState);
  void NavigateHistory(HWND window, int pane, int delta, const NotifyFn &notify,
                       const SearchStateFn &searchState);
  void NavigateUp(HWND window, int pane, const NotifyFn &notify,
                  const SearchStateFn &searchState);
  void RefreshPane(HWND window, int pane, const NotifyFn &notify,
                   const SearchStateFn &searchState);
  void StartSearch(HWND window, int pane, const std::wstring &query,
                   const NotifyFn &notify, const SearchStateFn &searchState);
  void CancelSearch(int pane, const NotifyFn &notify,
                    const SearchStateFn &searchState);
  void ToggleShowHidden(HWND window, int pane, const NotifyFn &notify,
                        const SearchStateFn &searchState);

  void HandleEnumerationBatch(LPARAM lParam);
  void HandleEnumerationDone(LPARAM lParam, const NotifyFn &notify,
                             const SearchStateFn &searchState);

  void OpenSelected(HWND window, int pane, const NotifyFn &notify,
                    const SearchStateFn &searchState);
  void BeginRename(int pane) const;
  void BeginRenameForNewItem(int pane, const std::wstring &path);
  void RestoreAddressText(int pane) const;
  void SelectAll(int pane) const;
  void SelectContextItem(int pane, int item) const;
  void HandleColumnClick(int pane, int subItem);
  [[nodiscard]] int FindItem(int pane, const NMLVFINDITEMW &find) const;
  void PopulateDisplayInfo(int pane, NMLVDISPINFOW &display) const;

  [[nodiscard]] std::vector<std::wstring> SelectedPaths(int pane) const;
  [[nodiscard]] std::wstring EffectivePath(int pane) const;
  [[nodiscard]] std::wstring SearchQuery(int pane) const;
  [[nodiscard]] std::wstring ItemPath(int pane, int item) const;
  [[nodiscard]] bool IsSearchMode(int pane) const;
  [[nodiscard]] bool IsBusy(int pane) const;
  [[nodiscard]] bool IsDriveView(int pane) const;
  [[nodiscard]] bool HasPath(int pane) const;

  void SetCutPaths(std::vector<std::wstring> paths);
  void ClearCutPaths();
  [[nodiscard]] bool HasCutPaths() const noexcept;
  [[nodiscard]] bool IsItemCut(int pane, int item) const;

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
    std::wstring pendingSelectionPath;
    bool pendingRenameOnSelect = false;
    std::uint64_t generation = 0;
    std::unique_ptr<std::jthread> worker;
    std::vector<RetiredWorker> retiredWorkers;
  };

  [[nodiscard]] static bool IsValidPane(int pane) noexcept;
  [[nodiscard]] static std::wstring AddressDisplayText(const Pane &pane);
  void RestorePendingSelection(int pane);
  void SortPane(int pane);
  void RetireWorker(Pane &pane);
  void FinishWorker(Pane &pane, std::uint64_t generation);

  Pane panes_[2];
  std::vector<std::wstring> cutPaths_;
};

} // namespace sf::win
