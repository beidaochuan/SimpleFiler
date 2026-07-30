#include "win/CommandController.h"

#include "core/CommandQuery.h"
#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <optional>
#include <tuple>

namespace sf::win {

bool CommandController::HasCommandInput(HWND searchEdit) const {
  return ParseCommandQuery(GetWindowTextString(searchEdit)).mode !=
         CommandMode::None;
}

void CommandController::RebuildCommandSuggestions(
    HWND searchEdit, HWND suggestions, const AppSettings &settings,
    const std::wstring &terminalDirectory, const NotifyFn &notify) {
  const CommandQuery command =
      ParseCommandQuery(GetWindowTextString(searchEdit));
  SendMessageW(suggestions, LB_RESETCONTENT, 0, 0);
  suggestionItems_.clear();
  if (command.mode == CommandMode::None) {
    ShowWindow(suggestions, SW_HIDE);
    return;
  }

  const auto bestScore = [&command](std::initializer_list<std::wstring> values)
      -> std::optional<int> {
    std::optional<int> best;
    for (const std::wstring &value : values) {
      const std::optional<int> score = CommandMatchScore(command.query, value);
      if (score && (!best || *score > *best))
        best = score;
    }
    return best;
  };

  if (command.mode == CommandMode::Folder) {
    for (const Bookmark &bookmark : settings.bookmarks) {
      const std::wstring name = Utf8ToWide(bookmark.name);
      const std::wstring path = Utf8ToWide(bookmark.path);
      const std::wstring alias = Utf8ToWide(bookmark.alias);
      std::optional<int> score = bestScore({name, path, alias});
      for (const std::string &keyword : bookmark.keywords) {
        const std::optional<int> keywordScore =
            CommandMatchScore(command.query, Utf8ToWide(keyword));
        if (keywordScore && (!score || *keywordScore > *score))
          score = keywordScore;
      }
      if (score) {
        const std::wstring label =
            alias.empty() ? name : name + L" [" + alias + L"]";
        suggestionItems_.push_back(
            {SuggestionKind::Folder, bookmark.id, false, *score, label, path});
      }
    }
  } else if (command.mode == CommandMode::Application) {
    for (const RegisteredLink &link : settings.links) {
      if (link.type != LinkType::Application)
        continue;
      const std::wstring name = Utf8ToWide(link.name);
      const std::wstring target = ResolveAppPath(Utf8ToWide(link.target));
      const std::wstring alias = Utf8ToWide(link.alias);
      std::optional<int> score = bestScore({name, target, alias});
      for (const std::string &keyword : link.keywords) {
        const std::optional<int> keywordScore =
            CommandMatchScore(command.query, Utf8ToWide(keyword));
        if (keywordScore && (!score || *keywordScore > *score))
          score = keywordScore;
      }
      if (score) {
        const std::wstring label =
            alias.empty() ? name : name + L" [" + alias + L"]";
        suggestionItems_.push_back(
            {SuggestionKind::Application, link.id, false, *score, label,
             target});
      }
    }
  } else if (command.mode == CommandMode::Terminal) {
    for (const auto &[administrator, terms, label] :
         std::array<std::tuple<bool, std::wstring, std::wstring>, 2>{
             {{false, L"通常 normal", L"CMDをここで開く"},
              {true, L"admin 管理者", L"管理者CMDをここで開く"}}}) {
      const std::optional<int> score = bestScore({terms, label});
      if (score) {
        suggestionItems_.push_back({SuggestionKind::Terminal, {},
                                    administrator, *score, label,
                                    terminalDirectory});
      }
    }
  }

  std::stable_sort(suggestionItems_.begin(), suggestionItems_.end(),
                   [](const Suggestion &left, const Suggestion &right) {
                     if (left.score != right.score)
                       return left.score > right.score;
                     return left.label < right.label;
                   });
  for (const Suggestion &suggestion : suggestionItems_) {
    const std::wstring text = suggestion.detail.empty()
                                  ? suggestion.label
                                  : suggestion.label + L"    " +
                                        suggestion.detail;
    SendMessageW(suggestions, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text.c_str()));
  }
  if (suggestionItems_.empty()) {
    ShowWindow(suggestions, SW_HIDE);
    notify(L"一致する候補がありません", false);
    return;
  }
  SendMessageW(suggestions, LB_SETCURSEL, 0, 0);
  SetWindowPos(suggestions, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void CommandController::MoveCommandSelection(HWND suggestions,
                                             int delta) const {
  if (!IsWindowVisible(suggestions) || suggestionItems_.empty())
    return;
  int selected =
      static_cast<int>(SendMessageW(suggestions, LB_GETCURSEL, 0, 0));
  if (selected < 0)
    selected = 0;
  selected = std::clamp(selected + delta, 0,
                        static_cast<int>(suggestionItems_.size()) - 1);
  SendMessageW(suggestions, LB_SETCURSEL, selected, 0);
}

CommandAction CommandController::AcceptCommandSuggestion(
    HWND searchEdit, HWND suggestions, HWND focusTarget,
    const AppSettings &settings, bool control, bool shift) {
  if (!IsWindowVisible(suggestions)) {
    if (HasCommandInput(searchEdit)) {
      return {CommandAction::Kind::Error, {}, L"実行できる候補がありません"};
    }
    return {CommandAction::Kind::Search, {}, GetWindowTextString(searchEdit)};
  }
  const int selected =
      static_cast<int>(SendMessageW(suggestions, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(suggestionItems_.size()))
    return {};
  const Suggestion suggestion = suggestionItems_[selected];
  DismissCommandSuggestions(searchEdit, suggestions, focusTarget, true);

  switch (suggestion.kind) {
  case SuggestionKind::Folder: {
    const auto bookmark =
        std::find_if(settings.bookmarks.begin(), settings.bookmarks.end(),
                     [&suggestion](const Bookmark &entry) {
                       return entry.id == suggestion.sourceId;
                     });
    if (bookmark == settings.bookmarks.end()) {
      return {CommandAction::Kind::Error, {},
              L"候補のフォルダー登録が変更されました"};
    }
    CommandAction action;
    action.kind = CommandAction::Kind::Navigate;
    action.sourceId = suggestion.sourceId;
    action.value = Utf8ToWide(bookmark->path);
    action.otherPane = shift;
    return action;
  }
  case SuggestionKind::Application: {
    const auto application =
        std::find_if(settings.links.begin(), settings.links.end(),
                     [&suggestion](const RegisteredLink &entry) {
                       return entry.id == suggestion.sourceId &&
                              entry.type == LinkType::Application;
                     });
    if (application == settings.links.end()) {
      return {CommandAction::Kind::Error, {},
              L"候補のアプリ登録が変更されました"};
    }
    CommandAction action;
    action.kind = CommandAction::Kind::LaunchApplication;
    action.sourceId = suggestion.sourceId;
    action.administrator = control;
    action.passSelection = !shift;
    return action;
  }
  case SuggestionKind::Terminal:
    return {CommandAction::Kind::LaunchTerminal, {}, {}, false,
            suggestion.administrator};
  }
  return {};
}

void CommandController::HideCommandSuggestions(HWND suggestions) {
  ShowWindow(suggestions, SW_HIDE);
  suggestionItems_.clear();
}

void CommandController::DismissCommandSuggestions(HWND searchEdit,
                                                  HWND suggestions,
                                                  HWND focusTarget,
                                                  bool clearInput) {
  HideCommandSuggestions(suggestions);
  if (clearInput)
    SetWindowTextW(searchEdit, L"");
  SetFocus(focusTarget);
}

bool CommandController::HandleCommandPrefixCharacter(
    HWND searchEdit, wchar_t character, HWND source) {
  constexpr ULONGLONG kPrefixTimeoutMilliseconds = 1500;
  character = static_cast<wchar_t>(
      std::towlower(static_cast<wint_t>(character)));
  const ULONGLONG now = GetTickCount64();
  if (source != prefixSource_ ||
      now - prefixTick_ > kPrefixTimeoutMilliseconds) {
    prefixBuffer_.clear();
  }
  prefixSource_ = source;
  prefixTick_ = now;

  std::wstring completedPrefix;
  if ((character == L'f' || character == L'a') &&
      prefixBuffer_.size() == 1 && prefixBuffer_.front() == character) {
    completedPrefix.assign(2, character);
  } else if (character == L'd' && prefixBuffer_ == L"cm") {
    completedPrefix = L"cmd";
  }

  if (!completedPrefix.empty()) {
    prefixBuffer_.clear();
    SetWindowTextW(searchEdit, completedPrefix.c_str());
    SetFocus(searchEdit);
    SendMessageW(searchEdit, EM_SETSEL,
                 static_cast<WPARAM>(completedPrefix.size()),
                 static_cast<LPARAM>(completedPrefix.size()));
    return true;
  }

  if (character == L'f' || character == L'a' || character == L'c') {
    prefixBuffer_.assign(1, character);
  } else if (character == L'm' && prefixBuffer_ == L"c") {
    prefixBuffer_ = L"cm";
  } else {
    prefixBuffer_.clear();
  }
  return false;
}

CommandRegistrationKind CommandController::RequestCommandRegistration(
    HWND searchEdit, HWND suggestions) {
  const CommandMode mode =
      ParseCommandQuery(GetWindowTextString(searchEdit)).mode;
  HideCommandSuggestions(suggestions);
  if (mode == CommandMode::Folder)
    return CommandRegistrationKind::Bookmark;
  if (mode == CommandMode::Application)
    return CommandRegistrationKind::Application;
  return CommandRegistrationKind::None;
}

void CommandController::LaunchRegisteredApplication(
    HWND window, const AppSettings &settings,
    const RegisteredApplicationLaunch &request,
    const NotifyFn &notify) const {
  const auto application =
      std::find_if(settings.links.begin(), settings.links.end(),
                   [&request](const RegisteredLink &entry) {
                     return entry.id == request.sourceId &&
                            entry.type == LinkType::Application;
                   });
  if (application == settings.links.end()) {
    notify(L"登録アプリの内容が変更されました", true);
    return;
  }
  const RegisteredLink &link = *application;
  const std::wstring target = ResolveAppPath(Utf8ToWide(link.target));
  if (GetFileAttributesW(ToExtendedPath(target).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    notify(L"登録アプリが見つかりません: " + target, true);
    return;
  }

  std::wstring arguments;
  const std::wstring argumentTemplate = Utf8ToWide(link.arguments);
  if (!argumentTemplate.empty()) {
    const AppArgumentExpansion expansion =
        ExpandAppArgumentTemplate(argumentTemplate, request.argumentContext);
    if (!expansion) {
      notify(L"アプリ引数を作成できません: " + expansion.error, true);
      return;
    }
    arguments = expansion.commandLine;
  } else if (request.passSelection) {
    if (request.argumentContext.files.empty()) {
      arguments =
          QuoteWindowsCommandLineArgument(request.argumentContext.folder);
    } else {
      for (const std::wstring &path : request.argumentContext.files) {
        if (!arguments.empty())
          arguments.push_back(L' ');
        arguments += QuoteWindowsCommandLineArgument(path);
      }
    }
  }

  std::wstring workingDirectory =
      ResolveAppPath(Utf8ToWide(link.workingDirectory));
  if (workingDirectory.empty())
    workingDirectory = request.argumentContext.folder;
  if (!OpenPath(window, target, arguments, workingDirectory,
                request.administrator || link.runAsAdministrator)) {
    const DWORD error = GetLastError();
    if (error != ERROR_CANCELLED) {
      notify(L"登録アプリを起動できません: " + WindowsErrorMessage(error), true);
    }
  }
}

} // namespace sf::win
