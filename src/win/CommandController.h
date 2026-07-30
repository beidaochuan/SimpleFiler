#pragma once

#include "core/AppArguments.h"
#include "core/Settings.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

struct CommandAction final {
  enum class Kind {
    None,
    Search,
    Navigate,
    LaunchApplication,
    LaunchTerminal,
    Error,
  };

  Kind kind = Kind::None;
  std::string sourceId;
  std::wstring value;
  bool otherPane = false;
  bool administrator = false;
  bool passSelection = true;
};

enum class CommandRegistrationKind {
  None,
  Bookmark,
  Application,
};

struct RegisteredApplicationLaunch final {
  std::string sourceId;
  bool administrator = false;
  bool passSelection = true;
  AppArgumentContext argumentContext;
};

class CommandController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;

  CommandController() = default;

  [[nodiscard]] bool HasCommandInput(HWND searchEdit) const;
  void RebuildCommandSuggestions(HWND searchEdit, HWND suggestions,
                                 const AppSettings &settings,
                                 const std::wstring &terminalDirectory,
                                 const NotifyFn &notify);
  void MoveCommandSelection(HWND suggestions, int delta) const;
  [[nodiscard]] CommandAction
  AcceptCommandSuggestion(HWND searchEdit, HWND suggestions, HWND focusTarget,
                          const AppSettings &settings, bool control,
                          bool shift);
  void HideCommandSuggestions(HWND suggestions);
  void DismissCommandSuggestions(HWND searchEdit, HWND suggestions,
                                 HWND focusTarget, bool clearInput);
  [[nodiscard]] bool HandleCommandPrefixCharacter(
      HWND searchEdit, wchar_t character, HWND source);
  [[nodiscard]] CommandRegistrationKind
  RequestCommandRegistration(HWND searchEdit, HWND suggestions);
  void LaunchRegisteredApplication(
      HWND window, const AppSettings &settings,
      const RegisteredApplicationLaunch &request,
      const NotifyFn &notify) const;

private:
  enum class SuggestionKind {
    Folder,
    Application,
    Terminal,
  };

  struct Suggestion final {
    SuggestionKind kind = SuggestionKind::Folder;
    std::string sourceId;
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
