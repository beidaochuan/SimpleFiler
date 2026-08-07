#include "core/Settings.h"
#include "win/AddressBar.h"
#include "win/AppMessages.h"
#include "win/AsyncTaskTracker.h"
#include "win/CommandController.h"
#include "win/FileOperationController.h"
#include "win/PaneController.h"
#include "win/PaneDropTarget.h"
#include "win/ShellMenuController.h"
#include "win/SidebarController.h"
#include "win/TerminalController.h"
#include "win/WinUtils.h"
#include "win/ZipController.h"
#include "win/ZipOperations.h"

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>

#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;
int breadcrumbPane = -1;
std::wstring breadcrumbTarget;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

LRESULT CALLBACK TestWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  if (message == sf::win::kMessageNavigateBreadcrumb) {
    breadcrumbPane = static_cast<int>(wParam);
    const auto *target = reinterpret_cast<const std::wstring *>(lParam);
    breadcrumbTarget = target != nullptr ? *target : L"<null>";
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

class TestControls final {
public:
  bool Create() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = TestWindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"SimpleFiler.ControllerTestWindow";
    if (RegisterClassW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
    window_ =
        CreateWindowExW(0, windowClass.lpszClassName, L"", WS_POPUP, -32000,
                        -32000, 320, 240, nullptr, nullptr,
                        windowClass.hInstance, nullptr);
    if (window_ == nullptr)
      return false;
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 0, 0, 200,
                            24, window_, nullptr, windowClass.hInstance,
                            nullptr);
    suggestions_ =
        CreateWindowExW(0, L"LISTBOX", L"",
                        WS_CHILD | WS_VISIBLE | LBS_NOTIFY, 0, 24, 300, 100,
                        window_, nullptr, windowClass.hInstance, nullptr);
    sidebar_ =
        CreateWindowExW(0, L"LISTBOX", L"",
                        WS_CHILD | WS_VISIBLE | LBS_NOTIFY, 0, 124, 200, 100,
                        window_, nullptr, windowClass.hInstance, nullptr);
    list_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA, 200, 124, 100, 100,
        window_, nullptr, windowClass.hInstance, nullptr);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    return edit_ != nullptr && suggestions_ != nullptr && sidebar_ != nullptr &&
           list_ != nullptr;
  }

  ~TestControls() {
    if (window_ != nullptr)
      DestroyWindow(window_);
  }

  [[nodiscard]] HWND Window() const { return window_; }
  [[nodiscard]] HWND Edit() const { return edit_; }
  [[nodiscard]] HWND Suggestions() const { return suggestions_; }
  [[nodiscard]] HWND Sidebar() const { return sidebar_; }
  [[nodiscard]] HWND List() const { return list_; }

private:
  HWND window_ = nullptr;
  HWND edit_ = nullptr;
  HWND suggestions_ = nullptr;
  HWND sidebar_ = nullptr;
  HWND list_ = nullptr;
};

void TestAsyncTaskTracker() {
  sf::win::AsyncTaskTracker tracker;
  const sf::win::OperationId first = tracker.NextId();
  const sf::win::OperationId second = tracker.NextId();
  std::atomic<int> completed = 0;
  tracker.Track(first, std::jthread([&completed] { ++completed; }));
  tracker.Track(second, std::jthread([&completed] { ++completed; }));
  Check(first != 0 && second == first + 1,
        "Task IDs should be stable and monotonic");
  Check(tracker.Size() == 2, "Tracked tasks should be counted");
  Check(tracker.Complete(first), "Known task completion should be accepted");
  Check(tracker.Size() == 1, "Only the completed task should be removed");
  Check(!tracker.Complete(first),
        "Duplicate task completion should be ignored");
  Check(tracker.Complete(second), "Second task should complete independently");
  Check(tracker.Size() == 0 && completed.load() == 2,
        "Completing tasks should join their worker threads");
}

sf::AppSettings CommandSettings() {
  sf::AppSettings settings;
  settings.links.push_back(
      {"app-alpha", sf::LinkType::Application, "Alpha",
       "C:\\missing-alpha.exe", "", "", "alpha", {}, false});
  settings.links.push_back(
      {"app-beta", sf::LinkType::Application, "Beta",
       "C:\\missing-beta.exe", "", "", "beta", {}, false});
  return settings;
}

