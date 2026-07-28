#include "win/App.h"

#include "win/AppMessages.h"
#include "win/ShellOperations.h"
#include "win/TerminalLauncher.h"
#include "win/WinUtils.h"
#include "win/ZipOperations.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <numeric>

namespace sf::win {
namespace {

constexpr wchar_t kWindowClass[] = L"SimpleFiler.MainWindow";
constexpr wchar_t kPromptClass[] = L"SimpleFiler.PromptWindow";
constexpr int kToolbarHeight = 38;
constexpr int kAddressHeight = 28;
constexpr int kStatusHeight = 24;
constexpr int kSidebarWidth = 210;
constexpr int kSplitterWidth = 6;

enum ControlId : int {
  IdBack = 100,
  IdForward,
  IdUp,
  IdRefresh,
  IdDrives,
  IdTogglePanes,
  IdToggleSidebar,
  IdAddBookmark,
  IdAddLink,
  IdTerminal,
  IdSearch,
  IdSearchEdit,
  IdLeftAddress = 200,
  IdRightAddress,
  IdLeftList = 210,
  IdRightList,
  IdSidebar = 220,
  IdCopy = 300,
  IdCut,
  IdPaste,
  IdDelete,
  IdPermanentDelete,
  IdRename,
  IdNewFolder,
  IdProperties,
  IdOpen,
  IdZipCreate,
  IdZipExtract,
  IdShowHidden,
  IdFocusAddress,
  IdFocusSearch,
  IdSwitchPane,
  IdAddFileLink,
  IdAddAppLink,
  IdRemoveSidebar,
  IdOpenSidebar,
  IdOpenSidebarAdmin,
  IdCmd,
  IdCmdAdmin,
  IdPowerShell,
  IdPowerShellAdmin
};

struct PromptState final {
  std::wstring label;
  std::wstring value;
  bool accepted = false;
  HWND edit = nullptr;
};

LRESULT CALLBACK PromptProcedure(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  auto *state =
      reinterpret_cast<PromptState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    state = static_cast<PromptState *>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  switch (message) {
  case WM_CREATE: {
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND label = CreateWindowExW(0, L"STATIC", state->label.c_str(),
                                 WS_CHILD | WS_VISIBLE, 12, 12, 396, 20, window,
                                 nullptr, nullptr, nullptr);
    state->edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 36, 396, 25,
        window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
    HWND ok = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 236, 72, 82, 27,
        window, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    HWND cancel = CreateWindowExW(
        0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 326,
        72, 82, 27, window, reinterpret_cast<HMENU>(IDCANCEL), nullptr,
        nullptr);
    for (HWND control : {label, state->edit, ok, cancel}) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    SendMessageW(state->edit, EM_SETSEL, 0, -1);
    SetFocus(state->edit);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK) {
      state->value = GetWindowTextString(state->edit);
      state->accepted = true;
      DestroyWindow(window);
      return 0;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      DestroyWindow(window);
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(window);
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK EditSubclass(HWND window, UINT message, WPARAM wParam,
                              LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
  if (message == WM_KEYDOWN && wParam == VK_RETURN) {
    const UINT target =
        reference < 2 ? kMessageNavigateAddress : kMessageSearch;
    PostMessageW(GetParent(window), target, static_cast<WPARAM>(reference), 0);
    return 0;
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

std::wstring HomeDirectory() {
  PWSTR path = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) {
    result = path;
    CoTaskMemFree(path);
  }
  return result;
}

std::wstring ExtensionType(const FileItem &item) {
  if (item.IsDirectory())
    return L"フォルダー";
  const wchar_t *extension = PathFindExtensionW(item.name.c_str());
  if (extension == nullptr || *extension == L'\0')
    return L"ファイル";
  std::wstring value = extension + 1;
  std::transform(value.begin(), value.end(), value.begin(), towupper);
  return value + L" ファイル";
}

std::wstring LeafName(const std::wstring &path) {
  std::filesystem::path value(path);
  if (!value.filename().empty())
    return value.filename().wstring();
  return path;
}

bool HasZipExtension(const std::wstring &path) {
  const wchar_t *extension = PathFindExtensionW(path.c_str());
  return extension != nullptr && _wcsicmp(extension, L".zip") == 0;
}

std::wstring PickFolder(HWND owner, const wchar_t *title) {
  IFileOpenDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  FILEOPENDIALOGOPTIONS options{};
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                     FOS_PATHMUSTEXIST);
  dialog->SetTitle(title);
  std::wstring path;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR rawPath = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        path = rawPath;
        CoTaskMemFree(rawPath);
      }
      item->Release();
    }
  }
  dialog->Release();
  return path;
}

} // namespace

App::App(HINSTANCE instance)
    : instance_(instance),
      settingsStore_(ExecutablePath().parent_path() / L"simplefiler.json") {}

App::~App() {
  for (Pane &pane : panes_) {
    if (pane.worker)
      pane.worker->request_stop();
    for (Pane::RetiredWorker &retired : pane.retiredWorkers)
      retired.thread->request_stop();
  }
  if (accelerators_ != nullptr)
    DestroyAcceleratorTable(accelerators_);
}

int App::Run(int showCommand, const std::wstring &initialPath) {
  if (!RegisterClasses() || !CreateMainWindow(showCommand))
    return 1;
  InitializeFromSettings(initialPath);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (accelerators_ == nullptr ||
        !TranslateAcceleratorW(window_, accelerators_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return static_cast<int>(message.wParam);
}

bool App::RegisterClasses() {
  WNDCLASSEXW mainClass{};
  mainClass.cbSize = sizeof(mainClass);
  mainClass.lpfnWndProc = WindowProcedure;
  mainClass.hInstance = instance_;
  mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  mainClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
  if (mainClass.hIcon == nullptr)
    mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  mainClass.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&mainClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  WNDCLASSEXW promptClass{};
  promptClass.cbSize = sizeof(promptClass);
  promptClass.lpfnWndProc = PromptProcedure;
  promptClass.hInstance = instance_;
  promptClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  promptClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  promptClass.lpszClassName = kPromptClass;
  return RegisterClassExW(&promptClass) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool App::CreateMainWindow(int showCommand) {
  const SettingsLoadResult loaded = settingsStore_.Load();
  settings_ = loaded.settings;
  twoPanes_ = settings_.twoPanes;
  sidebarVisible_ = settings_.sidebarVisible;
  splitRatio_ = settings_.splitRatio;

  const int x = settings_.windowX < 0 ? CW_USEDEFAULT : settings_.windowX;
  const int y = settings_.windowY < 0 ? CW_USEDEFAULT : settings_.windowY;
  window_ = CreateWindowExW(0, kWindowClass, L"SimpleFiler",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y,
                            settings_.windowWidth, settings_.windowHeight,
                            nullptr, nullptr, instance_, this);
  if (window_ == nullptr)
    return false;
  CreateControls();
  CreateAccelerators();
  ShowWindow(window_, settings_.maximized ? SW_MAXIMIZE : showCommand);
  UpdateWindow(window_);
  if (!loaded.warning.empty())
    Notify(Utf8ToWide(loaded.warning), true);
  return true;
}

void App::CreateControls() {
  const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const std::array<std::pair<const wchar_t *, int>, 10> buttons{
      {{L"←", IdBack},
       {L"→", IdForward},
       {L"↑", IdUp},
       {L"更新", IdRefresh},
       {L"PC", IdDrives},
       {L"1｜2", IdTogglePanes},
       {L"登録", IdToggleSidebar},
       {L"★追加", IdAddBookmark},
       {L"リンク＋", IdAddLink},
       {L"端末", IdTerminal}}};
  for (std::size_t index = 0; index < buttons.size(); ++index) {
    toolbar_[index] = CreateWindowExW(
        0, L"BUTTON", buttons[index].first,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttons[index].second)),
        instance_, nullptr);
    SendMessageW(toolbar_[index], WM_SETFONT, reinterpret_cast<WPARAM>(font),
                 TRUE);
  }
  searchEdit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
      window_, reinterpret_cast<HMENU>(IdSearchEdit), instance_, nullptr);
  SendMessageW(searchEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"名前を検索"));
  SetWindowSubclass(searchEdit_, EditSubclass, 1, 2);
  toolbar_[10] = CreateWindowExW(
      0, L"BUTTON", L"検索", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10,
      window_, reinterpret_cast<HMENU>(IdSearch), instance_, nullptr);
  SendMessageW(toolbar_[10], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

  sidebar_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL, 0, 0, 10,
      10, window_, reinterpret_cast<HMENU>(IdSidebar), instance_, nullptr);
  SendMessageW(sidebar_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

  CreatePaneControls(0);
  CreatePaneControls(1);
  status_ = CreateWindowExW(0, L"STATIC", L"準備完了",
                            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                            window_, nullptr, instance_, nullptr);
  SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

  SHFILEINFOW shellInfo{};
  HIMAGELIST images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
      L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &shellInfo, sizeof(shellInfo),
      SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
  if (images != nullptr) {
    ListView_SetImageList(panes_[0].list, images, LVSIL_SMALL);
    ListView_SetImageList(panes_[1].list, images, LVSIL_SMALL);
  }

  RECT client{};
  GetClientRect(window_, &client);
  LayoutControls(client.right, client.bottom);
  RebuildSidebar();
}

void App::CreatePaneControls(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  pane.address = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
      window_,
      reinterpret_cast<HMENU>(paneIndex == 0 ? IdLeftAddress : IdRightAddress),
      instance_, nullptr);
  SendMessageW(pane.address, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SetWindowSubclass(pane.address, EditSubclass, 1,
                    static_cast<DWORD_PTR>(paneIndex));

  pane.list = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHOWSELALWAYS | LVS_EDITLABELS,
      0, 0, 10, 10, window_,
      reinterpret_cast<HMENU>(paneIndex == 0 ? IdLeftList : IdRightList),
      instance_, nullptr);
  SendMessageW(pane.list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  ListView_SetExtendedListViewStyle(
      pane.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
  const std::array<std::pair<const wchar_t *, int>, 4> columns{
      {{L"名前", 260}, {L"種類", 120}, {L"サイズ", 100}, {L"更新日時", 140}}};
  for (int column = 0; column < static_cast<int>(columns.size()); ++column) {
    LVCOLUMNW value{};
    value.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    value.pszText = const_cast<wchar_t *>(columns[column].first);
    value.cx = columns[column].second;
    value.iSubItem = column;
    ListView_InsertColumn(pane.list, column, &value);
  }
}

void App::LayoutControls(int width, int height) {
  constexpr int gap = 4;
  int x = gap;
  const std::array<int, 10> buttonWidths{38, 38, 38, 54, 42,
                                         52, 54, 58, 66, 54};
  for (std::size_t index = 0; index < buttonWidths.size(); ++index) {
    MoveWindow(toolbar_[index], x, gap, buttonWidths[index], 30, TRUE);
    x += buttonWidths[index] + gap;
  }
  const int searchButtonWidth = 50;
  const int searchWidth =
      std::max(100, width - x - searchButtonWidth - gap * 3);
  MoveWindow(searchEdit_, x, gap + 2, searchWidth, 26, TRUE);
  MoveWindow(toolbar_[10], x + searchWidth + gap, gap, searchButtonWidth, 30,
             TRUE);

  const int contentTop = kToolbarHeight;
  const int contentBottom = std::max(contentTop, height - kStatusHeight);
  const int contentHeight = contentBottom - contentTop;
  const int sidebarWidth =
      sidebarVisible_ ? std::min(kSidebarWidth, width / 3) : 0;
  ShowWindow(sidebar_, sidebarVisible_ ? SW_SHOW : SW_HIDE);
  if (sidebarVisible_) {
    MoveWindow(sidebar_, gap, contentTop, sidebarWidth - gap, contentHeight,
               TRUE);
  }
  const int paneLeft = sidebarWidth + gap;
  const int paneWidth = std::max(0, width - paneLeft - gap);
  int leftWidth = paneWidth;
  int rightLeft = width;
  int rightWidth = 0;
  if (twoPanes_) {
    leftWidth = static_cast<int>((paneWidth - kSplitterWidth) * splitRatio_);
    rightLeft = paneLeft + leftWidth + kSplitterWidth;
    rightWidth = width - rightLeft - gap;
  }

  MoveWindow(panes_[0].address, paneLeft, contentTop, leftWidth, kAddressHeight,
             TRUE);
  MoveWindow(panes_[0].list, paneLeft, contentTop + kAddressHeight, leftWidth,
             contentHeight - kAddressHeight, TRUE);
  ShowWindow(panes_[1].address, twoPanes_ ? SW_SHOW : SW_HIDE);
  ShowWindow(panes_[1].list, twoPanes_ ? SW_SHOW : SW_HIDE);
  if (twoPanes_) {
    MoveWindow(panes_[1].address, rightLeft, contentTop, rightWidth,
               kAddressHeight, TRUE);
    MoveWindow(panes_[1].list, rightLeft, contentTop + kAddressHeight,
               rightWidth, contentHeight - kAddressHeight, TRUE);
  }
  MoveWindow(status_, gap, height - kStatusHeight + 3,
             std::max(0, width - gap * 2), kStatusHeight - 3, TRUE);
}

void App::CreateAccelerators() {
  const ACCEL values[] = {{FVIRTKEY | FCONTROL, 'C', IdCopy},
                          {FVIRTKEY | FCONTROL, 'X', IdCut},
                          {FVIRTKEY | FCONTROL, 'V', IdPaste},
                          {FVIRTKEY, VK_DELETE, IdDelete},
                          {FVIRTKEY | FSHIFT, VK_DELETE, IdPermanentDelete},
                          {FVIRTKEY, VK_F2, IdRename},
                          {FVIRTKEY | FCONTROL, 'R', IdRefresh},
                          {FVIRTKEY | FCONTROL, 'L', IdFocusAddress},
                          {FVIRTKEY | FCONTROL, 'F', IdFocusSearch},
                          {FVIRTKEY, VK_F6, IdSwitchPane},
                          {FVIRTKEY | FALT, VK_LEFT, IdBack},
                          {FVIRTKEY | FALT, VK_RIGHT, IdForward},
                          {FVIRTKEY | FALT, VK_UP, IdUp}};
  accelerators_ = CreateAcceleratorTableW(const_cast<ACCEL *>(values),
                                          static_cast<int>(std::size(values)));
}

void App::InitializeFromSettings(const std::wstring &initialPath) {
  for (int index = 0; index < 2; ++index) {
    panes_[index].showHidden = settings_.panes[index].showHidden;
    panes_[index].sortColumn = settings_.panes[index].sortColumn;
    panes_[index].sortAscending = settings_.panes[index].sortAscending;
  }
  const std::wstring home = HomeDirectory();
  std::wstring fileToOpen;
  std::wstring left =
      !initialPath.empty() ? initialPath : Utf8ToWide(settings_.panes[0].path);
  std::error_code pathError;
  if (!left.empty() && std::filesystem::is_regular_file(left, pathError)) {
    fileToOpen = left;
    left = std::filesystem::path(left).parent_path().wstring();
  }
  std::wstring right = Utf8ToWide(settings_.panes[1].path);
  if (left.empty() || !IsDirectory(left))
    left = home;
  if (right.empty() || !IsDirectory(right))
    right = home;
  Navigate(0, left);
  Navigate(1, right);
  SetFocus(panes_[0].list);
  if (!fileToOpen.empty() && !OpenPath(window_, fileToOpen))
    Notify(L"指定されたファイルを開けません", true);
}

void App::SaveSettings() {
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  GetWindowPlacement(window_, &placement);
  settings_.windowX = placement.rcNormalPosition.left;
  settings_.windowY = placement.rcNormalPosition.top;
  settings_.windowWidth =
      placement.rcNormalPosition.right - placement.rcNormalPosition.left;
  settings_.windowHeight =
      placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
  settings_.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
  settings_.twoPanes = twoPanes_;
  settings_.sidebarVisible = sidebarVisible_;
  settings_.splitRatio = splitRatio_;
  for (int index = 0; index < 2; ++index) {
    settings_.panes[index].path = WideToUtf8(panes_[index].path);
    settings_.panes[index].sortColumn = panes_[index].sortColumn;
    settings_.panes[index].sortAscending = panes_[index].sortAscending;
    settings_.panes[index].showHidden = panes_[index].showHidden;
  }
  std::string error;
  if (!settingsStore_.Save(settings_, &error))
    Notify(Utf8ToWide(error), true);
}

void App::Navigate(int paneIndex, const std::wstring &inputPath,
                   bool addHistory) {
  if (paneIndex < 0 || paneIndex > 1)
    return;
  if (inputPath.empty()) {
    ShowDrives(paneIndex, addHistory);
    return;
  }
  std::error_code error;
  std::filesystem::path path(inputPath);
  if (std::filesystem::is_regular_file(path, error)) {
    if (!OpenPath(window_, path.wstring()))
      Notify(L"ファイルを開けません", true);
    return;
  }
  if (!IsDirectory(path.wstring())) {
    Notify(L"フォルダーを開けません: " + path.wstring(), true);
    return;
  }
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (!error)
    path = absolute.lexically_normal();

  Pane &pane = panes_[paneIndex];
  RetireWorker(pane);
  pane.path = path.wstring();
  pane.searchMode = false;
  pane.searchRoot.clear();
  pane.searchQuery.clear();
  pane.busy = true;
  pane.driveView = false;
  pane.items.clear();
  ++pane.generation;
  ListView_SetItemCountEx(pane.list, 0, LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, pane.path.c_str());
  if (paneIndex == activePane_)
    SetWindowTextW(toolbar_[10], L"検索");
  if (addHistory) {
    if (!pane.history.empty() && pane.historyIndex + 1 < pane.history.size()) {
      pane.history.erase(pane.history.begin() +
                             static_cast<std::ptrdiff_t>(pane.historyIndex + 1),
                         pane.history.end());
    }
    if (pane.history.empty() || pane.history.back() != pane.path) {
      pane.history.push_back(pane.path);
      pane.historyIndex = pane.history.size() - 1;
    }
  }
  const std::uint64_t generation = pane.generation;
  const bool showHidden = pane.showHidden;
  pane.worker = std::make_unique<std::jthread>(
      [target = window_, paneIndex, generation, path = pane.path,
       showHidden](std::stop_token token) {
        EnumerateDirectory(target, paneIndex, generation, path, showHidden,
                           token);
      });
  Notify(L"読み込み中: " + pane.path);
}

void App::ShowDrives(int paneIndex, bool addHistory) {
  Pane &pane = panes_[paneIndex];
  RetireWorker(pane);
  pane.path.clear();
  pane.searchMode = false;
  pane.searchQuery.clear();
  pane.busy = false;
  pane.driveView = true;
  pane.items.clear();
  ++pane.generation;
  std::array<wchar_t, 512> drives{};
  const DWORD length =
      GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data());
  for (const wchar_t *drive = drives.data(); length != 0 && *drive != L'\0';
       drive += wcslen(drive) + 1) {
    FileItem item;
    item.path = drive;
    item.name = drive;
    item.attributes = FILE_ATTRIBUTE_DIRECTORY;
    SHFILEINFOW info{};
    SHGetFileInfoW(drive, 0, &info, sizeof(info),
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    item.iconIndex = info.iIcon;
    pane.items.push_back(std::move(item));
  }
  ListView_SetItemCountEx(pane.list, static_cast<int>(pane.items.size()),
                          LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, L"PC");
  if (addHistory) {
    if (!pane.history.empty() && pane.historyIndex + 1 < pane.history.size()) {
      pane.history.erase(pane.history.begin() +
                             static_cast<std::ptrdiff_t>(pane.historyIndex + 1),
                         pane.history.end());
    }
    if (!pane.history.empty() && pane.history.back().empty()) {
      pane.historyIndex = pane.history.size() - 1;
      Notify(std::format(L"{} ドライブ", pane.items.size()));
      return;
    }
    pane.history.push_back({});
    pane.historyIndex = pane.history.size() - 1;
  }
  Notify(std::format(L"{} ドライブ", pane.items.size()));
}

void App::NavigateHistory(int delta) {
  Pane &pane = panes_[activePane_];
  if (pane.history.empty())
    return;
  const auto next = static_cast<std::ptrdiff_t>(pane.historyIndex) + delta;
  if (next < 0 || next >= static_cast<std::ptrdiff_t>(pane.history.size()))
    return;
  pane.historyIndex = static_cast<std::size_t>(next);
  const std::wstring path = pane.history[pane.historyIndex];
  if (path.empty())
    ShowDrives(activePane_, false);
  else
    Navigate(activePane_, path, false);
}

void App::NavigateUp() {
  Pane &pane = panes_[activePane_];
  if (pane.driveView || pane.path.empty())
    return;
  const std::filesystem::path parent =
      std::filesystem::path(pane.path).parent_path();
  if (parent == pane.path || parent.empty())
    ShowDrives(activePane_);
  else
    Navigate(activePane_, parent.wstring());
}

void App::RefreshPane(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  if (pane.driveView)
    ShowDrives(paneIndex, false);
  else if (pane.searchMode) {
    const int previousActive = activePane_;
    activePane_ = paneIndex;
    StartSearch(pane.searchQuery);
    activePane_ = previousActive;
  } else
    Navigate(paneIndex, pane.path, false);
}

void App::StartSearch(const std::wstring &query) {
  Pane &pane = panes_[activePane_];
  if (query.empty()) {
    if (pane.searchMode)
      Navigate(activePane_, pane.searchRoot, false);
    return;
  }
  if (pane.driveView || pane.path.empty()) {
    Notify(L"検索するフォルダーを開いてください", true);
    return;
  }
  RetireWorker(pane);
  pane.searchRoot = pane.searchMode ? pane.searchRoot : pane.path;
  pane.searchQuery = query;
  pane.searchMode = true;
  pane.busy = true;
  pane.items.clear();
  ++pane.generation;
  ListView_SetItemCountEx(pane.list, 0, LVSICF_NOSCROLL);
  SetWindowTextW(pane.address, (L"検索: " + pane.searchRoot).c_str());
  const std::uint64_t generation = pane.generation;
  const bool showHidden = pane.showHidden;
  pane.worker = std::make_unique<std::jthread>(
      [target = window_, paneIndex = activePane_, generation,
       root = pane.searchRoot, query, showHidden](std::stop_token token) {
        SearchDirectory(target, paneIndex, generation, root, query, showHidden,
                        token);
      });
  SetWindowTextW(toolbar_[10], L"中止");
  Notify(L"検索中: " + query);
}

void App::SortPane(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  const int column = pane.sortColumn;
  const bool ascending = pane.sortAscending;
  std::stable_sort(
      pane.items.begin(), pane.items.end(),
      [column, ascending](const FileItem &left, const FileItem &right) {
        int comparison = 0;
        if (left.IsDirectory() != right.IsDirectory()) {
          comparison = left.IsDirectory() ? -1 : 1;
        } else if (column == 1) {
          comparison = _wcsicmp(ExtensionType(left).c_str(),
                                ExtensionType(right).c_str());
        } else if (column == 2) {
          comparison = left.size < right.size   ? -1
                       : left.size > right.size ? 1
                                                : 0;
        } else if (column == 3) {
          comparison = CompareFileTime(&left.modified, &right.modified);
        } else {
          comparison = _wcsicmp(left.name.c_str(), right.name.c_str());
        }
        return ascending ? comparison < 0 : comparison > 0;
      });
  ListView_RedrawItems(pane.list, 0, static_cast<int>(pane.items.size()) - 1);
}

void App::RetireWorker(Pane &pane) {
  if (!pane.worker)
    return;
  pane.worker->request_stop();
  pane.retiredWorkers.push_back(
      Pane::RetiredWorker{pane.generation, std::move(pane.worker)});
}

void App::FinishWorker(Pane &pane, std::uint64_t generation) {
  if (generation == pane.generation && pane.worker) {
    pane.worker->join();
    pane.worker.reset();
    return;
  }
  const auto found =
      std::find_if(pane.retiredWorkers.begin(), pane.retiredWorkers.end(),
                   [generation](const Pane::RetiredWorker &worker) {
                     return worker.generation == generation;
                   });
  if (found != pane.retiredWorkers.end()) {
    found->thread->join();
    pane.retiredWorkers.erase(found);
  }
}

std::vector<std::wstring> App::SelectedPaths() const {
  const Pane &pane = panes_[activePane_];
  std::vector<std::wstring> paths;
  int index = -1;
  while ((index = ListView_GetNextItem(pane.list, index, LVNI_SELECTED)) >= 0) {
    if (index < static_cast<int>(pane.items.size()))
      paths.push_back(pane.items[index].path);
  }
  return paths;
}

void App::OpenSelected() {
  Pane &pane = panes_[activePane_];
  const int index = ListView_GetNextItem(pane.list, -1, LVNI_SELECTED);
  if (index < 0 || index >= static_cast<int>(pane.items.size()))
    return;
  const FileItem &item = pane.items[index];
  if (item.IsDirectory())
    Navigate(activePane_, item.path);
  else if (!OpenPath(window_, item.path))
    Notify(L"ファイルを開けません", true);
}

void App::BeginRename() {
  Pane &pane = panes_[activePane_];
  const int index = ListView_GetNextItem(pane.list, -1, LVNI_SELECTED);
  if (index >= 0)
    ListView_EditLabel(pane.list, index);
}

void App::CopySelection(bool cut) {
  const auto paths = SelectedPaths();
  if (PutFilesOnClipboard(window_, paths, cut)) {
    Notify(cut ? L"切り取りました" : L"コピーしました");
  }
}

void App::Paste() {
  const Pane &pane = panes_[activePane_];
  if (!pane.path.empty()) {
    PasteFilesAsync(window_, pane.searchMode ? pane.searchRoot : pane.path);
    Notify(L"ファイル操作を開始しました");
  }
}

void App::DeleteSelection(bool permanent) {
  auto paths = SelectedPaths();
  if (paths.empty())
    return;
  if (permanent &&
      MessageBoxW(window_,
                  L"選択項目を完全に削除します。元に戻せません。続行しますか？",
                  L"SimpleFiler",
                  MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
    return;
  }
  DeleteFilesAsync(window_, std::move(paths), permanent);
  Notify(L"削除処理を開始しました");
}

void App::NewFolder() {
  const Pane &pane = panes_[activePane_];
  if (pane.path.empty())
    return;
  std::wstring name =
      PromptText(L"新しいフォルダー", L"フォルダー名", L"新しいフォルダー");
  if (name.empty())
    return;
  if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
    Notify(L"フォルダー名に使用できない文字があります", true);
    return;
  }
  CreateFolderAsync(window_, pane.searchMode ? pane.searchRoot : pane.path,
                    name);
  Notify(L"フォルダーを作成中です");
}

void App::ShowSelectedProperties() {
  const auto paths = SelectedPaths();
  if (!paths.empty())
    ShowProperties(window_, paths.front());
}

void App::AddCurrentBookmark() {
  const Pane &pane = panes_[activePane_];
  const std::wstring path = pane.searchMode ? pane.searchRoot : pane.path;
  if (path.empty())
    return;
  const std::wstring name =
      PromptText(L"ブックマーク追加", L"表示名", LeafName(path));
  if (name.empty())
    return;
  settings_.bookmarks.push_back(
      {MakeStableId(), WideToUtf8(name), WideToUtf8(path)});
  RebuildSidebar();
  SaveSettings();
}

void App::AddLink(bool application) {
  std::array<wchar_t, 32768> file{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.lpstrTitle =
      application ? L"アプリリンクを登録" : L"ファイルリンクを登録";
  dialog.lpstrFilter =
      application ? L"アプリケーション (*.exe)\0*.exe\0すべてのファイル\0*.*\0"
                  : L"すべてのファイル\0*.*\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetOpenFileNameW(&dialog))
    return;
  std::wstring name =
      PromptText(L"リンク追加", L"表示名", LeafName(file.data()));
  if (name.empty())
    return;
  std::wstring arguments;
  std::wstring workingDirectory;
  if (application) {
    arguments = PromptText(L"アプリリンク", L"引数（省略可）");
    workingDirectory =
        PromptText(L"アプリリンク", L"作業フォルダー（省略可）",
                   std::filesystem::path(file.data()).parent_path().wstring());
  }
  settings_.links.push_back(
      {MakeStableId(), application ? LinkType::Application : LinkType::File,
       WideToUtf8(name), WideToUtf8(file.data()), WideToUtf8(arguments),
       WideToUtf8(workingDirectory)});
  RebuildSidebar();
  SaveSettings();
}

void App::RebuildSidebar() {
  SendMessageW(sidebar_, LB_RESETCONTENT, 0, 0);
  sidebarMap_.clear();
  for (std::size_t index = 0; index < settings_.bookmarks.size(); ++index) {
    const Bookmark &bookmark = settings_.bookmarks[index];
    const std::wstring path = Utf8ToWide(bookmark.path);
    const std::wstring prefix = IsDirectory(path) ? L"★ " : L"⚠ ";
    SendMessageW(
        sidebar_, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>((prefix + Utf8ToWide(bookmark.name)).c_str()));
    sidebarMap_.emplace_back(true, index);
  }
  for (std::size_t index = 0; index < settings_.links.size(); ++index) {
    const RegisteredLink &link = settings_.links[index];
    const std::wstring target = Utf8ToWide(link.target);
    const std::wstring prefix =
        GetFileAttributesW(ToExtendedPath(target).c_str()) !=
                INVALID_FILE_ATTRIBUTES
            ? L"↗ "
            : L"⚠ ";
    SendMessageW(
        sidebar_, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>((prefix + Utf8ToWide(link.name)).c_str()));
    sidebarMap_.emplace_back(false, index);
  }
}

void App::ActivateSidebarItem(bool administrator) {
  const int selected =
      static_cast<int>(SendMessageW(sidebar_, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const auto [bookmark, index] = sidebarMap_[selected];
  if (bookmark) {
    Navigate(activePane_, Utf8ToWide(settings_.bookmarks[index].path));
    return;
  }
  const RegisteredLink &link = settings_.links[index];
  const std::wstring target = Utf8ToWide(link.target);
  if (GetFileAttributesW(ToExtendedPath(target).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    Notify(L"登録先が見つかりません: " + target, true);
    return;
  }
  if (!OpenPath(window_, target, Utf8ToWide(link.arguments),
                Utf8ToWide(link.workingDirectory),
                administrator && link.type == LinkType::Application)) {
    const DWORD error = GetLastError();
    if (error != ERROR_CANCELLED)
      Notify(L"リンク先を開けません: " + WindowsErrorMessage(error), true);
  }
}

void App::RemoveSidebarItem() {
  const int selected =
      static_cast<int>(SendMessageW(sidebar_, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const auto [bookmark, index] = sidebarMap_[selected];
  if (bookmark)
    settings_.bookmarks.erase(settings_.bookmarks.begin() + index);
  else
    settings_.links.erase(settings_.links.begin() + index);
  RebuildSidebar();
  SaveSettings();
}

void App::ShowTerminalMenu(HWND sourceButton) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IdCmd, L"CMDをここで開く");
  AppendMenuW(menu, MF_STRING, IdCmdAdmin, L"管理者としてCMDをここで開く");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdPowerShell, L"PowerShellをここで開く");
  AppendMenuW(menu, MF_STRING, IdPowerShellAdmin,
              L"管理者としてPowerShellをここで開く");
  const Pane &pane = panes_[activePane_];
  if (pane.path.empty()) {
    EnableMenuItem(menu, IdCmd, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, IdCmdAdmin, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, IdPowerShell, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, IdPowerShellAdmin, MF_BYCOMMAND | MF_GRAYED);
  }
  RECT rectangle{};
  GetWindowRect(sourceButton, &rectangle);
  TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rectangle.left,
                 rectangle.bottom, 0, window_, nullptr);
  DestroyMenu(menu);
}

void App::LaunchSelectedTerminal(TerminalKind kind, bool administrator) {
  const Pane &pane = panes_[activePane_];
  const std::wstring directory = pane.searchMode ? pane.searchRoot : pane.path;
  const TerminalLaunchResult result =
      LaunchTerminal(window_, directory, kind, administrator);
  if (result.launched || result.cancelled) {
    if (result.cancelled)
      Notify(L"管理者起動をキャンセルしました");
    return;
  }
  Notify(L"端末を起動できません: " + WindowsErrorMessage(result.error), true);
}

void App::ShowLinkMenu(HWND sourceButton) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IdAddFileLink, L"ファイルリンクを追加");
  AppendMenuW(menu, MF_STRING, IdAddAppLink, L"アプリリンクを追加");
  RECT rectangle{};
  GetWindowRect(sourceButton, &rectangle);
  TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rectangle.left,
                 rectangle.bottom, 0, window_, nullptr);
  DestroyMenu(menu);
}

void App::ShowFileMenu(POINT point) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IdOpen, L"開く");
  AppendMenuW(menu, MF_STRING, IdCopy, L"コピー");
  AppendMenuW(menu, MF_STRING, IdCut, L"切り取り");
  AppendMenuW(menu, MF_STRING, IdPaste, L"貼り付け");
  AppendMenuW(menu, MF_STRING, IdNewFolder, L"新しいフォルダー");
  AppendMenuW(menu, MF_STRING, IdRename, L"名前の変更");
  AppendMenuW(menu, MF_STRING, IdDelete, L"削除");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdZipCreate, L"ZIPを作成");
  AppendMenuW(menu, MF_STRING, IdZipExtract, L"ZIPを展開");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu,
              MF_STRING | (panes_[activePane_].showHidden ? MF_CHECKED : 0),
              IdShowHidden, L"隠しファイルを表示");
  AppendMenuW(menu, MF_STRING, IdProperties, L"プロパティ");
  const auto paths = SelectedPaths();
  if (paths.empty()) {
    for (UINT id : {IdOpen, IdCopy, IdCut, IdRename, IdDelete, IdZipCreate,
                    IdZipExtract, IdProperties}) {
      EnableMenuItem(menu, id, MF_BYCOMMAND | MF_GRAYED);
    }
  } else if (paths.size() != 1 || !HasZipExtension(paths.front())) {
    EnableMenuItem(menu, IdZipExtract, MF_BYCOMMAND | MF_GRAYED);
  }
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
  DestroyMenu(menu);
}

