#include "win/App.h"

#include "core/AppArguments.h"
#include "core/PortablePath.h"
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
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numeric>
#include <tuple>
#include <unordered_set>

namespace sf::win {
namespace {

constexpr wchar_t kWindowClass[] = L"SimpleFiler.MainWindow";
constexpr wchar_t kPromptClass[] = L"SimpleFiler.PromptWindow";
constexpr wchar_t kCommandCueText[] =
    L"コマンド / 検索  (ff フォルダー・aa アプリ・cmd 端末)";
constexpr int kToolbarHeight = 50;
constexpr int kAddressHeight = 22;
constexpr int kStatusHeight = 30;
constexpr int kSidebarWidth = 165;
constexpr int kSplitterWidth = 10;
constexpr COLORREF kBackgroundColor = RGB(244, 247, 251);
constexpr COLORREF kSurfaceColor = RGB(255, 255, 255);
constexpr COLORREF kSidebarColor = RGB(248, 250, 252);
constexpr COLORREF kActivePaneColor = RGB(247, 250, 255);
constexpr COLORREF kBorderColor = RGB(218, 225, 235);
constexpr COLORREF kTextColor = RGB(30, 41, 59);
constexpr COLORREF kMutedTextColor = RGB(100, 116, 139);
constexpr COLORREF kAccentColor = RGB(37, 99, 235);
constexpr COLORREF kAccentPressedColor = RGB(29, 78, 216);
constexpr COLORREF kAccentSoftColor = RGB(219, 234, 254);

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
  IdPowerShellAdmin,
  IdCopyToOther,
  IdMoveToOther,
  IdCommandSuggestions,
  IdEditSidebar,
  IdAddFolderLink,
  IdMoveSidebarUp,
  IdMoveSidebarDown,
  IdPromptEdit = 400,
  IdRegisteredAppBase = 1000,
  IdShellMenuFirst = 2000,
  IdShellMenuLast = 2999
};

struct PromptState final {
  std::wstring label;
  std::wstring value;
  bool accepted = false;
  HWND edit = nullptr;
  HFONT font = nullptr;
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
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
    const auto scale = [state](int value) {
      return MulDiv(value, static_cast<int>(state->dpi),
                    USER_DEFAULT_SCREEN_DPI);
    };
    const HFONT font =
        state->font != nullptr
            ? state->font
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND label = CreateWindowExW(0, L"STATIC", state->label.c_str(),
                                 WS_CHILD | WS_VISIBLE, scale(12), scale(12),
                                 scale(396), scale(20), window, nullptr, nullptr,
                                 nullptr);
    state->edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, scale(12),
        scale(36), scale(396), scale(25), window,
        reinterpret_cast<HMENU>(IdPromptEdit), nullptr, nullptr);
    HWND ok = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, scale(236),
        scale(72), scale(82), scale(27), window,
        reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    HWND cancel = CreateWindowExW(
        0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        scale(326), scale(72), scale(82), scale(27), window,
        reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
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
  if (message == WM_PAINT && reference == 2) {
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (GetWindowTextLengthW(window) == 0) {
      HDC dc = GetDC(window);
      if (dc != nullptr) {
        RECT cueRect{};
        GetClientRect(window, &cueRect);
        const UINT dpi = GetDpiForWindow(window);
        cueRect.left +=
            MulDiv(8, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        cueRect.right -=
            MulDiv(6, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        const HFONT font =
            reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        const HGDIOBJ oldFont =
            font != nullptr ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedTextColor);
        DrawTextW(dc, kCommandCueText, -1, &cueRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
        if (oldFont != nullptr)
          SelectObject(dc, oldFont);
        ReleaseDC(window, dc);
      }
    }
    return result;
  }
  if (message == WM_KEYDOWN) {
    if (reference == 2) {
      if (wParam == L'N' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        SendMessageW(GetParent(window), kMessageCommandNew, 0, 0);
        return 0;
      }
      if (wParam == VK_RETURN) {
        const WPARAM modifiers =
            ((GetKeyState(VK_CONTROL) & 0x8000) != 0 ? 1U : 0U) |
            ((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 2U : 0U);
        SendMessageW(GetParent(window), kMessageCommandAccept, modifiers, 0);
        return 0;
      }
      if (wParam == VK_UP || wParam == VK_DOWN) {
        SendMessageW(GetParent(window), kMessageCommandMove, 0,
                     wParam == VK_UP ? -1 : 1);
        return 0;
      }
      if (wParam == VK_ESCAPE) {
        SendMessageW(GetParent(window), kMessageCommandDismiss, 0, 0);
        return 0;
      }
    } else if (wParam == VK_RETURN) {
      PostMessageW(GetParent(window), kMessageNavigateAddress,
                   static_cast<WPARAM>(reference), 0);
      return 0;
    }
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SuggestionSubclass(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
  if (message == WM_KEYDOWN) {
    if (wParam == L'N' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
      SendMessageW(GetParent(window), kMessageCommandNew, 0, 0);
      return 0;
    }
    if (wParam == VK_RETURN) {
      const WPARAM modifiers =
          ((GetKeyState(VK_CONTROL) & 0x8000) != 0 ? 1U : 0U) |
          ((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 2U : 0U);
      SendMessageW(GetParent(window), kMessageCommandAccept, modifiers, 0);
      return 0;
    }
    if (wParam == VK_ESCAPE) {
      SendMessageW(GetParent(window), kMessageCommandDismiss, 0, 0);
      return 0;
    }
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ListSubclass(HWND window, UINT message, WPARAM wParam,
                              LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
  if (message == WM_CHAR && wParam >= L' ') {
    if (SendMessageW(GetParent(window), kMessageCommandType, wParam,
                     reinterpret_cast<LPARAM>(window)) != 0) {
      return 0;
    }
  }
  if (reference == 1 && message == WM_KEYDOWN &&
      (wParam == VK_UP || wParam == VK_DOWN) &&
      (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
      (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    SendMessageW(GetParent(window), kMessageSidebarMove,
                 wParam == VK_UP ? 1 : 0, reinterpret_cast<LPARAM>(window));
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

std::vector<std::string> SplitKeywords(const std::wstring &text) {
  std::vector<std::string> result;
  std::wstring current;
  const auto flush = [&result, &current] {
    if (!current.empty()) {
      result.push_back(WideToUtf8(current));
      current.clear();
    }
  };
  for (const wchar_t character : text) {
    if (std::iswspace(static_cast<wint_t>(character)) != 0 ||
        character == L',' || character == L';' || character == L'、') {
      flush();
    } else {
      current.push_back(character);
    }
  }
  flush();
  return result;
}

std::wstring JoinKeywords(const std::vector<std::string> &keywords) {
  std::wstring result;
  for (const std::string &keyword : keywords) {
    if (!result.empty())
      result.push_back(L' ');
    result += Utf8ToWide(keyword);
  }
  return result;
}

std::wstring ResolveAppPath(const std::wstring &path) {
  return sf::ResolvePortablePath(path, ExecutablePath().parent_path())
      .wstring();
}

std::wstring MakeAppPath(const std::wstring &path) {
  return sf::MakePortablePath(path, ExecutablePath().parent_path()).wstring();
}

template <typename T> class ComPtr final {
public:
  ComPtr() = default;
  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;
  ~ComPtr() {
    if (ptr_ != nullptr)
      ptr_->Release();
  }
  T **AddressOf() { return &ptr_; }
  T *Get() const { return ptr_; }
  T *operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }
  T *Detach() {
    T *detached = ptr_;
    ptr_ = nullptr;
    return detached;
  }

private:
  T *ptr_ = nullptr;
};

struct PidlDeleter final {
  using pointer = PIDLIST_ABSOLUTE;

  void operator()(pointer pidl) const {
    if (pidl != nullptr)
      ILFree(pidl);
  }
};

using UniquePidl = std::unique_ptr<ITEMIDLIST_ABSOLUTE, PidlDeleter>;

HFONT CreateUiFont(UINT dpi) {
  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                  &metrics, 0, dpi)) {
    return nullptr;
  }
  return CreateFontIndirectW(&metrics.lfMessageFont);
}

HFONT CreateSectionFont(HFONT baseFont) {
  LOGFONTW font{};
  if (baseFont == nullptr ||
      GetObjectW(baseFont, sizeof(font), &font) != sizeof(font)) {
    return nullptr;
  }
  font.lfWeight = FW_SEMIBOLD;
  return CreateFontIndirectW(&font);
}

void DrawRoundedSurface(HDC dc, RECT rect, COLORREF fill, COLORREF border,
                        int radius, int borderWidth = 1) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
  const HGDIOBJ oldBrush = SelectObject(dc, brush);
  const HGDIOBJ oldPen = SelectObject(dc, pen);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
  DeleteObject(brush);
}

bool IsRectVisible(const RECT &rect) {
  return rect.right > rect.left && rect.bottom > rect.top;
}

} // namespace

App::App(HINSTANCE instance)
    : instance_(instance),
      backgroundBrush_(CreateSolidBrush(kBackgroundColor)),
      surfaceBrush_(CreateSolidBrush(kSurfaceColor)),
      sidebarBrush_(CreateSolidBrush(kSidebarColor)),
      activePaneBrush_(CreateSolidBrush(kActivePaneColor)),
      settingsStore_(ExecutablePath().parent_path() / L"simplefiler.json") {}

App::~App() {
  if (cachedBackgroundMenu_ != nullptr)
    cachedBackgroundMenu_->Release();
  for (Pane &pane : panes_) {
    if (pane.worker)
      pane.worker->request_stop();
    for (Pane::RetiredWorker &retired : pane.retiredWorkers)
      retired.thread->request_stop();
  }
  if (accelerators_ != nullptr)
    DestroyAcceleratorTable(accelerators_);
  if (backgroundBrush_ != nullptr)
    DeleteObject(backgroundBrush_);
  if (surfaceBrush_ != nullptr)
    DeleteObject(surfaceBrush_);
  if (sidebarBrush_ != nullptr)
    DeleteObject(sidebarBrush_);
  if (activePaneBrush_ != nullptr)
    DeleteObject(activePaneBrush_);
  if (sectionFont_ != nullptr)
    DeleteObject(sectionFont_);
  if (uiFont_ != nullptr)
    DeleteObject(uiFont_);
}

int App::Run(int showCommand, const std::wstring &initialPath) {
  if (!RegisterClasses() || !CreateMainWindow(showCommand))
    return 1;
  InitializeFromSettings(initialPath);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    const HWND focus = GetFocus();
    const bool paneHasFocus =
        focus == panes_[0].list || focus == panes_[1].list;
    if (accelerators_ == nullptr || !paneHasFocus ||
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
  mainClass.hbrBackground = nullptr;
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
  window_ = CreateWindowExW(0, kWindowClass,
                            L"SimpleFiler — 2ペイン ファイルマネージャー",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y,
                            settings_.windowWidth, settings_.windowHeight,
                            nullptr, nullptr, instance_, this);
  if (window_ == nullptr)
    return false;
  CreateControls();
  CreateAccelerators();
  ShowWindow(window_, settings_.maximized ? SW_MAXIMIZE : showCommand);
  UpdateWindow(window_);
  VerifySettingsWritable();
  if (!loaded.warning.empty())
    Notify(Utf8ToWide(loaded.warning), true);
  return true;
}

void App::CreateControls() {
  dpi_ = GetDpiForWindow(window_);
  uiFont_ = CreateUiFont(dpi_);
  sectionFont_ = CreateSectionFont(uiFont_);
  const HFONT font =
      uiFont_ != nullptr
          ? uiFont_
          : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const std::array<std::pair<const wchar_t *, int>, 6> buttons{
      {{L"PC", IdDrives},
       {L"1｜2", IdTogglePanes},
       {L"サイド", IdToggleSidebar},
       {L"★ 追加", IdAddBookmark},
       {L"リンク ▾", IdAddLink},
       {L"端末 ▾", IdTerminal}}};
  for (std::size_t index = 0; index < buttons.size(); ++index) {
    toolbar_[index] = CreateWindowExW(
        0, L"BUTTON", buttons[index].first,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
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
  SendMessageW(
      searchEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
      MAKELPARAM(MulDiv(8, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI),
                 MulDiv(6, static_cast<int>(dpi_),
                        USER_DEFAULT_SCREEN_DPI)));
  SetWindowSubclass(searchEdit_, EditSubclass, 1, 2);
  toolbar_[6] = CreateWindowExW(
      0, L"BUTTON", L"実行",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
      window_, reinterpret_cast<HMENU>(IdSearch), instance_, nullptr);
  SendMessageW(toolbar_[6], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

  sidebarTitle_ = CreateWindowExW(
      0, L"STATIC", L"★  クイックアクセス",
      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, window_, nullptr,
      instance_, nullptr);
  SendMessageW(sidebarTitle_, WM_SETFONT,
               reinterpret_cast<WPARAM>(sectionFont_ != nullptr ? sectionFont_
                                                                : font),
               TRUE);
  sidebar_ = CreateWindowExW(
      0, WC_LISTBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL |
          LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
      0, 0, 10, 10, window_, reinterpret_cast<HMENU>(IdSidebar), instance_,
      nullptr);
  SendMessageW(sidebar_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessageW(sidebar_, LB_SETITEMHEIGHT, 0,
               MulDiv(28, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI));
  SetWindowSubclass(sidebar_, ListSubclass, 1, 1);

  CreatePaneControls(0);
  CreatePaneControls(1);
  commandSuggestions_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
      WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10,
      10, window_, reinterpret_cast<HMENU>(IdCommandSuggestions), instance_,
      nullptr);
  SendMessageW(commandSuggestions_, WM_SETFONT,
               reinterpret_cast<WPARAM>(font), TRUE);
  SetWindowSubclass(commandSuggestions_, SuggestionSubclass, 1, 0);
  status_ = CreateWindowExW(0, L"STATIC", L"●  準備完了",
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
  UpdateActivePaneVisuals();
}

void App::CreatePaneControls(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  const HFONT font =
      uiFont_ != nullptr
          ? uiFont_
          : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
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
  SetWindowSubclass(pane.list, ListSubclass, 1, 0);
  ListView_SetExtendedListViewStyle(
      pane.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                     LVS_EX_HEADERDRAGDROP);
  ListView_SetTextColor(pane.list, kTextColor);
  SetWindowTheme(pane.list, L"Explorer", nullptr);
  const std::array<std::pair<const wchar_t *, int>, 3> columns{
      {{L"名前", 300}, {L"サイズ", 100}, {L"更新日時", 140}}};
  for (int column = 0; column < static_cast<int>(columns.size()); ++column) {
    LVCOLUMNW value{};
    value.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    value.pszText = const_cast<wchar_t *>(columns[column].first);
    value.cx =
        MulDiv(columns[column].second, static_cast<int>(dpi_),
               USER_DEFAULT_SCREEN_DPI);
    value.iSubItem = column;
    ListView_InsertColumn(pane.list, column, &value);
  }
  const HWND listHeader = ListView_GetHeader(pane.list);
  if (listHeader != nullptr && sectionFont_ != nullptr) {
    SendMessageW(listHeader, WM_SETFONT,
                 reinterpret_cast<WPARAM>(sectionFont_), TRUE);
  }
}

void App::LayoutControls(int width, int height) {
  const auto scale = [this](int value) {
    return MulDiv(value, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  };
  const int gap = scale(10);
  const int cardInset = scale(6);
  const int sidebarTitleHeight = scale(26);
  const int toolbarHeight = scale(kToolbarHeight);
  const int addressHeight = scale(kAddressHeight);
  const int statusHeight = scale(kStatusHeight);
  const int splitterWidth = scale(kSplitterWidth);
  int x = gap;
  const int buttonHeight = scale(32);
  const int searchHeight = scale(30);
  const int buttonTop = (toolbarHeight - buttonHeight) / 2;
  const int searchTop = (toolbarHeight - searchHeight) / 2;
  const int buttonGap = scale(7);
  const std::array<int, 6> buttonWidths{56, 56, 64, 68, 74, 68};
  for (std::size_t index = 0; index < buttonWidths.size(); ++index) {
    const int buttonWidth = scale(buttonWidths[index]);
    MoveWindow(toolbar_[index], x, buttonTop, buttonWidth, buttonHeight, TRUE);
    x += buttonWidth;
    if (index + 1 < buttonWidths.size())
      x += buttonGap;
  }
  x += scale(12);
  const int searchButtonWidth = scale(60);
  const int searchGap = scale(8);
  const int searchWidth =
      std::max(scale(100), width - x - searchButtonWidth - gap - searchGap);
  MoveWindow(searchEdit_, x, searchTop, searchWidth, searchHeight, TRUE);
  MoveWindow(toolbar_[6], x + searchWidth + searchGap, buttonTop,
             searchButtonWidth, buttonHeight, TRUE);
  MoveWindow(commandSuggestions_, x, toolbarHeight,
             searchWidth + searchGap + searchButtonWidth, scale(220), TRUE);

  const int contentTop = toolbarHeight + gap;
  const int contentBottom =
      std::max(contentTop, height - statusHeight - scale(4));
  const int contentHeight = contentBottom - contentTop;
  const int sidebarWidth =
      sidebarVisible_ ? std::min(scale(kSidebarWidth), width / 3) : 0;
  ShowWindow(sidebar_, sidebarVisible_ ? SW_SHOW : SW_HIDE);
  ShowWindow(sidebarTitle_, sidebarVisible_ ? SW_SHOW : SW_HIDE);
  if (sidebarVisible_) {
    sidebarCardRect_ = {gap, contentTop, gap + sidebarWidth, contentBottom};
    MoveWindow(sidebarTitle_, sidebarCardRect_.left + scale(12),
               sidebarCardRect_.top + scale(7),
               sidebarWidth - scale(24), scale(20), TRUE);
    MoveWindow(sidebar_, sidebarCardRect_.left + cardInset,
               sidebarCardRect_.top + sidebarTitleHeight,
               sidebarWidth - cardInset * 2,
               std::max(0,
                        contentHeight - sidebarTitleHeight - cardInset),
               TRUE);
  } else {
    sidebarCardRect_ = {};
  }
  const int paneLeft =
      sidebarVisible_ ? gap + sidebarWidth + gap : gap;
  const int paneWidth = std::max(0, width - paneLeft - gap);
  int leftWidth = paneWidth;
  int rightLeft = width;
  int rightWidth = 0;
  if (twoPanes_) {
    leftWidth = static_cast<int>((paneWidth - splitterWidth) * splitRatio_);
    rightLeft = paneLeft + leftWidth + splitterWidth;
    rightWidth = width - rightLeft - gap;
  }

  paneCardRects_[0] = {paneLeft, contentTop, paneLeft + leftWidth,
                       contentBottom};
  MoveWindow(panes_[0].address, paneLeft + cardInset,
             contentTop + cardInset, std::max(0, leftWidth - cardInset * 2),
             addressHeight, TRUE);
  MoveWindow(panes_[0].list, paneLeft + cardInset,
             contentTop + cardInset + addressHeight + scale(5),
             std::max(0, leftWidth - cardInset * 2),
             std::max(0, contentHeight - addressHeight - cardInset * 2 -
                             scale(5)),
             TRUE);
  ShowWindow(panes_[1].address, twoPanes_ ? SW_SHOW : SW_HIDE);
  ShowWindow(panes_[1].list, twoPanes_ ? SW_SHOW : SW_HIDE);
  if (twoPanes_) {
    paneCardRects_[1] = {rightLeft, contentTop, rightLeft + rightWidth,
                         contentBottom};
    MoveWindow(panes_[1].address, rightLeft + cardInset,
               contentTop + cardInset,
               std::max(0, rightWidth - cardInset * 2), addressHeight, TRUE);
    MoveWindow(panes_[1].list, rightLeft + cardInset,
               contentTop + cardInset + addressHeight + scale(5),
               std::max(0, rightWidth - cardInset * 2),
               std::max(0, contentHeight - addressHeight - cardInset * 2 -
                               scale(5)),
               TRUE);
  } else {
    paneCardRects_[1] = {};
  }
  MoveWindow(status_, gap + scale(4), height - statusHeight + scale(5),
             std::max(0, width - gap * 2 - scale(8)),
             statusHeight - scale(5), TRUE);
  // MoveWindow's own repaint only invalidates the delta between the old and
  // new rect, not the whole client area. LVS_EX_DOUBLEBUFFER caches a
  // full-client offscreen bitmap, so a partial invalidate leaves stale
  // glyphs from the previous width composited into the new layout (seen as
  // truncated/overlapping file names after toggling the sidebar). Forcing a
  // full-rect invalidate here fixes it for every caller of LayoutControls
  // (WM_SIZE, WM_DPICHANGED, splitter drag, pane/sidebar toggle).
  InvalidateRect(panes_[0].list, nullptr, TRUE);
  InvalidateRect(panes_[1].list, nullptr, TRUE);
  InvalidateRect(window_, nullptr, TRUE);
}

void App::ApplyDpi(UINT dpi) {
  if (dpi == 0 || dpi == dpi_)
    return;
  dpi_ = dpi;
  HFONT newFont = CreateUiFont(dpi_);
  if (newFont != nullptr) {
    HFONT newSectionFont = CreateSectionFont(newFont);
    for (HWND control : toolbar_) {
      if (control != nullptr)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont),
                     TRUE);
    }
    for (HWND control : {searchEdit_, commandSuggestions_, sidebar_, status_,
                         panes_[0].address, panes_[0].list, panes_[1].address,
                         panes_[1].list}) {
      if (control != nullptr)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont),
                     TRUE);
    }
    for (HWND control : {sidebarTitle_}) {
      if (control != nullptr)
        SendMessageW(
            control, WM_SETFONT,
            reinterpret_cast<WPARAM>(newSectionFont != nullptr
                                         ? newSectionFont
                                         : newFont),
            TRUE);
    }
    if (sectionFont_ != nullptr)
      DeleteObject(sectionFont_);
    sectionFont_ = newSectionFont;
    if (uiFont_ != nullptr)
      DeleteObject(uiFont_);
    uiFont_ = newFont;
  }
  SendMessageW(sidebar_, LB_SETITEMHEIGHT, 0,
               MulDiv(28, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI));
  SendMessageW(
      searchEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
      MAKELPARAM(MulDiv(8, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI),
                 MulDiv(6, static_cast<int>(dpi_),
                        USER_DEFAULT_SCREEN_DPI)));
  const std::array<int, 3> columnWidths{300, 100, 140};
  for (Pane &pane : panes_) {
    for (int column = 0; column < static_cast<int>(columnWidths.size());
         ++column) {
      ListView_SetColumnWidth(
          pane.list, column,
          MulDiv(columnWidths[column], static_cast<int>(dpi_),
                 USER_DEFAULT_SCREEN_DPI));
    }
  }
}

void App::UpdateActivePaneVisuals() {
  for (int index = 0; index < 2; ++index) {
    const COLORREF background =
        index == activePane_ ? kActivePaneColor : kSurfaceColor;
    ListView_SetBkColor(panes_[index].list, background);
    ListView_SetTextBkColor(panes_[index].list, background);
    InvalidateRect(panes_[index].list, nullptr, FALSE);
    InvalidateRect(panes_[index].address, nullptr, TRUE);
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void App::RestorePaneFocusIfNeeded() {
  if (!IsWindowEnabled(window_) || GetActiveWindow() != window_)
    return;
  const HWND focus = GetFocus();
  if (focus == nullptr || focus == window_ || !IsChild(window_, focus))
    SetFocus(panes_[activePane_].list);
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
                          {FVIRTKEY, VK_TAB, IdSwitchPane},
                          {FVIRTKEY, VK_F5, IdCopyToOther},
                          {FVIRTKEY, VK_F6, IdMoveToOther},
                          {FVIRTKEY, VK_F7, IdNewFolder},
                          {FVIRTKEY | FCONTROL, 'N', IdNewFolder},
                          {FVIRTKEY, VK_BACK, IdUp},
                          {FVIRTKEY | FALT, VK_LEFT, IdBack},
                          {FVIRTKEY | FALT, VK_RIGHT, IdForward},
                          {FVIRTKEY | FALT, VK_UP, IdUp}};
  accelerators_ = CreateAcceleratorTableW(const_cast<ACCEL *>(values),
                                          static_cast<int>(std::size(values)));
}

void App::InitializeFromSettings(const std::wstring &initialPath) {
  for (int index = 0; index < 2; ++index) {
    panes_[index].showHidden = settings_.panes[index].showHidden;
    panes_[index].sortColumn =
        settings_.panes[index].sortColumn == 1
            ? 0
            : settings_.panes[index].sortColumn;
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

void App::VerifySettingsWritable() {
  const std::filesystem::path settingsPath = settingsStore_.Path();
  const DWORD attributes = GetFileAttributesW(settingsPath.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
    settingsWritable_ = false;
  } else {
    std::filesystem::path probe = settingsPath;
    probe += std::filesystem::path(".write-test-" + MakeStableId());
    {
      std::ofstream stream(probe, std::ios::binary | std::ios::trunc);
      settingsWritable_ = static_cast<bool>(stream);
    }
    std::error_code ignored;
    std::filesystem::remove(probe, ignored);
  }
  if (!settingsWritable_) {
    MessageBoxW(
        window_,
        L"SimpleFilerフォルダーへ設定を書き込めません。\n"
        L"ファイル操作は続けられますが、この起動中の設定変更は保存されません。",
        L"ポータブル設定", MB_OK | MB_ICONWARNING);
  }
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
  if (!settingsWritable_) {
    Notify(L"設定フォルダーへ書き込めないため保存しませんでした", true);
    return;
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
    SetWindowTextW(toolbar_[6], L"実行");
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
  SetWindowTextW(toolbar_[6], L"中止");
  Notify(L"検索中: " + query);
}

void App::SortPane(int paneIndex) {
  Pane &pane = panes_[paneIndex];
  std::unordered_set<std::wstring> selectedPaths;
  int selectedIndex = -1;
  while ((selectedIndex =
              ListView_GetNextItem(pane.list, selectedIndex, LVNI_SELECTED)) >=
         0) {
    if (selectedIndex < static_cast<int>(pane.items.size()))
      selectedPaths.insert(pane.items[selectedIndex].path);
  }
  std::wstring focusedPath;
  const int focusedIndex =
      ListView_GetNextItem(pane.list, -1, LVNI_FOCUSED);
  if (focusedIndex >= 0 &&
      focusedIndex < static_cast<int>(pane.items.size())) {
    focusedPath = pane.items[focusedIndex].path;
  }

  const int column = pane.sortColumn;
  const bool ascending = pane.sortAscending;
  std::stable_sort(
      pane.items.begin(), pane.items.end(),
      [column, ascending](const FileItem &left, const FileItem &right) {
        int comparison = 0;
        if (left.IsDirectory() != right.IsDirectory()) {
          comparison = left.IsDirectory() ? -1 : 1;
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
  ListView_SetItemState(pane.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  for (int index = 0; index < static_cast<int>(pane.items.size()); ++index) {
    UINT state = 0;
    if (selectedPaths.contains(pane.items[index].path))
      state |= LVIS_SELECTED;
    if (!focusedPath.empty() && pane.items[index].path == focusedPath)
      state |= LVIS_FOCUSED;
    if (state != 0) {
      ListView_SetItemState(pane.list, index, state,
                            LVIS_SELECTED | LVIS_FOCUSED);
    }
  }
  if (!pane.items.empty()) {
    ListView_RedrawItems(pane.list, 0,
                         static_cast<int>(pane.items.size()) - 1);
  }
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

void App::TransferSelectionToOtherPane(bool move) {
  if (!twoPanes_) {
    Notify(L"反対側のペインが表示されていません", true);
    return;
  }
  const auto paths = SelectedPaths();
  if (paths.empty())
    return;
  const Pane &destinationPane = panes_[1 - activePane_];
  const std::wstring destination =
      destinationPane.searchMode ? destinationPane.searchRoot
                                 : destinationPane.path;
  if (destination.empty()) {
    Notify(L"反対ペインのコピー先フォルダーがありません", true);
    return;
  }
  ++pendingFileOperations_;
  TransferFilesAsync(window_, paths, destination, move);
  Notify(move ? L"反対ペインへの移動を開始しました"
              : L"反対ペインへのコピーを開始しました");
}

void App::Paste() {
  const Pane &pane = panes_[activePane_];
  if (!pane.path.empty()) {
    ++pendingFileOperations_;
    PasteFilesAsync(window_, pane.searchMode ? pane.searchRoot : pane.path);
    Notify(L"ファイル操作を開始しました");
  }
}

void App::DeleteSelection(bool permanent) {
  auto paths = SelectedPaths();
  if (paths.empty())
    return;
  ++pendingFileOperations_;
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
  ++pendingFileOperations_;
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
  AddBookmarkForPath(path);
}

void App::AddLinkedFolder() {
  const std::wstring path = PickFolder(window_, L"フォルダーリンクを登録");
  if (path.empty())
    return;
  AddBookmarkForPath(path);
}

void App::AddBookmarkForPath(const std::wstring &path) {
  const std::wstring name =
      PromptText(L"ブックマーク追加", L"表示名", LeafName(path));
  if (name.empty())
    return;
  const std::wstring alias =
      PromptText(L"ブックマーク追加", L"ffのエイリアス（省略可）");
  const std::wstring keywords = PromptText(
      L"ブックマーク追加", L"検索キーワード（空白区切り、省略可）");
  settings_.bookmarks.push_back(
      {MakeStableId(), WideToUtf8(name), WideToUtf8(path), WideToUtf8(alias),
       SplitKeywords(keywords)});
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
  const std::wstring alias = PromptText(
      L"リンク追加",
      application ? L"aaのエイリアス（省略可）"
                  : L"エイリアス（省略可）");
  const std::wstring keywords = PromptText(
      L"リンク追加", L"検索キーワード（空白区切り、省略可）");
  const bool runAsAdministrator =
      application &&
      MessageBoxW(window_, L"通常の起動を管理者権限にしますか？",
                  L"アプリリンク", MB_ICONQUESTION | MB_YESNO |
                                        MB_DEFBUTTON2) == IDYES;
  settings_.links.push_back(
      {MakeStableId(), application ? LinkType::Application : LinkType::File,
       WideToUtf8(name), WideToUtf8(MakeAppPath(file.data())),
       WideToUtf8(arguments),
       WideToUtf8(MakeAppPath(workingDirectory)), WideToUtf8(alias),
       SplitKeywords(keywords), runAsAdministrator});
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
    const std::wstring target =
        ResolveAppPath(Utf8ToWide(link.target));
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
  if (link.type == LinkType::Application) {
    LaunchRegisteredApplication(index, administrator, true);
    return;
  }
  const std::wstring target = ResolveAppPath(Utf8ToWide(link.target));
  if (GetFileAttributesW(ToExtendedPath(target).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    Notify(L"登録先が見つかりません: " + target, true);
    return;
  }
  if (!OpenPath(window_, target, Utf8ToWide(link.arguments),
                ResolveAppPath(Utf8ToWide(link.workingDirectory)))) {
    const DWORD error = GetLastError();
    if (error != ERROR_CANCELLED)
      Notify(L"リンク先を開けません: " + WindowsErrorMessage(error), true);
  }
}

void App::EditSidebarItem() {
  const int selected =
      static_cast<int>(SendMessageW(sidebar_, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;

  const auto [bookmarkEntry, index] = sidebarMap_[selected];
  if (bookmarkEntry) {
    Bookmark &bookmark = settings_.bookmarks[index];
    const std::wstring name = PromptText(
        L"フォルダー登録を編集", L"表示名", Utf8ToWide(bookmark.name));
    if (name.empty())
      return;
    const std::wstring path = PromptText(
        L"フォルダー登録を編集", L"フォルダーパス",
        Utf8ToWide(bookmark.path));
    if (path.empty())
      return;
    const std::wstring alias = PromptText(
        L"フォルダー登録を編集", L"ffのエイリアス（省略可）",
        Utf8ToWide(bookmark.alias));
    const std::wstring keywords = PromptText(
        L"フォルダー登録を編集", L"検索キーワード（空白区切り）",
        JoinKeywords(bookmark.keywords));
    bookmark.name = WideToUtf8(name);
    bookmark.path = WideToUtf8(path);
    bookmark.alias = WideToUtf8(alias);
    bookmark.keywords = SplitKeywords(keywords);
  } else {
    RegisteredLink &link = settings_.links[index];
    const std::wstring name = PromptText(
        L"リンク登録を編集", L"表示名", Utf8ToWide(link.name));
    if (name.empty())
      return;
    const std::wstring target = PromptText(
        L"リンク登録を編集", L"対象パス", Utf8ToWide(link.target));
    if (target.empty())
      return;
    std::wstring arguments = Utf8ToWide(link.arguments);
    std::wstring workingDirectory = Utf8ToWide(link.workingDirectory);
    if (link.type == LinkType::Application) {
      arguments = PromptText(L"アプリ登録を編集", L"引数テンプレート",
                             arguments);
      workingDirectory = PromptText(L"アプリ登録を編集",
                                    L"作業フォルダー（省略可）",
                                    workingDirectory);
    }
    const std::wstring alias = PromptText(
        L"リンク登録を編集",
        link.type == LinkType::Application ? L"aaのエイリアス（省略可）"
                                           : L"エイリアス（省略可）",
        Utf8ToWide(link.alias));
    const std::wstring keywords = PromptText(
        L"リンク登録を編集", L"検索キーワード（空白区切り）",
        JoinKeywords(link.keywords));
    bool runAsAdministrator = link.runAsAdministrator;
    if (link.type == LinkType::Application) {
      runAsAdministrator =
          MessageBoxW(window_, L"通常の起動を管理者権限にしますか？",
                      L"アプリ登録を編集",
                      MB_ICONQUESTION | MB_YESNO |
                          (link.runAsAdministrator ? MB_DEFBUTTON1
                                                   : MB_DEFBUTTON2)) == IDYES;
    }
    link.name = WideToUtf8(name);
    link.target = WideToUtf8(MakeAppPath(target));
    link.arguments = WideToUtf8(arguments);
    link.workingDirectory =
        WideToUtf8(MakeAppPath(workingDirectory));
    link.alias = WideToUtf8(alias);
    link.keywords = SplitKeywords(keywords);
    link.runAsAdministrator = runAsAdministrator;
  }
  RebuildSidebar();
  SaveSettings();
  SendMessageW(sidebar_, LB_SETCURSEL, selected, 0);
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

void App::MoveSidebarItem(bool up) {
  const int selected =
      static_cast<int>(SendMessageW(sidebar_, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(sidebarMap_.size()))
    return;
  const auto [bookmark, index] = sidebarMap_[selected];
  const std::size_t sectionSize =
      bookmark ? settings_.bookmarks.size() : settings_.links.size();
  if (up ? index == 0 : index + 1 >= sectionSize)
    return;
  const std::size_t otherIndex = up ? index - 1 : index + 1;
  if (bookmark) {
    std::swap(settings_.bookmarks[index], settings_.bookmarks[otherIndex]);
  } else {
    std::swap(settings_.links[index], settings_.links[otherIndex]);
  }
  RebuildSidebar();
  SaveSettings();
  SendMessageW(sidebar_, LB_SETCURSEL,
               up ? selected - 1 : selected + 1, 0);
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

void App::RebuildCommandSuggestions() {
  const CommandQuery command =
      ParseCommandQuery(GetWindowTextString(searchEdit_));
  SendMessageW(commandSuggestions_, LB_RESETCONTENT, 0, 0);
  commandSuggestionItems_.clear();
  if (command.mode == CommandMode::None) {
    ShowWindow(commandSuggestions_, SW_HIDE);
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
    for (std::size_t index = 0; index < settings_.bookmarks.size(); ++index) {
      const Bookmark &bookmark = settings_.bookmarks[index];
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
        commandSuggestionItems_.push_back(
            {CommandSuggestionKind::Folder, index, false, *score, label,
             path});
      }
    }
  } else if (command.mode == CommandMode::Application) {
    for (std::size_t index = 0; index < settings_.links.size(); ++index) {
      const RegisteredLink &link = settings_.links[index];
      if (link.type != LinkType::Application)
        continue;
      const std::wstring name = Utf8ToWide(link.name);
      const std::wstring target =
          ResolveAppPath(Utf8ToWide(link.target));
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
        commandSuggestionItems_.push_back({CommandSuggestionKind::Application,
                                           index, false, *score, label,
                                           target});
      }
    }
  } else if (command.mode == CommandMode::Terminal) {
    const Pane &pane = panes_[activePane_];
    const std::wstring directory =
        pane.searchMode ? pane.searchRoot : pane.path;
    for (const auto &[administrator, terms, label] :
         std::array<std::tuple<bool, std::wstring, std::wstring>, 2>{
             {{false, L"通常 normal", L"CMDをここで開く"},
              {true, L"admin 管理者", L"管理者CMDをここで開く"}}}) {
      const std::optional<int> score = bestScore({terms, label});
      if (score) {
        commandSuggestionItems_.push_back(
            {CommandSuggestionKind::Terminal, 0, administrator, *score, label,
             directory});
      }
    }
  }

  std::stable_sort(commandSuggestionItems_.begin(),
                   commandSuggestionItems_.end(),
                   [](const CommandSuggestion &left,
                      const CommandSuggestion &right) {
                     if (left.score != right.score)
                       return left.score > right.score;
                     return left.label < right.label;
                   });
  for (const CommandSuggestion &suggestion : commandSuggestionItems_) {
    const std::wstring text = suggestion.detail.empty()
                                  ? suggestion.label
                                  : suggestion.label + L"    " +
                                        suggestion.detail;
    SendMessageW(commandSuggestions_, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text.c_str()));
  }
  if (commandSuggestionItems_.empty()) {
    ShowWindow(commandSuggestions_, SW_HIDE);
    Notify(L"一致する候補がありません");
    return;
  }
  SendMessageW(commandSuggestions_, LB_SETCURSEL, 0, 0);
  SetWindowPos(commandSuggestions_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void App::MoveCommandSelection(int delta) {
  if (!IsWindowVisible(commandSuggestions_) ||
      commandSuggestionItems_.empty()) {
    return;
  }
  int selected =
      static_cast<int>(SendMessageW(commandSuggestions_, LB_GETCURSEL, 0, 0));
  if (selected < 0)
    selected = 0;
  selected = std::clamp(selected + delta, 0,
                        static_cast<int>(commandSuggestionItems_.size()) - 1);
  SendMessageW(commandSuggestions_, LB_SETCURSEL, selected, 0);
}

void App::AcceptCommandSuggestion(bool control, bool shift) {
  if (!IsWindowVisible(commandSuggestions_)) {
    if (ParseCommandQuery(GetWindowTextString(searchEdit_)).mode !=
        CommandMode::None) {
      Notify(L"実行できる候補がありません", true);
      return;
    }
    StartSearch(GetWindowTextString(searchEdit_));
    return;
  }
  const int selected =
      static_cast<int>(SendMessageW(commandSuggestions_, LB_GETCURSEL, 0, 0));
  if (selected < 0 ||
      selected >= static_cast<int>(commandSuggestionItems_.size())) {
    return;
  }
  const CommandSuggestion suggestion = commandSuggestionItems_[selected];
  DismissCommandSuggestions(true);

  switch (suggestion.kind) {
  case CommandSuggestionKind::Folder:
    if (suggestion.sourceIndex < settings_.bookmarks.size()) {
      const int pane = shift && twoPanes_ ? 1 - activePane_ : activePane_;
      Navigate(pane,
               Utf8ToWide(settings_.bookmarks[suggestion.sourceIndex].path));
      activePane_ = pane;
      UpdateActivePaneVisuals();
      SetFocus(panes_[activePane_].list);
    }
    break;
  case CommandSuggestionKind::Application:
    LaunchRegisteredApplication(suggestion.sourceIndex, control, !shift);
    break;
  case CommandSuggestionKind::Terminal:
    LaunchSelectedTerminal(TerminalKind::CommandPrompt,
                           suggestion.administrator);
    break;
  }
}

void App::DismissCommandSuggestions(bool clearInput) {
  ShowWindow(commandSuggestions_, SW_HIDE);
  commandSuggestionItems_.clear();
  if (clearInput)
    SetWindowTextW(searchEdit_, L"");
  SetFocus(panes_[activePane_].list);
}

bool App::HandleCommandPrefixCharacter(wchar_t character, HWND source) {
  constexpr ULONGLONG kPrefixTimeoutMilliseconds = 1500;
  character = static_cast<wchar_t>(
      std::towlower(static_cast<wint_t>(character)));
  const ULONGLONG now = GetTickCount64();
  if (source != commandPrefixSource_ ||
      now - commandPrefixTick_ > kPrefixTimeoutMilliseconds) {
    commandPrefixBuffer_.clear();
  }
  commandPrefixSource_ = source;
  commandPrefixTick_ = now;

  std::wstring completedPrefix;
  if ((character == L'f' || character == L'a') &&
      commandPrefixBuffer_.size() == 1 &&
      commandPrefixBuffer_.front() == character) {
    completedPrefix.assign(2, character);
  } else if (character == L'd' && commandPrefixBuffer_ == L"cm") {
    completedPrefix = L"cmd";
  }

  if (!completedPrefix.empty()) {
    commandPrefixBuffer_.clear();
    SetWindowTextW(searchEdit_, completedPrefix.c_str());
    SetFocus(searchEdit_);
    SendMessageW(searchEdit_, EM_SETSEL,
                 static_cast<WPARAM>(completedPrefix.size()),
                 static_cast<LPARAM>(completedPrefix.size()));
    return true;
  }

  if (character == L'f' || character == L'a' || character == L'c') {
    commandPrefixBuffer_.assign(1, character);
  } else if (character == L'm' && commandPrefixBuffer_ == L"c") {
    commandPrefixBuffer_ = L"cm";
  } else {
    commandPrefixBuffer_.clear();
  }
  return false;
}

void App::AddCommandRegistration() {
  const CommandMode mode =
      ParseCommandQuery(GetWindowTextString(searchEdit_)).mode;
  ShowWindow(commandSuggestions_, SW_HIDE);
  if (mode == CommandMode::Folder) {
    AddCurrentBookmark();
  } else if (mode == CommandMode::Application) {
    AddLink(true);
  } else {
    Notify(L"ffまたはaaを入力してから登録してください", true);
    return;
  }
  SetFocus(searchEdit_);
  RebuildCommandSuggestions();
}

void App::LaunchRegisteredApplication(std::size_t index, bool administrator,
                                      bool passSelection) {
  if (index >= settings_.links.size() ||
      settings_.links[index].type != LinkType::Application) {
    return;
  }
  const RegisteredLink &link = settings_.links[index];
  const std::wstring target = ResolveAppPath(Utf8ToWide(link.target));
  if (GetFileAttributesW(ToExtendedPath(target).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    Notify(L"登録アプリが見つかりません: " + target, true);
    return;
  }

  const Pane &pane = panes_[activePane_];
  const auto paneFolder = [](const Pane &value) {
    return value.searchMode ? value.searchRoot : value.path;
  };
  AppArgumentContext context;
  if (passSelection)
    context.files = SelectedPaths();
  context.folder = paneFolder(pane);
  context.left = paneFolder(panes_[0]);
  context.right = paneFolder(panes_[1]);
  context.other = paneFolder(panes_[1 - activePane_]);

  std::wstring arguments;
  const std::wstring argumentTemplate = Utf8ToWide(link.arguments);
  if (!argumentTemplate.empty()) {
    const AppArgumentExpansion expansion =
        ExpandAppArgumentTemplate(argumentTemplate, context);
    if (!expansion) {
      Notify(L"アプリ引数を作成できません: " + expansion.error, true);
      return;
    }
    arguments = expansion.commandLine;
  } else if (passSelection) {
    if (context.files.empty()) {
      arguments = QuoteWindowsCommandLineArgument(context.folder);
    } else {
      for (const std::wstring &path : context.files) {
        if (!arguments.empty())
          arguments.push_back(L' ');
        arguments += QuoteWindowsCommandLineArgument(path);
      }
    }
  }

  std::wstring workingDirectory =
      ResolveAppPath(Utf8ToWide(link.workingDirectory));
  if (workingDirectory.empty())
    workingDirectory = context.folder;
  if (!OpenPath(window_, target, arguments, workingDirectory,
                administrator || link.runAsAdministrator)) {
    const DWORD error = GetLastError();
    if (error != ERROR_CANCELLED) {
      Notify(L"登録アプリを起動できません: " + WindowsErrorMessage(error),
             true);
    }
  }
}

void App::ShowLinkMenu(HWND sourceButton) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IdAddFolderLink, L"フォルダーリンクを追加");
  AppendMenuW(menu, MF_STRING, IdAddFileLink, L"ファイルリンクを追加");
  AppendMenuW(menu, MF_STRING, IdAddAppLink, L"アプリリンクを追加");
  RECT rectangle{};
  GetWindowRect(sourceButton, &rectangle);
  TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rectangle.left,
                 rectangle.bottom, 0, window_, nullptr);
  DestroyMenu(menu);
}

void App::AppendFallbackBackgroundMenu(POINT screenPoint) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IdPaste, L"貼り付け");
  AppendMenuW(menu, MF_STRING, IdNewFolder, L"新しいフォルダー");
  const UINT selectedCommand =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                     screenPoint.y, 0, window_, nullptr);
  DestroyMenu(menu);
  if (selectedCommand != 0)
    SendMessageW(window_, WM_COMMAND, selectedCommand, 0);
}

void App::ShowBackgroundShellMenu(const std::wstring &folderPath,
                                  POINT screenPoint) {
  IContextMenu *contextMenu = nullptr;
  if (cachedBackgroundMenu_ != nullptr &&
      cachedBackgroundMenuFolder_ == folderPath) {
    contextMenu = cachedBackgroundMenu_;
  } else {
    if (cachedBackgroundMenu_ != nullptr) {
      cachedBackgroundMenu_->Release();
      cachedBackgroundMenu_ = nullptr;
      cachedBackgroundMenuFolder_.clear();
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(folderPath.c_str(), nullptr, &pidl, 0,
                                  nullptr)) ||
        pidl == nullptr) {
      AppendFallbackBackgroundMenu(screenPoint);
      return;
    }
    ComPtr<IShellFolder> desktop;
    ComPtr<IShellFolder> folder;
    ComPtr<IContextMenu> freshContextMenu;
    const bool bound =
        SUCCEEDED(SHGetDesktopFolder(desktop.AddressOf())) &&
        SUCCEEDED(desktop->BindToObject(pidl, nullptr,
                                        IID_PPV_ARGS(folder.AddressOf()))) &&
        SUCCEEDED(folder->CreateViewObject(
            window_, IID_PPV_ARGS(freshContextMenu.AddressOf())));
    ILFree(pidl);
    if (!bound) {
      AppendFallbackBackgroundMenu(screenPoint);
      return;
    }
    cachedBackgroundMenuFolder_ = folderPath;
    cachedBackgroundMenu_ = freshContextMenu.Detach();
    contextMenu = cachedBackgroundMenu_;
  }

  HMENU menu = CreatePopupMenu();
  // The background menu has no selected item, so CMF_CANRENAME (which
  // Explorer only sets for a single-item selection) is intentionally omitted.
  if (FAILED(contextMenu->QueryContextMenu(menu, 0, IdShellMenuFirst,
                                           IdShellMenuLast, CMF_NORMAL))) {
    DestroyMenu(menu);
    cachedBackgroundMenu_->Release();
    cachedBackgroundMenu_ = nullptr;
    cachedBackgroundMenuFolder_.clear();
    AppendFallbackBackgroundMenu(screenPoint);
    return;
  }

  ComPtr<IContextMenu2> contextMenu2;
  ComPtr<IContextMenu3> contextMenu3;
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu2.AddressOf())));
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu3.AddressOf())));

  const UINT selectedCommand = [&] {
    // Scope guard: HandleMessage forwards WM_INITMENUPOPUP/WM_MENUCHAR/etc.
    // to these pointers only while they are armed here, and they must be
    // disarmed before returning on every path, including early returns
    // added later, so any lingering pointer can't be used after this
    // function releases it.
    struct ScopedActiveMenu final {
      App &app;
      ScopedActiveMenu(App &app, IContextMenu2 *menu2, IContextMenu3 *menu3)
          : app(app) {
        app.activeShellMenu2_ = menu2;
        app.activeShellMenu3_ = menu3;
      }
      ~ScopedActiveMenu() {
        app.activeShellMenu2_ = nullptr;
        app.activeShellMenu3_ = nullptr;
      }
    } scopedActiveMenu(*this, contextMenu2.Get(), contextMenu3.Get());
    return TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                          screenPoint.x, screenPoint.y, 0, window_, nullptr);
  }();

  if (selectedCommand >= IdShellMenuFirst &&
      selectedCommand <= IdShellMenuLast) {
    // lpVerb/lpVerbW must carry the *offset* from idCmdFirst, packed via
    // MAKEINTRESOURCE, not the raw command ID or a real string pointer;
    // CMIC_MASK_UNICODE tells the shell to prefer lpVerbW.
    const UINT_PTR verbOffset = selectedCommand - IdShellMenuFirst;
    CMINVOKECOMMANDINFOEX invoke{};
    invoke.cbSize = sizeof(invoke);
    invoke.fMask = CMIC_MASK_UNICODE;
    invoke.hwnd = window_;
    invoke.lpVerb = MAKEINTRESOURCEA(verbOffset);
    invoke.lpVerbW = MAKEINTRESOURCEW(verbOffset);
    invoke.nShow = SW_SHOWNORMAL;
    if (FAILED(contextMenu->InvokeCommand(
            reinterpret_cast<CMINVOKECOMMANDINFO *>(&invoke)))) {
      cachedBackgroundMenu_->Release();
      cachedBackgroundMenu_ = nullptr;
      cachedBackgroundMenuFolder_.clear();
    }
    RefreshPane(activePane_);
  }
  // The dynamically populated "New" submenu keeps command state in its menu
  // items until InvokeCommand returns. Destroying the menu first makes commands
  // such as "New Folder" fail with E_FAIL.
  DestroyMenu(menu);
}

bool App::ShowItemShellMenu(const std::vector<std::wstring> &paths,
                            POINT screenPoint) {
  if (paths.empty())
    return false;

  std::vector<UniquePidl> itemPidls;
  std::vector<PCUITEMID_CHILD> childPidls;
  itemPidls.reserve(paths.size());
  childPidls.reserve(paths.size());

  UniquePidl parentPidl;
  for (const std::wstring &path : paths) {
    PIDLIST_ABSOLUTE rawItemPidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &rawItemPidl, 0,
                                  nullptr)) ||
        rawItemPidl == nullptr) {
      return false;
    }
    UniquePidl itemPidl(rawItemPidl);
    UniquePidl currentParent(ILCloneFull(itemPidl.get()));
    if (!currentParent || !ILRemoveLastID(currentParent.get()))
      return false;
    if (!parentPidl) {
      parentPidl.reset(ILCloneFull(currentParent.get()));
      if (!parentPidl)
        return false;
    } else if (!ILIsEqual(parentPidl.get(), currentParent.get())) {
      // IShellFolder::GetUIObjectOf requires all selected child PIDLs to be
      // relative to one parent. This can occur for cross-folder search results.
      return false;
    }

    childPidls.push_back(ILFindLastID(itemPidl.get()));
    itemPidls.push_back(std::move(itemPidl));
  }

  ComPtr<IShellFolder> parentFolder;
  PCUITEMID_CHILD ignoredChild = nullptr;
  if (FAILED(SHBindToParent(
          itemPidls.front().get(), IID_IShellFolder,
          reinterpret_cast<void **>(parentFolder.AddressOf()),
          &ignoredChild))) {
    return false;
  }

  ComPtr<IContextMenu> contextMenu;
  if (FAILED(parentFolder->GetUIObjectOf(
          window_, static_cast<UINT>(childPidls.size()), childPidls.data(),
          IID_IContextMenu, nullptr,
          reinterpret_cast<void **>(contextMenu.AddressOf())))) {
    return false;
  }

  HMENU menu = CreatePopupMenu();
  if (menu == nullptr)
    return false;

  UINT queryFlags = CMF_NORMAL | CMF_ITEMMENU;
  if (paths.size() == 1)
    queryFlags |= CMF_CANRENAME;
  if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    queryFlags |= CMF_EXTENDEDVERBS;
  if (FAILED(contextMenu->QueryContextMenu(
          menu, 0, IdShellMenuFirst, IdShellMenuLast, queryFlags))) {
    DestroyMenu(menu);
    return false;
  }

  ComPtr<IContextMenu2> contextMenu2;
  ComPtr<IContextMenu3> contextMenu3;
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu2.AddressOf())));
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu3.AddressOf())));

  const UINT selectedCommand = [&] {
    struct ScopedActiveMenu final {
      App &app;
      ScopedActiveMenu(App &app, IContextMenu2 *menu2, IContextMenu3 *menu3)
          : app(app) {
        app.activeShellMenu2_ = menu2;
        app.activeShellMenu3_ = menu3;
      }
      ~ScopedActiveMenu() {
        app.activeShellMenu2_ = nullptr;
        app.activeShellMenu3_ = nullptr;
      }
    } scopedActiveMenu(*this, contextMenu2.Get(), contextMenu3.Get());
    return TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                          screenPoint.x, screenPoint.y, 0, window_, nullptr);
  }();

  bool openInSimpleFiler = false;
  bool renameInSimpleFiler = false;
  if (selectedCommand >= IdShellMenuFirst &&
      selectedCommand <= IdShellMenuLast) {
    const UINT_PTR verbOffset = selectedCommand - IdShellMenuFirst;
    std::array<wchar_t, 128> canonicalVerb{};
    if (SUCCEEDED(contextMenu->GetCommandString(
            verbOffset, GCS_VERBW, nullptr,
            reinterpret_cast<char *>(canonicalVerb.data()),
            static_cast<UINT>(canonicalVerb.size())))) {
      openInSimpleFiler =
          paths.size() == 1 && _wcsicmp(canonicalVerb.data(), L"open") == 0;
      renameInSimpleFiler =
          paths.size() == 1 && _wcsicmp(canonicalVerb.data(), L"rename") == 0;
    }

    if (!openInSimpleFiler && !renameInSimpleFiler) {
      CMINVOKECOMMANDINFOEX invoke{};
      invoke.cbSize = sizeof(invoke);
      invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
      if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        invoke.fMask |= CMIC_MASK_CONTROL_DOWN;
      invoke.hwnd = window_;
      invoke.lpVerb = MAKEINTRESOURCEA(verbOffset);
      invoke.lpVerbW = MAKEINTRESOURCEW(verbOffset);
      invoke.nShow = SW_SHOWNORMAL;
      invoke.ptInvoke = screenPoint;
      static_cast<void>(contextMenu->InvokeCommand(
          reinterpret_cast<CMINVOKECOMMANDINFO *>(&invoke)));
      RefreshPane(activePane_);
    }
  }

  // Cascading shell extensions can retain command state in the HMENU until
  // InvokeCommand has completed.
  DestroyMenu(menu);
  if (openInSimpleFiler)
    OpenSelected();
  else if (renameInSimpleFiler)
    BeginRename();
  return true;
}

void App::ShowFileMenu(POINT point) {
  const auto paths = SelectedPaths();
  if (paths.empty()) {
    const Pane &pane = panes_[activePane_];
    const std::wstring folder = pane.searchMode ? pane.searchRoot : pane.path;
    if (pane.driveView || folder.empty())
      AppendFallbackBackgroundMenu(point);
    else
      ShowBackgroundShellMenu(folder, point);
    return;
  }
  if (ShowItemShellMenu(paths, point))
    return;

  // Keep the existing commands available if Windows cannot create one shell
  // context menu (for example, search results selected across parent folders).
  HMENU menu = CreatePopupMenu();
  {
    AppendMenuW(menu, MF_STRING, IdOpen, L"開く");

    HMENU applications = CreatePopupMenu();
    bool hasApplication = false;
    for (std::size_t index = 0; index < settings_.links.size(); ++index) {
      const RegisteredLink &link = settings_.links[index];
      if (link.type != LinkType::Application ||
          IdRegisteredAppBase + index > 0x7fff) {
        continue;
      }
      const std::wstring name = Utf8ToWide(link.name);
      AppendMenuW(applications, MF_STRING, IdRegisteredAppBase + index,
                  name.c_str());
      hasApplication = true;
    }
    if (!hasApplication)
      AppendMenuW(applications, MF_STRING | MF_GRAYED, 0,
                  L"登録アプリなし");
    AppendMenuW(menu, MF_POPUP,
                reinterpret_cast<UINT_PTR>(applications),
                L"登録アプリで開く");
    AppendMenuW(menu, MF_STRING, IdAddAppLink, L"アプリを登録...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdCopy, L"コピー");
    AppendMenuW(menu, MF_STRING, IdCut, L"切り取り");
    AppendMenuW(menu, MF_STRING, IdRename, L"名前の変更");
    AppendMenuW(menu, MF_STRING, IdDelete, L"削除");
    AppendMenuW(menu, MF_STRING, IdProperties, L"プロパティ");
    if (paths.size() != 1) {
      EnableMenuItem(menu, IdOpen, MF_BYCOMMAND | MF_GRAYED);
      EnableMenuItem(menu, IdRename, MF_BYCOMMAND | MF_GRAYED);
      EnableMenuItem(menu, IdProperties, MF_BYCOMMAND | MF_GRAYED);
    }
  }

  const UINT selectedCommand =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                     0, window_, nullptr);
  DestroyMenu(menu);
  if (selectedCommand >= IdRegisteredAppBase &&
      selectedCommand - IdRegisteredAppBase < settings_.links.size()) {
    LaunchRegisteredApplication(selectedCommand - IdRegisteredAppBase, false,
                                true);
  } else if (selectedCommand != 0) {
    SendMessageW(window_, WM_COMMAND, selectedCommand, 0);
  }
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
    ++pendingZipOperations_;
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
  ++pendingZipOperations_;
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
  const HWND previousFocus = GetFocus();
  PromptState state{label, initial};
  state.font = uiFont_;
  state.dpi = dpi_;
  RECT owner{};
  GetWindowRect(window_, &owner);
  const int promptWidth =
      MulDiv(430, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  const int promptHeight =
      MulDiv(140, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  HWND prompt =
      CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title.c_str(),
                      WS_POPUP | WS_CAPTION | WS_SYSMENU,
                      owner.left + (owner.right - owner.left - promptWidth) / 2,
                      owner.top + (owner.bottom - owner.top - promptHeight) / 2,
                      promptWidth, promptHeight, window_, nullptr, instance_,
                      &state);
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
  SetActiveWindow(window_);
  SetForegroundWindow(window_);
  if (previousFocus != nullptr && IsWindow(previousFocus) &&
      (previousFocus == window_ || IsChild(window_, previousFocus))) {
    SetFocus(previousFocus);
  } else {
    SetFocus(panes_[activePane_].list);
  }
  return state.accepted ? state.value : std::wstring{};
}

void App::Notify(const std::wstring &message, bool error) {
  SetWindowTextW(status_,
                 (error ? L"⚠  " + message : L"●  " + message).c_str());
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
  case WM_ACTIVATE:
    if (LOWORD(wParam) != WA_INACTIVE)
      PostMessageW(window_, kMessageRestoreFocus, 0, 0);
    break;
  case WM_SETFOCUS:
    PostMessageW(window_, kMessageRestoreFocus, 0, 0);
    break;
  case WM_GETMINMAXINFO: {
    auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
    limits->ptMinTrackSize.x =
        MulDiv(640, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    limits->ptMinTrackSize.y =
        MulDiv(360, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    return 0;
  }
  case WM_DPICHANGED: {
    const auto *suggested = reinterpret_cast<const RECT *>(lParam);
    SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    ApplyDpi(HIWORD(wParam));
    RECT client{};
    GetClientRect(window_, &client);
    LayoutControls(client.right, client.bottom);
    return 0;
  }
  case WM_SIZE:
    LayoutControls(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_ERASEBKGND: {
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(reinterpret_cast<HDC>(wParam), &client, backgroundBrush_);
    return TRUE;
  }
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, backgroundBrush_);

    const int radius =
        MulDiv(12, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    if (IsRectVisible(sidebarCardRect_)) {
      DrawRoundedSurface(dc, sidebarCardRect_, kSidebarColor, kBorderColor,
                         radius);
    }
    for (int index = 0; index < 2; ++index) {
      if (!IsRectVisible(paneCardRects_[index]))
        continue;
      DrawRoundedSurface(
          dc, paneCardRects_[index],
          index == activePane_ ? kActivePaneColor : kSurfaceColor,
          index == activePane_ ? kAccentPressedColor : kBorderColor, radius,
          index == activePane_
              ? MulDiv(2, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI)
              : 1);
    }

    HPEN separator = CreatePen(PS_SOLID, 1, kBorderColor);
    const HGDIOBJ oldPen = SelectObject(dc, separator);
    const int toolbarBottom =
        MulDiv(kToolbarHeight - 1, static_cast<int>(dpi_),
               USER_DEFAULT_SCREEN_DPI);
    MoveToEx(dc, 0, toolbarBottom, nullptr);
    LineTo(dc, client.right, toolbarBottom);
    SelectObject(dc, oldPen);
    DeleteObject(separator);
    EndPaint(window_, &paint);
    return 0;
  }
  case WM_DRAWITEM: {
    auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
    if (item->CtlType == ODT_BUTTON) {
      const bool primary = item->hwndItem == toolbar_[6];
      const bool pressed = (item->itemState & ODS_SELECTED) != 0;
      const bool disabled = (item->itemState & ODS_DISABLED) != 0;
      const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
      FillRect(item->hDC, &item->rcItem, backgroundBrush_);

      RECT button = item->rcItem;
      InflateRect(&button, -1, -1);
      COLORREF fill = kSurfaceColor;
      COLORREF border = kBorderColor;
      COLORREF text = disabled ? kMutedTextColor : kTextColor;
      if (primary) {
        fill = pressed ? kAccentPressedColor : kAccentColor;
        border = fill;
        text = RGB(255, 255, 255);
      } else if (pressed || hot) {
        fill = pressed ? kAccentSoftColor : RGB(239, 246, 255);
        border = RGB(147, 197, 253);
      }
      const int radius =
          MulDiv(9, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      DrawRoundedSurface(item->hDC, button, fill, border, radius);

      wchar_t label[64]{};
      GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
      SetBkMode(item->hDC, TRANSPARENT);
      SetTextColor(item->hDC, text);
      RECT textRect = item->rcItem;
      if (pressed)
        OffsetRect(&textRect, 0, 1);
      DrawTextW(item->hDC, label, -1, &textRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      if ((item->itemState & ODS_FOCUS) != 0) {
        RECT focus = button;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item->hDC, &focus);
      }
      return TRUE;
    }
    if (item->CtlID == IdSidebar && item->CtlType == ODT_LISTBOX) {
      FillRect(item->hDC, &item->rcItem, sidebarBrush_);
      if (item->itemID == static_cast<UINT>(-1))
        return TRUE;

      RECT row = item->rcItem;
      const int horizontalInset =
          MulDiv(4, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      InflateRect(&row, -horizontalInset, -2);
      const bool selected = (item->itemState & ODS_SELECTED) != 0;
      if (selected) {
        const int radius =
            MulDiv(8, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
        DrawRoundedSurface(item->hDC, row, kAccentSoftColor,
                           kAccentSoftColor, radius);
      }

      const LRESULT textLength =
          SendMessageW(sidebar_, LB_GETTEXTLEN, item->itemID, 0);
      std::wstring text(
          textLength > 0 ? static_cast<std::size_t>(textLength + 1) : 1U,
          L'\0');
      SendMessageW(sidebar_, LB_GETTEXT, item->itemID,
                   reinterpret_cast<LPARAM>(text.data()));
      if (textLength > 0)
        text.resize(static_cast<std::size_t>(textLength));
      else
        text.clear();

      SetBkMode(item->hDC, TRANSPARENT);
      SetTextColor(item->hDC, selected ? RGB(30, 64, 175) : kTextColor);
      RECT textRect = row;
      textRect.left +=
          MulDiv(10, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      DrawTextW(item->hDC, text.c_str(), static_cast<int>(text.size()),
                &textRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      if ((item->itemState & ODS_FOCUS) != 0)
        DrawFocusRect(item->hDC, &row);
      return TRUE;
    }
    if (activeShellMenu2_ != nullptr &&
        SUCCEEDED(
            activeShellMenu2_->HandleMenuMsg(message, wParam, lParam))) {
      return 0;
    }
    break;
  }
  case WM_CTLCOLOREDIT: {
    const HWND control = reinterpret_cast<HWND>(lParam);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, kTextColor);
    if (control == searchEdit_) {
      SetBkColor(dc, kSurfaceColor);
      return reinterpret_cast<LRESULT>(surfaceBrush_);
    }
    const int paneIndex = PaneIndexFromControl(control);
    if (control == panes_[paneIndex].address) {
      const bool active = paneIndex == activePane_;
      SetBkColor(dc, active ? kActivePaneColor : kSurfaceColor);
      return reinterpret_cast<LRESULT>(
          active ? activePaneBrush_ : surfaceBrush_);
    }
    break;
  }
  case WM_CTLCOLORLISTBOX: {
    const HWND control = reinterpret_cast<HWND>(lParam);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, kTextColor);
    SetBkColor(dc, control == sidebar_ ? kSidebarColor : kSurfaceColor);
    return reinterpret_cast<LRESULT>(control == sidebar_ ? sidebarBrush_
                                                         : surfaceBrush_);
  }
  case WM_CTLCOLORSTATIC: {
    const HWND control = reinterpret_cast<HWND>(lParam);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetBkMode(dc, TRANSPARENT);
    if (control == status_) {
      SetTextColor(dc, kMutedTextColor);
      return reinterpret_cast<LRESULT>(backgroundBrush_);
    }
    if (control == sidebarTitle_) {
      SetTextColor(dc, kTextColor);
      return reinterpret_cast<LRESULT>(sidebarBrush_);
    }
    break;
  }
  case WM_LBUTTONDOWN: {
    if (twoPanes_) {
      const int splitterWidth =
          MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                 USER_DEFAULT_SCREEN_DPI);
      const int split = paneCardRects_[0].right + splitterWidth / 2;
      if (abs(GET_X_LPARAM(lParam) - split) <= splitterWidth) {
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
      const int splitterWidth =
          MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                 USER_DEFAULT_SCREEN_DPI);
      const int gap =
          MulDiv(10, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      const int paneLeft =
          sidebarVisible_ ? sidebarCardRect_.right + gap : gap;
      const int available =
          client.right - paneLeft - splitterWidth - gap;
      if (available > 0) {
        splitRatio_ = std::clamp(
            static_cast<double>(GET_X_LPARAM(lParam) - paneLeft) /
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
      const int splitterWidth =
          MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                 USER_DEFAULT_SCREEN_DPI);
      POINT point{};
      GetCursorPos(&point);
      ScreenToClient(window_, &point);
      const int split = paneCardRects_[0].right + splitterWidth / 2;
      if (abs(point.x - split) <= splitterWidth) {
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
    if (HIWORD(wParam) == LBN_DBLCLK &&
        command == IdCommandSuggestions) {
      AcceptCommandSuggestion(false, false);
      return 0;
    }
    if (HIWORD(wParam) == EN_CHANGE && command == IdSearchEdit) {
      RebuildCommandSuggestions();
      return 0;
    }
    if (HIWORD(wParam) == EN_SETFOCUS &&
        (command == IdLeftAddress || command == IdRightAddress)) {
      ShowWindow(commandSuggestions_, SW_HIDE);
      commandSuggestionItems_.clear();
      activePane_ = command == IdLeftAddress ? 0 : 1;
      UpdateActivePaneVisuals();
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
      UpdateActivePaneVisuals();
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
      ShowLinkMenu(toolbar_[4]);
      break;
    case IdAddFolderLink:
      AddLinkedFolder();
      break;
    case IdAddFileLink:
      AddLink(false);
      break;
    case IdAddAppLink:
      AddLink(true);
      break;
    case IdTerminal:
      ShowTerminalMenu(toolbar_[5]);
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
      if (ParseCommandQuery(GetWindowTextString(searchEdit_)).mode !=
          CommandMode::None) {
        AcceptCommandSuggestion(
            (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      } else if (panes_[activePane_].searchMode &&
                 panes_[activePane_].busy) {
        RetireWorker(panes_[activePane_]);
        ++panes_[activePane_].generation;
        panes_[activePane_].busy = false;
        SetWindowTextW(toolbar_[6], L"実行");
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
    case IdCopyToOther:
      TransferSelectionToOtherPane(false);
      break;
    case IdMoveToOther:
      TransferSelectionToOtherPane(true);
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
        UpdateActivePaneVisuals();
        SetWindowTextW(searchEdit_,
                       panes_[activePane_].searchMode
                           ? panes_[activePane_].searchQuery.c_str()
                           : L"");
        SetWindowTextW(toolbar_[6], panes_[activePane_].searchMode &&
                                             panes_[activePane_].busy
                                         ? L"中止"
                                         : L"実行");
        SetFocus(panes_[activePane_].list);
      }
      break;
    case IdRemoveSidebar:
      RemoveSidebarItem();
      break;
    case IdEditSidebar:
      EditSidebarItem();
      break;
    case IdOpenSidebar:
      ActivateSidebarItem(false);
      break;
    case IdOpenSidebarAdmin:
      ActivateSidebarItem(true);
      break;
    case IdMoveSidebarUp:
      MoveSidebarItem(true);
      break;
    case IdMoveSidebarDown:
      MoveSidebarItem(false);
      break;
    }
    PostMessageW(window_, kMessageRestoreFocus, 0, 0);
    return 0;
  }
  case WM_NOTIFY: {
    const auto *header = reinterpret_cast<NMHDR *>(lParam);
    const int paneIndex = PaneIndexFromControl(header->hwndFrom);
    if (header->hwndFrom == panes_[paneIndex].list) {
      if (header->code == NM_SETFOCUS) {
        activePane_ = paneIndex;
        UpdateActivePaneVisuals();
        SetWindowTextW(searchEdit_,
                       panes_[activePane_].searchMode
                           ? panes_[activePane_].searchQuery.c_str()
                           : L"");
        SetWindowTextW(toolbar_[6], panes_[activePane_].searchMode &&
                                             panes_[activePane_].busy
                                         ? L"中止"
                                         : L"実行");
        Notify(paneIndex == 0 ? L"左ペイン" : L"右ペイン");
      } else if (header->code == NM_DBLCLK) {
        activePane_ = paneIndex;
        UpdateActivePaneVisuals();
        OpenSelected();
      } else if (header->code == NM_RCLICK) {
        activePane_ = paneIndex;
        UpdateActivePaneVisuals();
        const auto *click = reinterpret_cast<const NMITEMACTIVATE *>(lParam);
        Pane &pane = panes_[paneIndex];
        if (click->iItem >= 0) {
          const UINT state =
              ListView_GetItemState(pane.list, click->iItem, LVIS_SELECTED);
          if ((state & LVIS_SELECTED) == 0) {
            ListView_SetItemState(pane.list, -1, 0,
                                  LVIS_SELECTED | LVIS_FOCUSED);
          }
          ListView_SetItemState(pane.list, click->iItem,
                                LVIS_SELECTED | LVIS_FOCUSED,
                                LVIS_SELECTED | LVIS_FOCUSED);
          ListView_SetSelectionMark(pane.list, click->iItem);
        } else {
          ListView_SetItemState(pane.list, -1, 0,
                                LVIS_SELECTED | LVIS_FOCUSED);
          ListView_SetSelectionMark(pane.list, -1);
        }
        POINT point{};
        GetCursorPos(&point);
        ShowFileMenu(point);
        PostMessageW(window_, kMessageRestoreFocus, 0, 0);
      } else if (header->code == LVN_KEYDOWN) {
        const auto *key = reinterpret_cast<NMLVKEYDOWN *>(lParam);
        if (key->wVKey == VK_RETURN) {
          activePane_ = paneIndex;
          UpdateActivePaneVisuals();
          OpenSelected();
        }
      } else if (header->code == LVN_COLUMNCLICK) {
        auto *click = reinterpret_cast<NMLISTVIEW *>(lParam);
        Pane &pane = panes_[paneIndex];
        constexpr std::array<int, 3> sortColumns{0, 2, 3};
        const int sortColumn = sortColumns[click->iSubItem];
        if (pane.sortColumn == sortColumn)
          pane.sortAscending = !pane.sortAscending;
        else {
          pane.sortColumn = sortColumn;
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
              text = item.IsDirectory() ? L"" : FormatFileSize(item.size);
              break;
            case 2:
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
            ++pendingFileOperations_;
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
      bool canMoveUp = false;
      bool canMoveDown = false;
      if (selected >= 0 && selected < static_cast<int>(sidebarMap_.size())) {
        const auto [bookmark, index] = sidebarMap_[selected];
        canRunAsAdmin =
            !bookmark && settings_.links[index].type == LinkType::Application;
        const std::size_t sectionSize =
            bookmark ? settings_.bookmarks.size() : settings_.links.size();
        canMoveUp = index > 0;
        canMoveDown = index + 1 < sectionSize;
      }
      AppendMenuW(menu, MF_STRING | (canRunAsAdmin ? 0 : MF_GRAYED),
                  IdOpenSidebarAdmin, L"管理者として実行");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IdEditSidebar, L"登録を編集");
      AppendMenuW(menu, MF_STRING, IdRemoveSidebar, L"登録を削除");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING | (canMoveUp ? 0 : MF_GRAYED),
                  IdMoveSidebarUp, L"上へ移動");
      AppendMenuW(menu, MF_STRING | (canMoveDown ? 0 : MF_GRAYED),
                  IdMoveSidebarDown, L"下へ移動");
      TrackPopupMenu(menu, TPM_RIGHTBUTTON, GET_X_LPARAM(lParam),
                     GET_Y_LPARAM(lParam), 0, window_, nullptr);
      DestroyMenu(menu);
      PostMessageW(window_, kMessageRestoreFocus, 0, 0);
      return 0;
    }
    break;
  case kMessageCommandType:
    return HandleCommandPrefixCharacter(
               static_cast<wchar_t>(wParam),
               reinterpret_cast<HWND>(lParam))
               ? TRUE
               : FALSE;
  case kMessageCommandAccept:
    AcceptCommandSuggestion((wParam & 1U) != 0, (wParam & 2U) != 0);
    return 0;
  case kMessageCommandMove:
    MoveCommandSelection(static_cast<int>(static_cast<INT_PTR>(lParam)));
    return 0;
  case kMessageCommandDismiss:
    DismissCommandSuggestions(true);
    return 0;
  case kMessageCommandNew:
    AddCommandRegistration();
    return 0;
  case kMessageSidebarMove:
    if (reinterpret_cast<HWND>(lParam) == sidebar_)
      MoveSidebarItem(wParam != 0);
    return 0;
  case kMessageRestoreFocus:
    RestorePaneFocusIfNeeded();
    return 0;
  case kMessageNavigateAddress: {
    const int paneIndex = static_cast<int>(wParam);
    activePane_ = paneIndex;
    UpdateActivePaneVisuals();
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
        SetWindowTextW(toolbar_[6], L"実行");
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
    if (pendingFileOperations_ > 0)
      --pendingFileOperations_;
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
    SetFocus(panes_[activePane_].list);
    return 0;
  }
  case kMessageZipDone: {
    std::unique_ptr<ZipResult> result(reinterpret_cast<ZipResult *>(lParam));
    if (pendingZipOperations_ > 0)
      --pendingZipOperations_;
    if (result->success) {
      Notify(result->message);
      RefreshPane(activePane_);
    } else {
      Notify(result->message, true);
    }
    SetFocus(panes_[activePane_].list);
    return 0;
  }
  case WM_CLOSE:
    if (pendingFileOperations_ + pendingZipOperations_ > 0) {
      const std::wstring closeWarning =
          std::format(L"ファイル処理が {} 件進行中です。\n"
                      L"終了すると処理が途中で止まる可能性があります。\n\n"
                      L"それでも終了しますか？",
                      pendingFileOperations_ + pendingZipOperations_);
      if (MessageBoxW(window_, closeWarning.c_str(), L"SimpleFiler を終了",
                      MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        PostMessageW(window_, kMessageRestoreFocus, 0, 0);
        return 0;
      }
    }
    SaveSettings();
    DestroyWindow(window_);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case WM_INITMENUPOPUP:
  case WM_UNINITMENUPOPUP:
  case WM_MEASUREITEM:
    // activeShellMenu2_ is only non-null while a shell TrackPopupMenu call is
    // modal, so no other popup menu can be active at the same time.
    if (activeShellMenu2_ != nullptr &&
        SUCCEEDED(activeShellMenu2_->HandleMenuMsg(message, wParam, lParam))) {
      return 0;
    }
    break;
  case WM_MENUCHAR: {
    if (activeShellMenu3_ != nullptr) {
      LRESULT result = 0;
      if (SUCCEEDED(activeShellMenu3_->HandleMenuMsg2(message, wParam, lParam,
                                                       &result))) {
        return result;
      }
    }
    break;
  }
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace sf::win