void TestCommandController(const TestControls &controls) {
  sf::win::CommandController controller;
  sf::AppSettings settings = CommandSettings();
  int notifications = 0;
  const auto notify = [&notifications](const std::wstring &, bool) {
    ++notifications;
  };

  SetWindowTextW(controls.Edit(), L"aa alpha");
  controller.RebuildCommandSuggestions(controls.Edit(), controls.Suggestions(),
                                       settings, L"C:\\", notify);
  std::swap(settings.links[0], settings.links[1]);
  const sf::win::CommandAction launch = controller.AcceptCommandSuggestion(
      controls.Edit(), controls.Suggestions(), controls.List(), settings, true,
      true);
  Check(launch.kind ==
            sf::win::CommandAction::Kind::LaunchApplication &&
            launch.sourceId == "app-alpha" && launch.administrator &&
            !launch.passSelection,
        "Command selection should resolve applications by stable ID");

  settings = CommandSettings();
  SetWindowTextW(controls.Edit(), L"aa alpha");
  controller.RebuildCommandSuggestions(controls.Edit(), controls.Suggestions(),
                                       settings, L"C:\\", notify);
  settings.links.clear();
  const sf::win::CommandAction stale = controller.AcceptCommandSuggestion(
      controls.Edit(), controls.Suggestions(), controls.List(), settings,
      false, false);
  Check(stale.kind == sf::win::CommandAction::Kind::Error,
        "Removed command targets should return an explicit error action");

  controller.HideCommandSuggestions(controls.Suggestions());
  SetWindowTextW(controls.Edit(), L"report");
  const sf::win::CommandAction search = controller.AcceptCommandSuggestion(
      controls.Edit(), controls.Suggestions(), controls.List(), settings,
      false, false);
  Check(search.kind == sf::win::CommandAction::Kind::Search &&
            search.value == L"report",
        "Plain command input should produce a search action");

  SetWindowTextW(controls.Edit(), L"ff work");
  Check(controller.RequestCommandRegistration(controls.Edit(),
                                              controls.Suggestions()) ==
            sf::win::CommandRegistrationKind::Bookmark,
        "ff should request bookmark registration");
  SetWindowTextW(controls.Edit(), L"aa editor");
  Check(controller.RequestCommandRegistration(controls.Edit(),
                                              controls.Suggestions()) ==
            sf::win::CommandRegistrationKind::Application,
        "aa should request application registration");
  Check(notifications == 0,
        "Matching command suggestions should not emit warnings");
}

void TestSidebarController(const TestControls &controls) {
  sf::win::SidebarController controller;
  sf::AppSettings settings;
  settings.bookmarks.push_back(
      {"bookmark-one", "One", "C:\\One", "", {}});
  settings.bookmarks.push_back(
      {"bookmark-two", "Two", "C:\\Two", "", {}});
  controller.RebuildSidebar(controls.Sidebar(), settings);
  std::swap(settings.bookmarks[0], settings.bookmarks[1]);
  SendMessageW(controls.Sidebar(), LB_SETCURSEL, 0, 0);

  std::wstring navigated;
  controller.ActivateSidebarItem(
      controls.Window(), controls.Sidebar(), settings, false,
      [&navigated](const std::wstring &path) { navigated = path; },
      [](const std::string &, bool) {},
      [](const std::wstring &, bool) {});
  Check(navigated == L"C:\\One",
        "Sidebar activation should resolve bookmarks by stable ID");

  int saves = 0;
  controller.MoveSidebarItem(controls.Sidebar(), settings, true,
                             [&saves] { ++saves; });
  Check(settings.bookmarks.front().id == "bookmark-one" &&
            SendMessageW(controls.Sidebar(), LB_GETCURSEL, 0, 0) == 0 &&
            saves == 1,
        "Sidebar movement should restore selection from the stable ID");

  controller.RebuildSidebar(controls.Sidebar(), settings);
  std::swap(settings.bookmarks[0], settings.bookmarks[1]);
  SendMessageW(controls.Sidebar(), LB_SETCURSEL, 0, 0);
  controller.RemoveSidebarItem(controls.Sidebar(), settings,
                               [&saves] { ++saves; });
  Check(settings.bookmarks.size() == 1 &&
            settings.bookmarks.front().id == "bookmark-two" && saves == 2,
        "Sidebar removal should delete the stable-ID target after reordering");
}