void App::CreateZipFromSelection() {
  const auto paths = SelectedPaths();
  if (paths.empty())
    return;
  std::array<wchar_t, 32768> output{};
  wcscpy_s(output.data(), output.size(), L"archive.zip");
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFile = output.data();
  dialog.nMaxFile = static_cast<DWORD>(output.size());
  dialog.lpstrFilter = L"ZIPアーカイブ (*.zip)\0*.zip\0";
  dialog.lpstrDefExt = L"zip";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&dialog)) {
    std::error_code absoluteError;
    const std::filesystem::path destination =
        std::filesystem::absolute(output.data(), absoluteError);
    if (absoluteError) {
      Notify(L"ZIP出力先を解決できません", true);
      return;
    }
    const bool overwritesSource = std::any_of(
        paths.begin(), paths.end(), [&destination](const std::wstring &source) {
          std::error_code error;
          return std::filesystem::equivalent(destination, source, error);
        });
    if (overwritesSource) {
      Notify(L"選択したファイル自身をZIP出力先にはできません", true);
      return;
    }
    CreateZipAsync(window_, paths, output.data());
    Notify(L"ZIPを作成中です");
  }
}

void App::ExtractSelectedZip() {
  const auto paths = SelectedPaths();
  if (paths.size() != 1 || !HasZipExtension(paths.front()))
    return;
  const std::wstring destination = PickFolder(window_, L"展開先を選択");
  if (destination.empty())
    return;
  ExtractZipAsync(window_, paths.front(), destination);
  Notify(L"ZIPを展開中です");
}

