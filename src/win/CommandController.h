#pragma once

#include "core/AppArguments.h"
#include "core/Settings.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

class CommandController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using StartSearchFn = std::function<void(const std::wstring &query)>;
  using NavigateFn =
      std::function<void(const std::wstring &path, bool otherPane)>;
  using LaunchTerminalFn = std::function<void(bool administrator)>;
  using AddBookmarkFn = std::function<void()>;
  using AddApplicationFn = std::function<void()>;
  using RebuildSuggestionsFn = std::function<void()>;

  CommandController() = default;

  [[nodiscard]] bool HasCommandInput(HWND searchEdit) const;
  void RebuildCommandSuggestions(HWND searchEdit, HWND suggestions,
                                 const AppSettings &settings,
                                 const std::wstring &terminalDirectory,
                                 const NotifyFn &notify);
  void MoveCommandSelection(HWND suggestions, int delta) const;
  void AcceptCommandSuggestion(
      HWND window, HWND searchEdit, HWND suggestions, HWND focusTarget,
      const AppSettings &settings, const AppArgumentContext &argumentContext,
      bool control, bool shift, const StartSearchFn &startSearch,
      const NavigateFn &navigate, const LaunchTerminalFn &launchTerminal,
      const NotifyFn &notify);
  void HideCommandSuggestions(HWND suggestions);
  void DismissCommandSuggestions(HWND searchEdit, HWND suggestions,
                                 HWND focusTarget, bool clearInput);
  [[nodiscard]] bool HandleCommandPrefixCharacter(
      HWND searchEdit, wchar_t character, HWND source);
  void AddCommandRegistration(
      HWND searchEdit, HWND suggestions, const AddBookmarkFn &addBookmark,
      const AddApplicationFn &addApplication, const NotifyFn &notify,
      const RebuildSuggestionsFn &rebuildSuggestions);
  void LaunchRegisteredApplication(
      HWND window, const AppSettings &settings, std::size_t index,
      bool administrator, bool passSelection,
      const AppArgumentContext &argumentContext,
      const NotifyFn &notify) const;

private:
  enum class SuggestionKind {
    Folder,
    Application,
    Terminal,
  };

  struct Suggestion final {
    SuggestionKind kind = SuggestionKind::Folder;
    std::size_t sourceIndex = 0;
    bool administrator = false;
    int score = 0;
    std::wstring label;
    std::wstring detail;
  };

  std::vector<Suggestion> suggestionItems_;
  std::wstring prefixBuffer_;
  HWND prefixSource_ = nullptr;
  ULONGLONG prefixTick_ = 0;
};

} // namespace sf::win