void TestPaneController(const TestControls &controls) {
  sf::win::PaneController controller;
  controller.AttachControls(0, controls.Edit(), controls.List());
  Check(controller.AddressHandle(0) == controls.Edit() &&
            controller.ListHandle(0) == controls.List(),
        "Pane controls should be attached");
  Check(controller.PaneIndexFromControl(controls.List(), 1) == 0 &&
            controller.PaneIndexFromControl(nullptr, 1) == 1,
        "Pane lookup should identify controls and preserve fallback");

  sf::PaneSettings input;
  input.sortColumn = 2;
  input.sortAscending = false;
  input.showHidden = true;
  controller.ApplySettings(0, input);
  sf::PaneSettings output;
  controller.WriteSettings(0, output);
  Check(output.sortColumn == 2 && !output.sortAscending && output.showHidden,
        "Pane settings should round-trip through the controller");

  auto batch = std::make_unique<sf::win::EnumerationBatch>();
  batch->items = {{L"C:\\Alpha.txt", L"Alpha.txt"},
                  {L"C:\\alpine.txt", L"alpine.txt"},
                  {L"C:\\Beta.txt", L"Beta.txt"}};
  controller.HandleEnumerationBatch(reinterpret_cast<LPARAM>(batch.release()));

  NMLVFINDITEMW find{};
  find.iStart = 1;
  find.lvfi.flags = LVFI_STRING | LVFI_PARTIAL;
  find.lvfi.psz = L"ALP";
  Check(controller.FindItem(0, find) == 1,
        "Quick-key search should find a case-insensitive prefix");
  find.iStart = 2;
  find.lvfi.flags |= LVFI_WRAP;
  Check(controller.FindItem(0, find) == 0,
        "Quick-key search should wrap to the beginning");
  find.iStart = 0;
  find.lvfi.flags = LVFI_STRING;
  find.lvfi.psz = L"beta.txt";
  Check(controller.FindItem(0, find) == 2,
        "Exact item lookup should remain case-insensitive");
  find.lvfi.psz = L"missing.txt";
  Check(controller.FindItem(0, find) == -1,
        "Quick-key search should report a missing item");
}

void TestRestoreAddressText(const TestControls &controls) {
  sf::win::PaneController controller;
  controller.AttachControls(0, controls.Edit(), controls.List());
  const auto notify = [](const std::wstring &, bool) {};
  const auto searchState = [](int, bool, bool) {};

  const std::wstring directory =
      std::filesystem::temp_directory_path().wstring();
  controller.Navigate(controls.Window(), 0, directory, false, notify,
                      searchState);
  SetWindowTextW(controls.Edit(), L"typed-but-not-navigated");
  controller.RestoreAddressText(0);
  Check(sf::win::GetWindowTextString(controls.Edit()) ==
            controller.EffectivePath(0),
        "Escape while browsing a folder should restore the actual path, "
        "not the typed text");

  controller.ShowDrives(controls.Window(), 0, false, notify, searchState);
  SetWindowTextW(controls.Edit(), L"typed-but-not-navigated");
  controller.RestoreAddressText(0);
  Check(sf::win::GetWindowTextString(controls.Edit()) == L"PC",
        "Escape while browsing drives should restore the \"PC\" label");

  controller.Navigate(controls.Window(), 0, directory, false, notify,
                      searchState);
  controller.StartSearch(controls.Window(), 0, L"report", notify,
                         searchState);
  SetWindowTextW(controls.Edit(), L"typed-but-not-navigated");
  controller.RestoreAddressText(0);
  Check(sf::win::GetWindowTextString(controls.Edit()) ==
            L"検索: " + directory,
        "Escape while searching should restore the search-mode label");
}

void TestAddressBar(const TestControls &controls) {
  breadcrumbPane = -1;
  breadcrumbTarget = L"<unset>";
  SetWindowTextW(controls.Edit(), LR"(D:\)");
  sf::win::AttachAddressBar(controls.Edit(), 1);
  SetFocus(nullptr);

  SendMessageW(controls.Edit(), WM_LBUTTONDOWN, MK_LBUTTON,
               MAKELPARAM(8, 12));
  Check(breadcrumbPane == 1 && breadcrumbTarget.empty(),
        "The PC breadcrumb should synchronously request the drive view");
}