int App::PaneIndexFromControl(HWND control) const {
  if (control == panes_[0].list || control == panes_[0].address)
    return 0;
  if (control == panes_[1].list || control == panes_[1].address)
    return 1;
  return activePane_;
}

std::wstring App::PromptText(const std::wstring &title,
                             const std::wstring &label,
                             const std::wstring &initial) const {
  PromptState state{label, initial};
  RECT owner{};
  GetWindowRect(window_, &owner);
  HWND prompt =
      CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title.c_str(),
                      WS_POPUP | WS_CAPTION | WS_SYSMENU,
                      owner.left + (owner.right - owner.left - 430) / 2,
                      owner.top + (owner.bottom - owner.top - 140) / 2, 430,
                      140, window_, nullptr, instance_, &state);
  if (prompt == nullptr)
    return {};
  EnableWindow(window_, FALSE);
  ShowWindow(prompt, SW_SHOW);
  MSG message{};
  while (IsWindow(prompt) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(prompt, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  EnableWindow(window_, TRUE);
  SetForegroundWindow(window_);
  return state.accepted ? state.value : std::wstring{};
}

void App::Notify(const std::wstring &message, bool error) {
  SetWindowTextW(status_, (error ? L"⚠ " + message : message).c_str());
}

LRESULT CALLBACK App::WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
  App *app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    app = static_cast<App *>(create->lpCreateParams);
    app->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  return app != nullptr ? app->HandleMessage(message, wParam, lParam)
                        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_GETMINMAXINFO: {
    auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
    limits->ptMinTrackSize.x = 760;
    limits->ptMinTrackSize.y = 480;
    return 0;
  }
  case WM_SIZE:
    LayoutControls(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_LBUTTONDOWN: {
    if (twoPanes_) {
      RECT left{};
      GetWindowRect(panes_[0].list, &left);
      POINT split{left.right + kSplitterWidth / 2, GET_Y_LPARAM(lParam)};
      ScreenToClient(window_, &split);
      if (abs(GET_X_LPARAM(lParam) - split.x) <= kSplitterWidth) {
        draggingSplitter_ = true;
        SetCapture(window_);
      }
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (draggingSplitter_) {
      RECT client{};
      GetClientRect(window_, &client);
      const int sidebarWidth =
          sidebarVisible_
              ? std::min(kSidebarWidth, static_cast<int>(client.right / 3))
              : 0;
      const int available = client.right - sidebarWidth - kSplitterWidth - 8;
      if (available > 0) {
        splitRatio_ = std::clamp(
            static_cast<double>(GET_X_LPARAM(lParam) - sidebarWidth) /
                available,
            0.2, 0.8);
        LayoutControls(client.right, client.bottom);
      }
    }
    return 0;
  case WM_LBUTTONUP:
    if (draggingSplitter_) {
      draggingSplitter_ = false;
      ReleaseCapture();
    }
    return 0;
  case WM_SETCURSOR:
    if (twoPanes_ && LOWORD(lParam) == HTCLIENT) {
      POINT point{};
      GetCursorPos(&point);
      ScreenToClient(window_, &point);
      RECT left{};
      GetWindowRect(panes_[0].list, &left);
      POINT split{left.right, 0};
      ScreenToClient(window_, &split);
      if (abs(point.x - split.x) <= kSplitterWidth) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
      }
    }
    break;
  case WM_COMMAND: {
    const int command = LOWORD(wParam);
    if (HIWORD(wParam) == LBN_DBLCLK && command == IdSidebar) {
      ActivateSidebarItem();
      return 0;
    }
    if (HIWORD(wParam) == EN_SETFOCUS &&
        (command == IdLeftAddress || command == IdRightAddress)) {
      activePane_ = command == IdLeftAddress ? 0 : 1;
    }
    switch (command) {
    case IdBack:
      NavigateHistory(-1);
      break;
    case IdForward:
      NavigateHistory(1);
      break;
    case IdUp:
      NavigateUp();
      break;
    case IdRefresh:
      RefreshPane(activePane_);
      break;
    case IdDrives:
      ShowDrives(activePane_);
      break;
    case IdTogglePanes: {
      twoPanes_ = !twoPanes_;
      if (!twoPanes_ && activePane_ == 1)
        activePane_ = 0;
      RECT client{};
      GetClientRect(window_, &client);
      LayoutControls(client.right, client.bottom);
      break;
    }
    case IdToggleSidebar: {
      sidebarVisible_ = !sidebarVisible_;
      RECT client{};
      GetClientRect(window_, &client);
      LayoutControls(client.right, client.bottom);
      break;
    }
    case IdAddBookmark:
      AddCurrentBookmark();
      break;
    case IdAddLink:
      ShowLinkMenu(toolbar_[8]);
      break;
    case IdAddFileLink:
      AddLink(false);
      break;
    case IdAddAppLink:
      AddLink(true);
      break;
    case IdTerminal:
      ShowTerminalMenu(toolbar_[9]);
      break;
    case IdCmd:
      LaunchSelectedTerminal(TerminalKind::CommandPrompt, false);
      break;
    case IdCmdAdmin:
      LaunchSelectedTerminal(TerminalKind::CommandPrompt, true);
      break;
    case IdPowerShell:
      LaunchSelectedTerminal(TerminalKind::PowerShell, false);
      break;
    case IdPowerShellAdmin:
      LaunchSelectedTerminal(TerminalKind::PowerShell, true);
      break;
    case IdSearch:
      if (panes_[activePane_].searchMode && panes_[activePane_].busy) {
        RetireWorker(panes_[activePane_]);
        ++panes_[activePane_].generation;
        panes_[activePane_].busy = false;
        SetWindowTextW(toolbar_[10], L"検索");
        Notify(L"検索を中止しました");
      } else {
        StartSearch(GetWindowTextString(searchEdit_));
      }
      break;
    case IdCopy:
      CopySelection(false);
      break;
    case IdCut:
      CopySelection(true);
      break;
    case IdPaste:
      Paste();
      break;
    case IdDelete:
      DeleteSelection(false);
      break;
    case IdPermanentDelete:
      DeleteSelection(true);
      break;
    case IdRename:
      BeginRename();
      break;
    case IdNewFolder:
      NewFolder();
      break;
    case IdProperties:
      ShowSelectedProperties();
      break;
    case IdOpen:
      OpenSelected();
      break;
    case IdZipCreate:
      CreateZipFromSelection();
      break;
    case IdZipExtract:
      ExtractSelectedZip();
      break;
    case IdShowHidden:
      panes_[activePane_].showHidden = !panes_[activePane_].showHidden;
      RefreshPane(activePane_);
      break;
    case IdFocusAddress:
      SetFocus(panes_[activePane_].address);
      SendMessageW(panes_[activePane_].address, EM_SETSEL, 0, -1);
      break;
    case IdFocusSearch:
      SetFocus(searchEdit_);
      SendMessageW(searchEdit_, EM_SETSEL, 0, -1);
      break;
    case IdSwitchPane:
      if (twoPanes_) {
        activePane_ = 1 - activePane_;
        SetWindowTextW(searchEdit_,
                       panes_[activePane_].searchMode
                           ? panes_[activePane_].searchQuery.c_str()
                           : L"");
        SetWindowTextW(toolbar_[10], panes_[activePane_].searchMode &&
                                             panes_[activePane_].busy
                                         ? L"中止"
                                         : L"検索");
        SetFocus(panes_[activePane_].list);
      }
      break;
    case IdRemoveSidebar:
      RemoveSidebarItem();
      break;
    case IdOpenSidebar:
      ActivateSidebarItem(false);
      break;
    case IdOpenSidebarAdmin:
      ActivateSidebarItem(true);
      break;
    }
    return 0;
  }
  case WM_NOTIFY: {
    const auto *header = reinterpret_cast<NMHDR *>(lParam);
    const int paneIndex = PaneIndexFromControl(header->hwndFrom);
    if (header->hwndFrom == panes_[paneIndex].list) {
      if (header->code == NM_SETFOCUS) {
        activePane_ = paneIndex;
        SetWindowTextW(searchEdit_,
                       panes_[activePane_].searchMode
                           ? panes_[activePane_].searchQuery.c_str()
                           : L"");
        SetWindowTextW(toolbar_[10], panes_[activePane_].searchMode &&
                                             panes_[activePane_].busy
                                         ? L"中止"
                                         : L"検索");
        Notify(paneIndex == 0 ? L"左ペイン" : L"右ペイン");
      } else if (header->code == NM_DBLCLK) {
        activePane_ = paneIndex;
        OpenSelected();
      } else if (header->code == NM_RCLICK) {
        activePane_ = paneIndex;
        POINT point{};
        GetCursorPos(&point);
        ShowFileMenu(point);
      } else if (header->code == LVN_KEYDOWN) {
        const auto *key = reinterpret_cast<NMLVKEYDOWN *>(lParam);
        if (key->wVKey == VK_RETURN) {
          activePane_ = paneIndex;
          OpenSelected();
        }
      } else if (header->code == LVN_COLUMNCLICK) {
        auto *click = reinterpret_cast<NMLISTVIEW *>(lParam);
        Pane &pane = panes_[paneIndex];
        if (pane.sortColumn == click->iSubItem)
          pane.sortAscending = !pane.sortAscending;
        else {
          pane.sortColumn = click->iSubItem;
          pane.sortAscending = true;
        }
        SortPane(paneIndex);
      } else if (header->code == LVN_GETDISPINFOW) {
        auto *display = reinterpret_cast<NMLVDISPINFOW *>(lParam);
        const int itemIndex = display->item.iItem;
        Pane &pane = panes_[paneIndex];
        if (itemIndex >= 0 && itemIndex < static_cast<int>(pane.items.size())) {
          const FileItem &item = pane.items[itemIndex];
          if ((display->item.mask & LVIF_IMAGE) != 0) {
            display->item.iImage = item.iconIndex;
          }
          if ((display->item.mask & LVIF_TEXT) != 0) {
            thread_local std::wstring text;
            switch (display->item.iSubItem) {
            case 0:
              text = item.name;
              break;
            case 1:
              text = ExtensionType(item);
              break;
            case 2:
              text = item.IsDirectory() ? L"" : FormatFileSize(item.size);
              break;
            case 3:
              text = FormatFileTime(item.modified);
              break;
            default:
              text.clear();
              break;
            }
            display->item.pszText = text.data();
          }
        }
      } else if (header->code == LVN_ENDLABELEDITW) {
        const auto *edit = reinterpret_cast<NMLVDISPINFOW *>(lParam);
        if (edit->item.pszText != nullptr && edit->item.iItem >= 0 &&
            edit->item.iItem <
                static_cast<int>(panes_[paneIndex].items.size())) {
          Pane &pane = panes_[paneIndex];
          const std::wstring name = edit->item.pszText;
          if (!name.empty() &&
              name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos) {
            RenameFileAsync(window_, pane.items[edit->item.iItem].path, name);
            return TRUE;
          }
        }
      }
    }
    return 0;
  }
  case WM_CONTEXTMENU:
    if (reinterpret_cast<HWND>(wParam) == sidebar_) {
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, IdOpenSidebar, L"開く");
      const int selected =
          static_cast<int>(SendMessageW(sidebar_, LB_GETCURSEL, 0, 0));
      bool canRunAsAdmin = false;
      if (selected >= 0 && selected < static_cast<int>(sidebarMap_.size())) {
        const auto [bookmark, index] = sidebarMap_[selected];
        canRunAsAdmin =
            !bookmark && settings_.links[index].type == LinkType::Application;
      }
      AppendMenuW(menu, MF_STRING | (canRunAsAdmin ? 0 : MF_GRAYED),
                  IdOpenSidebarAdmin, L"管理者として実行");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IdRemoveSidebar, L"登録を削除");
      TrackPopupMenu(menu, TPM_RIGHTBUTTON, GET_X_LPARAM(lParam),
                     GET_Y_LPARAM(lParam), 0, window_, nullptr);
      DestroyMenu(menu);
      return 0;
    }
    break;
  case kMessageNavigateAddress: {
    const int paneIndex = static_cast<int>(wParam);
    activePane_ = paneIndex;
    const std::wstring text = GetWindowTextString(panes_[paneIndex].address);
    if (_wcsicmp(text.c_str(), L"PC") == 0)
      ShowDrives(paneIndex);
    else
      Navigate(paneIndex, text);
    return 0;
  }
  case kMessageSearch:
    StartSearch(GetWindowTextString(searchEdit_));
    return 0;
  case kMessageEnumerationBatch: {
    std::unique_ptr<EnumerationBatch> batch(
        reinterpret_cast<EnumerationBatch *>(lParam));
    if (batch->pane >= 0 && batch->pane < 2 &&
        batch->generation == panes_[batch->pane].generation) {
      Pane &pane = panes_[batch->pane];
      if (batch->replace)
        pane.items.clear();
      pane.items.insert(pane.items.end(),
                        std::make_move_iterator(batch->items.begin()),
                        std::make_move_iterator(batch->items.end()));
      ListView_SetItemCountEx(pane.list, static_cast<int>(pane.items.size()),
                              LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
      InvalidateRect(pane.list, nullptr, FALSE);
    }
    return 0;
  }
  case kMessageEnumerationDone: {
    std::unique_ptr<EnumerationDone> done(
        reinterpret_cast<EnumerationDone *>(lParam));
    if (done->pane >= 0 && done->pane < 2)
      FinishWorker(panes_[done->pane], done->generation);
    if (done->pane >= 0 && done->pane < 2 &&
        done->generation == panes_[done->pane].generation) {
      panes_[done->pane].busy = false;
      if (done->pane == activePane_)
        SetWindowTextW(toolbar_[10], L"検索");
      SortPane(done->pane);
      if (done->error == ERROR_SUCCESS) {
        Notify(std::format(L"{} 項目", done->itemCount));
      } else if (done->error != ERROR_CANCELLED) {
        Notify(L"読み込みに失敗しました: " + WindowsErrorMessage(done->error),
               true);
      }
    }
    return 0;
  }
  case kMessageOperationDone: {
    std::unique_ptr<OperationResult> result(
        reinterpret_cast<OperationResult *>(lParam));
    if (result->aborted)
      Notify(L"ファイル操作をキャンセルしました");
    else if (FAILED(result->result)) {
      Notify(L"ファイル操作に失敗しました: " +
                 WindowsErrorMessage(HRESULT_CODE(result->result)),
             true);
    } else {
      Notify(L"ファイル操作が完了しました");
      RefreshPane(0);
      RefreshPane(1);
    }
    return 0;
  }
  case kMessageZipDone: {
    std::unique_ptr<ZipResult> result(reinterpret_cast<ZipResult *>(lParam));
    if (result->success) {
      Notify(result->message);
      RefreshPane(activePane_);
    } else {
      Notify(result->message, true);
    }
    return 0;
  }
  case WM_CLOSE:
    SaveSettings();
    DestroyWindow(window_);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace sf::win