void TestOperationControllers() {
  sf::win::FileOperationController fileController;
  int fileErrors = 0;
  const bool renamed = fileController.RenameItem(
      nullptr, L"C:\\file.txt", L"bad:name",
      [&fileErrors](const std::wstring &, bool error) {
        if (error)
          ++fileErrors;
      });
  Check(!renamed && fileErrors == 1 &&
            fileController.PendingOperationCount() == 0,
        "Invalid rename should fail before starting a worker");

  bool prompted = false;
  fileController.NewFolder(
      nullptr, {},
      [&prompted](const std::wstring &, const std::wstring &,
                  const std::wstring &) {
        prompted = true;
        return std::wstring(L"folder");
      },
      [](const std::wstring &, bool) {});
  Check(!prompted && fileController.PendingOperationCount() == 0,
        "Missing folder parent should not prompt or start a worker");

  sf::win::ZipController zipController;
  int zipNotifications = 0;
  std::vector<int> refreshed;
  auto *unknown =
      new sf::win::ZipResult{999, true, L"unexpected completion"};
  zipController.HandleZipDone(
      reinterpret_cast<LPARAM>(unknown),
      [&zipNotifications](const std::wstring &, bool) { ++zipNotifications; },
      [&refreshed](int pane) { refreshed.push_back(pane); });
  Check(zipNotifications == 0 && refreshed.empty() &&
            zipController.PendingOperationCount() == 0,
        "Unknown ZIP completions should not notify or refresh a pane");
}

void TestComputeDropEffect() {
  constexpr DWORD kBothEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE;
  Check(sf::win::ComputeDropEffect(true, 0, kBothEffects) == DROPEFFECT_MOVE,
        "Same volume with no modifier should default to move");
  Check(sf::win::ComputeDropEffect(false, 0, kBothEffects) == DROPEFFECT_COPY,
        "Different volume with no modifier should default to copy");
  Check(sf::win::ComputeDropEffect(false, MK_SHIFT, kBothEffects) ==
            DROPEFFECT_MOVE,
        "Shift should force move regardless of volume");
  Check(sf::win::ComputeDropEffect(true, MK_CONTROL, kBothEffects) ==
            DROPEFFECT_COPY,
        "Ctrl should force copy regardless of volume");
  Check(sf::win::ComputeDropEffect(false, MK_SHIFT | MK_CONTROL,
                                   kBothEffects) == DROPEFFECT_MOVE,
        "Shift should take priority over Ctrl when both are held");
  Check(sf::win::ComputeDropEffect(true, MK_SHIFT, DROPEFFECT_COPY) ==
            DROPEFFECT_NONE,
        "Computed effect not present in the offered mask should be none");
}

void TestTerminalAndShellMenuControllers(const TestControls &controls) {
  sf::win::TerminalController terminal;
  bool terminalError = false;
  terminal.LaunchSelectedTerminal(
      controls.Window(), L"Z:\\path-that-does-not-exist",
      sf::win::TerminalKind::CommandPrompt, false,
      [&terminalError](const std::wstring &, bool error) {
        terminalError = error;
      });
  Check(terminalError,
        "Terminal controller should report an invalid working directory");

  sf::win::ShellMenuController shellMenu;
  LRESULT result = 123;
  Check(!shellMenu.HandleMenuMessage(WM_INITMENUPOPUP, 0, 0, result) &&
            result == 123,
        "Shell menu messages should pass through without an active menu");
}

} // namespace

int main() {
  const HRESULT initialized =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) {
    std::cerr << "COM initialization failed\n";
    return 1;
  }
  INITCOMMONCONTROLSEX controlsInitialization{
      sizeof(controlsInitialization), ICC_LISTVIEW_CLASSES};
  if (!InitCommonControlsEx(&controlsInitialization)) {
    std::cerr << "Common controls initialization failed\n";
    CoUninitialize();
    return 1;
  }

  TestControls controls;
  if (!controls.Create()) {
    std::cerr << "Controller test controls could not be created\n";
    CoUninitialize();
    return 1;
  }

  TestAsyncTaskTracker();
  TestCommandController(controls);
  TestSidebarController(controls);
  TestPaneController(controls);
  TestRestoreAddressText(controls);
  TestOperationControllers();
  TestComputeDropEffect();
  TestTerminalAndShellMenuControllers(controls);
  TestAddressBar(controls);

  if (failures == 0)
    std::cout << "Controller tests passed\n";
  CoUninitialize();
  return failures == 0 ? 0 : 1;
}
