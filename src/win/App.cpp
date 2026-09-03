#include "win/App.h"

#include "core/AppArguments.h"
#include "win/AddressBar.h"
#include "win/AppMessages.h"
#include "win/DriveInfo.h"
#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace sf::win {
namespace {

constexpr wchar_t kPromptClass[] = L"SimpleFiler.PromptWindow";
constexpr wchar_t kShortcutsClass[] = L"SimpleFiler.ShortcutsWindow";
constexpr wchar_t kCommandCueText[] =
    L"コマンド / 検索  (ff フォルダー・aa アプリ・cmd 端末)";
constexpr int kToolbarHeight = 50;
constexpr int kAddressHeight = 22;
constexpr int kStatusHeight = 30;
constexpr int kDriveCapacityWidth = 240;
constexpr int kSidebarWidth = 165;
constexpr int kSplitterWidth = 10;
constexpr UINT_PTR kDriveCapacityTimerId = 1;
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
  IdDown,
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
  IdSelectAll,
  IdPromptEdit = 400,
  // WM_SYSCOMMAND requires the low 4 bits of a custom command id to be zero.
  IdShowAbout = 416,
  IdShowShortcuts = 432,
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
    HWND label = CreateWindowExW(
        0, L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE, scale(12),
        scale(12), scale(396), scale(20), window, nullptr, nullptr, nullptr);
    state->edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, scale(12),
        scale(36), scale(396), scale(25), window,
        reinterpret_cast<HMENU>(IdPromptEdit), nullptr, nullptr);
    HWND ok =
        CreateWindowExW(0, L"BUTTON", L"OK",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        scale(236), scale(72), scale(82), scale(27), window,
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

struct ShortcutEntry final {
  const wchar_t *action;
  const wchar_t *keys;
};

constexpr ShortcutEntry kShortcutEntries[] = {
    {L"コピー／切り取り／貼り付け", L"Ctrl+C / Ctrl+X / Ctrl+V"},
    {L"すべて選択", L"Ctrl+A"},
    {L"反対ペインへコピー／移動", L"F5 / F6"},
    {L"新しいフォルダー", L"Ctrl+N / F7"},
    {L"名前変更", L"F2"},
    {L"ごみ箱へ削除／完全削除", L"Delete / Shift+Delete"},
    {L"更新", L"Ctrl+R"},
    {L"アドレスバー", L"Ctrl+L"},
    {L"検索", L"Ctrl+F"},
    {L"隠しファイル／フォルダーの表示切替", L"Ctrl+H"},
    {L"ペイン切り替え", L"Tab"},
    {L"戻る／進む／上へ", L"Alt+← / Alt+→ / Alt+↑ または Backspace"},
};

constexpr int IdShortcutsList = 1;

struct ShortcutsState final {
  HFONT font = nullptr;
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
  HWND list = nullptr;
  HWND ok = nullptr;
};

void LayoutShortcutsDialog(HWND window, ShortcutsState *state) {
  const auto scale = [state](int value) {
    return MulDiv(value, static_cast<int>(state->dpi), USER_DEFAULT_SCREEN_DPI);
  };
  RECT client{};
  GetClientRect(window, &client);
  const int okHeight = scale(27);
  const int okWidth = scale(82);
  const int margin = scale(12);
  MoveWindow(state->list, margin, margin, client.right - margin * 2,
            client.bottom - margin * 3 - okHeight, TRUE);
  MoveWindow(state->ok, client.right - margin - okWidth,
            client.bottom - margin - okHeight, okWidth, okHeight, TRUE);
}

LRESULT CALLBACK ShortcutsProcedure(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
  auto *state = reinterpret_cast<ShortcutsState *>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    state = static_cast<ShortcutsState *>(create->lpCreateParams);
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
    state->list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
            LVS_NOSORTHEADER,
        0, 0, 10, 10, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdShortcutsList)),
        nullptr, nullptr);
    SendMessageW(state->list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT);
    SetWindowTheme(state->list, L"Explorer", nullptr);
    const std::array<std::pair<const wchar_t *, int>, 2> columns{
        {{L"操作", 230}, {L"キー", 260}}};
    for (int column = 0; column < static_cast<int>(columns.size()); ++column) {
      LVCOLUMNW value{};
      value.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      value.pszText = const_cast<wchar_t *>(columns[column].first);
      value.cx = scale(columns[column].second);
      value.iSubItem = column;
      ListView_InsertColumn(state->list, column, &value);
    }
    for (int row = 0; row < static_cast<int>(std::size(kShortcutEntries));
        ++row) {
      LVITEMW item{};
      item.mask = LVIF_TEXT;
      item.iItem = row;
      item.iSubItem = 0;
      item.pszText = const_cast<wchar_t *>(kShortcutEntries[row].action);
      ListView_InsertItem(state->list, &item);
      ListView_SetItemText(state->list, row, 1,
                           const_cast<wchar_t *>(kShortcutEntries[row].keys));
    }
    state->ok = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 10, 10,
        window, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    SendMessageW(state->ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    LayoutShortcutsDialog(window, state);
    SetFocus(state->ok);
    return 0;
  }
  case WM_SIZE:
    if (state != nullptr)
      LayoutShortcutsDialog(window, state);
    break;
  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
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

LRESULT CALLBACK SearchEditSubclass(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
  if (message == WM_PAINT) {
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

std::wstring ApplicationVersionString() {
  const std::wstring executablePath = ExecutablePath().wstring();
  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(executablePath.c_str(), &handle);
  if (size == 0)
    return L"不明";
  std::vector<std::byte> buffer(size);
  if (!GetFileVersionInfoW(executablePath.c_str(), handle, size,
                           buffer.data())) {
    return L"不明";
  }
  void *value = nullptr;
  UINT valueSize = 0;
  if (!VerQueryValueW(buffer.data(), LR"(\StringFileInfo\041104B0\FileVersion)",
                      &value, &valueSize) ||
      value == nullptr || valueSize == 0) {
    return L"不明";
  }
  return std::wstring(static_cast<const wchar_t *>(value));
}

thread_local HWND g_messageBoxCenterParent = nullptr;

LRESULT CALLBACK CenterMessageBoxHookProcedure(int code, WPARAM wParam,
                                               LPARAM lParam) {
  if (code == HCBT_ACTIVATE && g_messageBoxCenterParent != nullptr) {
    const HWND dialog = reinterpret_cast<HWND>(wParam);
    RECT parentRect{};
    RECT dialogRect{};
    if (GetWindowRect(g_messageBoxCenterParent, &parentRect) &&
        GetWindowRect(dialog, &dialogRect)) {
      const int dialogWidth = dialogRect.right - dialogRect.left;
      const int dialogHeight = dialogRect.bottom - dialogRect.top;
      const int x = parentRect.left +
                    ((parentRect.right - parentRect.left) - dialogWidth) / 2;
      const int y = parentRect.top +
                    ((parentRect.bottom - parentRect.top) - dialogHeight) / 2;
      SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

int ShowMessageBoxCenteredOnParent(HWND parent, const std::wstring &text,
                                   const std::wstring &title, UINT type) {
  g_messageBoxCenterParent = parent;
  const HHOOK hook = SetWindowsHookExW(WH_CBT, CenterMessageBoxHookProcedure,
                                       nullptr, GetCurrentThreadId());
  const int result = MessageBoxW(parent, text.c_str(), title.c_str(), type);
  if (hook != nullptr)
    UnhookWindowsHookEx(hook);
  g_messageBoxCenterParent = nullptr;
  return result;
}

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

void DrawDropDownIndicator(HDC dc, int centerX, int centerY, COLORREF color,
                           UINT dpi) {
  const int width =
      MulDiv(12, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
  const int height =
      MulDiv(7, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
  const std::array<POINT, 3> points{{
      {centerX - width / 2, centerY - height / 2},
      {centerX + (width + 1) / 2, centerY - height / 2},
      {centerX, centerY + (height + 1) / 2},
  }};
  HBRUSH brush = CreateSolidBrush(color);
  const HGDIOBJ oldBrush = SelectObject(dc, brush);
  const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
  Polygon(dc, points.data(), static_cast<int>(points.size()));
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(brush);
}

bool IsRectVisible(const RECT &rect) {
  return rect.right > rect.left && rect.bottom > rect.top;
}

} // namespace

App::App(HINSTANCE instance)
    : instance_(instance), backgroundBrush_(CreateSolidBrush(kBackgroundColor)),
      surfaceBrush_(CreateSolidBrush(kSurfaceColor)),
      sidebarBrush_(CreateSolidBrush(kSidebarColor)),
      activePaneBrush_(CreateSolidBrush(kActivePaneColor)),
      settingsStore_(ExecutablePath().parent_path() / L"simplefiler.json") {}

App::~App() {
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
    const bool paneHasFocus = focus == paneController_.ListHandle(0) ||
                              focus == paneController_.ListHandle(1);
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
  mainClass.lpszClassName = kMainWindowClass;
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
  if (!RegisterClassExW(&promptClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  WNDCLASSEXW shortcutsClass{};
  shortcutsClass.cbSize = sizeof(shortcutsClass);
  shortcutsClass.lpfnWndProc = ShortcutsProcedure;
  shortcutsClass.hInstance = instance_;
  shortcutsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  shortcutsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  shortcutsClass.lpszClassName = kShortcutsClass;
  return RegisterClassExW(&shortcutsClass) != 0 ||
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
  window_ = CreateWindowExW(
      0, kMainWindowClass, L"SimpleFiler",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, settings_.windowWidth,
      settings_.windowHeight, nullptr, nullptr, instance_, this);
  if (window_ == nullptr)
    return false;
  const HMENU systemMenu = GetSystemMenu(window_, FALSE);
  if (systemMenu != nullptr) {
    AppendMenuW(systemMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(systemMenu, MF_STRING, IdShowShortcuts, L"キーボードショートカット");
    AppendMenuW(systemMenu, MF_STRING, IdShowAbout, L"SimpleFiler について");
  }
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
  const HFONT font = uiFont_ != nullptr
                         ? uiFont_
                         : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const std::array<std::pair<const wchar_t *, int>, 6> buttons{
      {{L"PC", IdDrives},
       {L"1｜2", IdTogglePanes},
       {L"サイド", IdToggleSidebar},
       {L"★ 追加", IdAddBookmark},
       {L"リンク", IdAddLink},
       {L"端末", IdTerminal}}};
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
                 MulDiv(6, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI)));
  SetWindowSubclass(searchEdit_, SearchEditSubclass, 1, 0);
  toolbar_[6] = CreateWindowExW(
      0, L"BUTTON", L"実行", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0, 10, 10, window_, reinterpret_cast<HMENU>(IdSearch), instance_,
      nullptr);
  SendMessageW(toolbar_[6], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

  sidebarTitle_ = CreateWindowExW(0, L"STATIC", L"★  クイックアクセス",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                                  window_, nullptr, instance_, nullptr);
  SendMessageW(
      sidebarTitle_, WM_SETFONT,
      reinterpret_cast<WPARAM>(sectionFont_ != nullptr ? sectionFont_ : font),
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
      WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10,
      window_, reinterpret_cast<HMENU>(IdCommandSuggestions), instance_,
      nullptr);
  SendMessageW(commandSuggestions_, WM_SETFONT, reinterpret_cast<WPARAM>(font),
               TRUE);
  SetWindowSubclass(commandSuggestions_, SuggestionSubclass, 1, 0);
  status_ = CreateWindowExW(0, L"STATIC", L"●  準備完了",
                            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                            window_, nullptr, instance_, nullptr);
  SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  driveCapacity_ = CreateWindowExW(0, L"STATIC", L"",
                                   WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10,
                                   10, window_, nullptr, instance_, nullptr);
  SendMessageW(driveCapacity_, WM_SETFONT, reinterpret_cast<WPARAM>(font),
               TRUE);

  SHFILEINFOW shellInfo{};
  HIMAGELIST images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
      L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &shellInfo, sizeof(shellInfo),
      SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
  if (images != nullptr) {
    ListView_SetImageList(paneController_.ListHandle(0), images, LVSIL_SMALL);
    ListView_SetImageList(paneController_.ListHandle(1), images, LVSIL_SMALL);
  }

  RECT client{};
  GetClientRect(window_, &client);
  LayoutControls(client.right, client.bottom);
  sidebarController_.RebuildSidebar(sidebar_, settings_);
  UpdateActivePaneVisuals();
  SetTimer(window_, kDriveCapacityTimerId, 1500, nullptr);
}

void App::CreatePaneControls(int paneIndex) {
  const HFONT font = uiFont_ != nullptr
                         ? uiFont_
                         : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const HWND address = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
      window_,
      reinterpret_cast<HMENU>(paneIndex == 0 ? IdLeftAddress : IdRightAddress),
      instance_, nullptr);
  SendMessageW(address, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessageW(address, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELPARAM(MulDiv(6, static_cast<int>(dpi_),
                                 USER_DEFAULT_SCREEN_DPI),
                          MulDiv(6, static_cast<int>(dpi_),
                                 USER_DEFAULT_SCREEN_DPI)));
  AttachAddressBar(address, paneIndex);

  const HWND list = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHOWSELALWAYS | LVS_EDITLABELS,
      0, 0, 10, 10, window_,
      reinterpret_cast<HMENU>(paneIndex == 0 ? IdLeftList : IdRightList),
      instance_, nullptr);
  SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SetWindowSubclass(list, ListSubclass, 1, 0);
  ListView_SetExtendedListViewStyle(
      list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                LVS_EX_HEADERDRAGDROP);
  ListView_SetTextColor(list, kTextColor);
  SetWindowTheme(list, L"Explorer", nullptr);
  const std::array<std::pair<const wchar_t *, int>, 3> columns{
      {{L"名前", 300}, {L"サイズ", 100}, {L"更新日時", 140}}};
  for (int column = 0; column < static_cast<int>(columns.size()); ++column) {
    LVCOLUMNW value{};
    value.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    value.pszText = const_cast<wchar_t *>(columns[column].first);
    value.cx = MulDiv(columns[column].second, static_cast<int>(dpi_),
                      USER_DEFAULT_SCREEN_DPI);
    value.iSubItem = column;
    ListView_InsertColumn(list, column, &value);
  }
  const HWND listHeader = ListView_GetHeader(list);
  if (listHeader != nullptr && sectionFont_ != nullptr) {
    SendMessageW(listHeader, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_),
                 TRUE);
  }
  paneController_.AttachControls(paneIndex, address, list);

  dropTargets_[paneIndex] = new PaneDropTarget(
      paneIndex, &dragSourcePane_,
      [this](int pane) { return paneController_.IsDriveView(pane); },
      [this](int pane) { return paneController_.EffectivePath(pane); },
      [this](const std::vector<std::wstring> &paths,
             const std::wstring &destination, bool move) {
        fileOperationController_.TransferSelectionToOtherPane(
            window_, paths, destination, /*twoPanes=*/true, move,
            [this](const std::wstring &message, bool error) {
              Notify(message, error);
            });
      });
  if (FAILED(RegisterDragDrop(list, dropTargets_[paneIndex]))) {
    // Drag-and-drop onto this pane silently won't work; other functionality
    // is unaffected, and status_ does not exist yet at this point in
    // startup, so this is surfaced via debug output rather than Notify().
    OutputDebugStringW(L"SimpleFiler: RegisterDragDrop failed for pane\n");
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
  const int searchHeight = addressHeight;
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
               sidebarCardRect_.top + scale(7), sidebarWidth - scale(24),
               scale(20), TRUE);
    MoveWindow(
        sidebar_, sidebarCardRect_.left + cardInset,
        sidebarCardRect_.top + sidebarTitleHeight, sidebarWidth - cardInset * 2,
        std::max(0, contentHeight - sidebarTitleHeight - cardInset), TRUE);
  } else {
    sidebarCardRect_ = {};
  }
  const int paneLeft = sidebarVisible_ ? gap + sidebarWidth + gap : gap;
  const int paneWidth = std::max(0, width - paneLeft - gap);
  int leftWidth = paneWidth;
  int rightLeft = width;
  int rightWidth = 0;
  if (twoPanes_) {
    leftWidth = static_cast<int>((paneWidth - splitterWidth) * splitRatio_);
    rightLeft = paneLeft + leftWidth + splitterWidth;
    rightWidth = width - rightLeft - gap;
  }

  // In two-pane mode pane 0 is always the left pane. In single-pane mode,
  // show whichever pane is currently active instead of always pane 0.
  const int fullPane = twoPanes_ ? 0 : activePane_;
  const int otherPane = 1 - fullPane;
  // fullPane may have been hidden earlier as the "other" pane while in
  // single-pane mode with the opposite pane active, so show it explicitly.
  ShowWindow(paneController_.AddressHandle(fullPane), SW_SHOW);
  ShowWindow(paneController_.ListHandle(fullPane), SW_SHOW);
  paneCardRects_[fullPane] = {paneLeft, contentTop, paneLeft + leftWidth,
                              contentBottom};
  MoveWindow(paneController_.AddressHandle(fullPane), paneLeft + cardInset,
             contentTop + cardInset, std::max(0, leftWidth - cardInset * 2),
             addressHeight, TRUE);
  MoveWindow(
      paneController_.ListHandle(fullPane), paneLeft + cardInset,
      contentTop + cardInset + addressHeight + scale(5),
      std::max(0, leftWidth - cardInset * 2),
      std::max(0, contentHeight - addressHeight - cardInset * 2 - scale(5)),
      TRUE);
  ShowWindow(paneController_.AddressHandle(otherPane),
             twoPanes_ ? SW_SHOW : SW_HIDE);
  ShowWindow(paneController_.ListHandle(otherPane),
             twoPanes_ ? SW_SHOW : SW_HIDE);
  if (twoPanes_) {
    paneCardRects_[otherPane] = {rightLeft, contentTop, rightLeft + rightWidth,
                                 contentBottom};
    MoveWindow(paneController_.AddressHandle(otherPane), rightLeft + cardInset,
               contentTop + cardInset, std::max(0, rightWidth - cardInset * 2),
               addressHeight, TRUE);
    MoveWindow(
        paneController_.ListHandle(otherPane), rightLeft + cardInset,
        contentTop + cardInset + addressHeight + scale(5),
        std::max(0, rightWidth - cardInset * 2),
        std::max(0, contentHeight - addressHeight - cardInset * 2 - scale(5)),
        TRUE);
  } else {
    paneCardRects_[otherPane] = {};
  }
  const int driveCapacityWidth = std::max(
      0, std::min(scale(kDriveCapacityWidth), (width - gap * 2 - scale(8)) / 3));
  const int statusTop = height - statusHeight + scale(5);
  const int statusRowHeight = statusHeight - scale(5);
  MoveWindow(status_, gap + scale(4), statusTop,
             std::max(0, width - gap * 2 - scale(8) - driveCapacityWidth),
             statusRowHeight, TRUE);
  MoveWindow(driveCapacity_,
             gap + scale(4) + std::max(0, width - gap * 2 - scale(8) -
                                               driveCapacityWidth),
             statusTop, driveCapacityWidth, statusRowHeight, TRUE);
  // MoveWindow's own repaint only invalidates the delta between the old and
  // new rect, not the whole client area. LVS_EX_DOUBLEBUFFER caches a
  // full-client offscreen bitmap, so a partial invalidate leaves stale
  // glyphs from the previous width composited into the new layout (seen as
  // truncated/overlapping file names after toggling the sidebar). Forcing a
  // full-rect invalidate here fixes it for every caller of LayoutControls
  // (WM_SIZE, WM_DPICHANGED, splitter drag, pane/sidebar toggle).
  InvalidateRect(paneController_.ListHandle(0), nullptr, TRUE);
  InvalidateRect(paneController_.ListHandle(1), nullptr, TRUE);
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
    for (HWND control :
         {searchEdit_, commandSuggestions_, sidebar_, status_,
          paneController_.AddressHandle(0), paneController_.ListHandle(0),
          paneController_.AddressHandle(1), paneController_.ListHandle(1)}) {
      if (control != nullptr)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont),
                     TRUE);
    }
    for (HWND control : {sidebarTitle_}) {
      if (control != nullptr)
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(
                         newSectionFont != nullptr ? newSectionFont : newFont),
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
                 MulDiv(6, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI)));
  for (int pane = 0; pane < 2; ++pane) {
    SendMessageW(
        paneController_.AddressHandle(pane), EM_SETMARGINS,
        EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(MulDiv(6, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI),
                   MulDiv(6, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI)));
  }
  const std::array<int, 3> columnWidths{300, 100, 140};
  for (int pane = 0; pane < 2; ++pane) {
    for (int column = 0; column < static_cast<int>(columnWidths.size());
         ++column) {
      ListView_SetColumnWidth(paneController_.ListHandle(pane), column,
                              MulDiv(columnWidths[column],
                                     static_cast<int>(dpi_),
                                     USER_DEFAULT_SCREEN_DPI));
    }
  }
}

void App::UpdateActivePaneVisuals() {
  for (int index = 0; index < 2; ++index) {
    const COLORREF background =
        index == activePane_ ? kActivePaneColor : kSurfaceColor;
    ListView_SetBkColor(paneController_.ListHandle(index), background);
    ListView_SetTextBkColor(paneController_.ListHandle(index), background);
    InvalidateRect(paneController_.ListHandle(index), nullptr, FALSE);
    InvalidateRect(paneController_.AddressHandle(index), nullptr, TRUE);
  }
  InvalidateRect(window_, nullptr, FALSE);
  UpdateDriveCapacityDisplay();
}

void App::UpdatePaneSearchState(int pane, bool searchMode, bool busy) {
  if (pane == activePane_)
    SetWindowTextW(toolbar_[6], searchMode && busy ? L"中止" : L"実行");
}

void App::RestorePaneFocusIfNeeded() {
  if (!IsWindowEnabled(window_) || GetActiveWindow() != window_)
    return;
  const HWND focus = GetFocus();
  if (focus == nullptr || focus == window_ || !IsChild(window_, focus))
    SetFocus(paneController_.ListHandle(activePane_));
}

void App::CreateAccelerators() {
  const ACCEL values[] = {{FVIRTKEY | FCONTROL, 'C', IdCopy},
                          {FVIRTKEY | FCONTROL, 'X', IdCut},
                          {FVIRTKEY | FCONTROL, 'V', IdPaste},
                          {FVIRTKEY | FCONTROL, 'A', IdSelectAll},
                          {FVIRTKEY, VK_DELETE, IdDelete},
                          {FVIRTKEY | FSHIFT, VK_DELETE, IdPermanentDelete},
                          {FVIRTKEY, VK_F2, IdRename},
                          {FVIRTKEY | FCONTROL, 'R', IdRefresh},
                          {FVIRTKEY | FCONTROL, 'L', IdFocusAddress},
                          {FVIRTKEY | FCONTROL, 'F', IdFocusSearch},
                          {FVIRTKEY | FCONTROL, 'H', IdShowHidden},
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
  for (int index = 0; index < 2; ++index)
    paneController_.ApplySettings(index, settings_.panes[index]);
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
  const auto notify = [this](const std::wstring &message, bool error) {
    Notify(message, error);
  };
  const auto searchState = [this](int pane, bool searchMode, bool busy) {
    UpdatePaneSearchState(pane, searchMode, busy);
  };
  paneController_.Navigate(window_, 0, left, true, notify, searchState);
  paneController_.Navigate(window_, 1, right, true, notify, searchState);
  SetFocus(paneController_.ListHandle(0));
  if (!fileToOpen.empty() &&
      !OpenPath(window_, fileToOpen, {},
                std::filesystem::path(fileToOpen).parent_path().wstring()))
    Notify(L"指定されたファイルを開けません", true);
}

void App::OpenExternalPath(const std::wstring &path) {
  if (IsIconic(window_))
    ShowWindow(window_, SW_RESTORE);
  SetForegroundWindow(window_);
  if (path.empty())
    return;
  std::wstring target = path;
  std::error_code pathError;
  if (std::filesystem::is_regular_file(target, pathError)) {
    const std::wstring parent =
        std::filesystem::path(target).parent_path().wstring();
    if (!parent.empty() && IsDirectory(parent))
      NavigatePane(activePane_, parent);
    if (!OpenPath(window_, target, {}, parent))
      Notify(L"指定されたファイルを開けません", true);
    return;
  }
  if (IsDirectory(target)) {
    NavigatePane(activePane_, target);
    return;
  }
  Notify(L"指定されたパスが見つかりません", true);
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
  for (int index = 0; index < 2; ++index)
    paneController_.WriteSettings(index, settings_.panes[index]);
  if (!settingsWritable_) {
    Notify(L"設定フォルダーへ書き込めないため保存しませんでした", true);
    return;
  }
  std::string error;
  if (!settingsStore_.Save(settings_, &error))
    Notify(Utf8ToWide(error), true);
}

AppArgumentContext App::BuildAppArgumentContext(bool includeSelection) const {
  AppArgumentContext context;
  if (includeSelection)
    context.files = paneController_.SelectedPaths(activePane_);
  context.folder = paneController_.EffectivePath(activePane_);
  context.left = paneController_.EffectivePath(0);
  context.right = paneController_.EffectivePath(1);
  context.other = paneController_.EffectivePath(1 - activePane_);
  return context;
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
  HWND prompt = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kPromptClass, title.c_str(),
      WS_POPUP | WS_CAPTION | WS_SYSMENU,
      owner.left + (owner.right - owner.left - promptWidth) / 2,
      owner.top + (owner.bottom - owner.top - promptHeight) / 2, promptWidth,
      promptHeight, window_, nullptr, instance_, &state);
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
    SetFocus(paneController_.ListHandle(activePane_));
  }
  return state.accepted ? state.value : std::wstring{};
}

void App::Notify(const std::wstring &message, bool error) {
  SetWindowTextW(status_,
                 (error ? L"⚠  " + message : L"●  " + message).c_str());
}

void App::UpdateDriveCapacityDisplay() {
  // Left blank both while showing the drive list (no single drive applies)
  // and when the query fails (unreachable drive); the two are not
  // distinguished in the UI.
  std::wstring text;
  if (!paneController_.IsDriveView(activePane_)) {
    const std::wstring path = paneController_.EffectivePath(activePane_);
    if (const auto capacity = QueryDriveCapacity(path))
      text = FormatDriveCapacity(*capacity);
  }
  SetWindowTextW(driveCapacity_, text.c_str());
}

void App::ShowAboutDialog() {
  const std::wstring message =
      L"SimpleFiler " + ApplicationVersionString() +
      L"\n\n"
      L"Copyright (c) 2026 beidaochuan\n"
      L"MIT License\n\n"
      L"このソフトウェアは以下のライブラリを使用しています:\n"
      L"- minizip-ng (zlib license)\n"
      L"- zlib-ng (zlib license)\n\n"
      L"詳細は LICENSE および THIRD_PARTY_NOTICES.md を参照してください。";
  ShowMessageBoxCenteredOnParent(window_, message, L"SimpleFiler について",
                                 MB_OK | MB_ICONINFORMATION);
}

void App::ShowShortcutsDialog() {
  const HWND previousFocus = GetFocus();
  ShortcutsState state{uiFont_, dpi_};
  RECT owner{};
  GetWindowRect(window_, &owner);
  const int dialogWidth =
      MulDiv(520, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  const int dialogHeight =
      MulDiv(320, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kShortcutsClass, L"キーボードショートカット",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      owner.left + (owner.right - owner.left - dialogWidth) / 2,
      owner.top + (owner.bottom - owner.top - dialogHeight) / 2, dialogWidth,
      dialogHeight, window_, nullptr, instance_, &state);
  if (dialog == nullptr)
    return;
  EnableWindow(window_, FALSE);
  ShowWindow(dialog, SW_SHOW);
  MSG message{};
  while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dialog, &message)) {
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
    SetFocus(paneController_.ListHandle(activePane_));
  }
}

void App::NavigatePane(int pane, const std::wstring &path, bool addHistory) {
  paneController_.Navigate(
      window_, pane, path, addHistory,
      [this](const std::wstring &message, bool error) {
        Notify(message, error);
      },
      [this](int changedPane, bool searchMode, bool busy) {
        UpdatePaneSearchState(changedPane, searchMode, busy);
      });
}

void App::RefreshPaneView(int pane) {
  paneController_.RefreshPane(
      window_, pane,
      [this](const std::wstring &message, bool error) {
        Notify(message, error);
      },
      [this](int changedPane, bool searchMode, bool busy) {
        UpdatePaneSearchState(changedPane, searchMode, busy);
      });
}

void App::StartPaneSearch(int pane, const std::wstring &query) {
  paneController_.StartSearch(
      window_, pane, query,
      [this](const std::wstring &message, bool error) {
        Notify(message, error);
      },
      [this](int changedPane, bool searchMode, bool busy) {
        UpdatePaneSearchState(changedPane, searchMode, busy);
      });
}

void App::OpenPaneSelection(int pane) {
  paneController_.OpenSelected(
      window_, pane,
      [this](const std::wstring &message, bool error) {
        Notify(message, error);
      },
      [this](int changedPane, bool searchMode, bool busy) {
        UpdatePaneSearchState(changedPane, searchMode, busy);
      });
}

void App::LaunchRegisteredApplication(const std::string &id,
                                      bool administrator,
                                      bool passSelection) {
  RegisteredApplicationLaunch request;
  request.sourceId = id;
  request.administrator = administrator;
  request.passSelection = passSelection;
  request.argumentContext = BuildAppArgumentContext(passSelection);
  commandController_.LaunchRegisteredApplication(
      window_, settings_, request,
      [this](const std::wstring &message, bool error) {
        Notify(message, error);
      });
}

void App::AcceptCommandSuggestion(bool control, bool shift) {
  const CommandAction action = commandController_.AcceptCommandSuggestion(
      searchEdit_, commandSuggestions_,
      paneController_.ListHandle(activePane_), settings_, control, shift);
  switch (action.kind) {
  case CommandAction::Kind::None:
    return;
  case CommandAction::Kind::Search:
    StartPaneSearch(activePane_, action.value);
    return;
  case CommandAction::Kind::Navigate: {
    const int pane =
        action.otherPane && twoPanes_ ? 1 - activePane_ : activePane_;
    NavigatePane(pane, action.value);
    activePane_ = pane;
    UpdateActivePaneVisuals();
    UpdatePaneSearchState(activePane_,
                          paneController_.IsSearchMode(activePane_),
                          paneController_.IsBusy(activePane_));
    SetFocus(paneController_.ListHandle(activePane_));
    return;
  }
  case CommandAction::Kind::LaunchApplication:
    LaunchRegisteredApplication(action.sourceId, action.administrator,
                                action.passSelection);
    return;
  case CommandAction::Kind::LaunchTerminal:
    terminalController_.LaunchSelectedTerminal(
        window_, paneController_.EffectivePath(activePane_),
        TerminalKind::CommandPrompt, action.administrator,
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    return;
  case CommandAction::Kind::Error:
    Notify(action.value, true);
    return;
  }
}

bool App::HandleOwnerDraw(WPARAM, LPARAM lParam, LRESULT &result) {
  auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
  if (item->CtlType == ODT_BUTTON) {
    const bool primary = item->hwndItem == toolbar_[6];
    const bool hasDropDown =
        item->CtlID == IdAddLink || item->CtlID == IdTerminal;
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
    const HFONT font =
        reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ oldFont =
        font != nullptr ? SelectObject(item->hDC, font) : nullptr;
    RECT textRect = item->rcItem;
    if (pressed)
      OffsetRect(&textRect, 0, 1);
    if (hasDropDown) {
      RECT measured{};
      DrawTextW(item->hDC, label, -1, &measured,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
      const int arrowWidth =
          MulDiv(12, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      const int arrowGap =
          MulDiv(6, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      const int contentWidth = measured.right + arrowGap + arrowWidth;
      const int contentLeft =
          textRect.left +
          ((textRect.right - textRect.left) - contentWidth) / 2;
      textRect.left = contentLeft;
      textRect.right = contentLeft + measured.right;
      DrawTextW(item->hDC, label, -1, &textRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
      DrawDropDownIndicator(
          item->hDC, textRect.right + arrowGap + arrowWidth / 2,
          (textRect.top + textRect.bottom) / 2, text, dpi_);
    } else {
      DrawTextW(item->hDC, label, -1, &textRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }
    if (oldFont != nullptr)
      SelectObject(item->hDC, oldFont);
    if ((item->itemState & ODS_FOCUS) != 0) {
      RECT focus = button;
      InflateRect(&focus, -3, -3);
      DrawFocusRect(item->hDC, &focus);
    }
    result = TRUE;
    return true;
  }
  if (item->CtlID != IdSidebar || item->CtlType != ODT_LISTBOX)
    return false;

  FillRect(item->hDC, &item->rcItem, sidebarBrush_);
  if (item->itemID == static_cast<UINT>(-1)) {
    result = TRUE;
    return true;
  }

  RECT row = item->rcItem;
  const int horizontalInset =
      MulDiv(4, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  InflateRect(&row, -horizontalInset, -2);
  const bool selected = (item->itemState & ODS_SELECTED) != 0;
  if (selected) {
    const int radius =
        MulDiv(8, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    DrawRoundedSurface(item->hDC, row, kAccentSoftColor, kAccentSoftColor,
                       radius);
  }

  const LRESULT textLength =
      SendMessageW(sidebar_, LB_GETTEXTLEN, item->itemID, 0);
  std::wstring text(
      textLength > 0 ? static_cast<std::size_t>(textLength + 1) : 1U, L'\0');
  SendMessageW(sidebar_, LB_GETTEXT, item->itemID,
               reinterpret_cast<LPARAM>(text.data()));
  if (textLength > 0)
    text.resize(static_cast<std::size_t>(textLength));
  else
    text.clear();

  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, selected ? RGB(30, 64, 175) : kTextColor);
  RECT textRect = row;
  textRect.left += MulDiv(10, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
  DrawTextW(item->hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if ((item->itemState & ODS_FOCUS) != 0)
    DrawFocusRect(item->hDC, &row);
  result = TRUE;
  return true;
}

bool App::HandleControlColor(UINT message, WPARAM wParam, LPARAM lParam,
                             LRESULT &result) {
  const HWND control = reinterpret_cast<HWND>(lParam);
  HDC dc = reinterpret_cast<HDC>(wParam);
  switch (message) {
  case WM_CTLCOLOREDIT: {
    SetTextColor(dc, kTextColor);
    if (control == searchEdit_) {
      SetBkColor(dc, kSurfaceColor);
      result = reinterpret_cast<LRESULT>(surfaceBrush_);
      return true;
    }
    const int paneIndex =
        paneController_.PaneIndexFromControl(control, activePane_);
    if (control == paneController_.AddressHandle(paneIndex)) {
      const bool active = paneIndex == activePane_;
      SetBkColor(dc, active ? kActivePaneColor : kSurfaceColor);
      result =
          reinterpret_cast<LRESULT>(active ? activePaneBrush_ : surfaceBrush_);
      return true;
    }
    return false;
  }
  case WM_CTLCOLORLISTBOX:
    SetTextColor(dc, kTextColor);
    SetBkColor(dc, control == sidebar_ ? kSidebarColor : kSurfaceColor);
    result = reinterpret_cast<LRESULT>(control == sidebar_ ? sidebarBrush_
                                                           : surfaceBrush_);
    return true;
  case WM_CTLCOLORSTATIC:
    SetBkMode(dc, TRANSPARENT);
    if (control == status_ || control == driveCapacity_) {
      SetTextColor(dc, kMutedTextColor);
      result = reinterpret_cast<LRESULT>(backgroundBrush_);
      return true;
    }
    if (control == sidebarTitle_) {
      SetTextColor(dc, kTextColor);
      result = reinterpret_cast<LRESULT>(sidebarBrush_);
      return true;
    }
    return false;
  default:
    return false;
  }
}

bool App::HandleSplitterMessage(UINT message, WPARAM, LPARAM lParam,
                                LRESULT &result) {
  switch (message) {
  case WM_LBUTTONDOWN:
    if (twoPanes_) {
      const int splitterWidth = MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                                       USER_DEFAULT_SCREEN_DPI);
      const int split = paneCardRects_[0].right + splitterWidth / 2;
      if (abs(GET_X_LPARAM(lParam) - split) <= splitterWidth) {
        draggingSplitter_ = true;
        SetCapture(window_);
      }
    }
    result = 0;
    return true;
  case WM_MOUSEMOVE:
    if (draggingSplitter_) {
      RECT client{};
      GetClientRect(window_, &client);
      const int splitterWidth = MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                                       USER_DEFAULT_SCREEN_DPI);
      const int gap =
          MulDiv(10, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
      const int paneLeft = sidebarVisible_ ? sidebarCardRect_.right + gap : gap;
      const int available = client.right - paneLeft - splitterWidth - gap;
      if (available > 0) {
        splitRatio_ = std::clamp(
            static_cast<double>(GET_X_LPARAM(lParam) - paneLeft) / available,
            0.2, 0.8);
        LayoutControls(client.right, client.bottom);
      }
    }
    result = 0;
    return true;
  case WM_LBUTTONUP:
    if (draggingSplitter_) {
      draggingSplitter_ = false;
      ReleaseCapture();
    }
    result = 0;
    return true;
  case WM_SETCURSOR:
    if (twoPanes_ && LOWORD(lParam) == HTCLIENT) {
      const int splitterWidth = MulDiv(kSplitterWidth, static_cast<int>(dpi_),
                                       USER_DEFAULT_SCREEN_DPI);
      POINT point{};
      GetCursorPos(&point);
      ScreenToClient(window_, &point);
      const int split = paneCardRects_[0].right + splitterWidth / 2;
      if (abs(point.x - split) <= splitterWidth) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        result = TRUE;
        return true;
      }
    }
    return false;
  default:
    return false;
  }
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
  LRESULT appResult = 0;
  if (HandleAppMessage(message, wParam, lParam, appResult))
    return appResult;

  switch (message) {
  case WM_SYSCOMMAND:
    if ((wParam & 0xFFF0) == IdShowAbout) {
      ShowAboutDialog();
      return 0;
    }
    if ((wParam & 0xFFF0) == IdShowShortcuts) {
      ShowShortcutsDialog();
      return 0;
    }
    break;
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
  case WM_TIMER:
    if (wParam == kDriveCapacityTimerId) {
      UpdateDriveCapacityDisplay();
      return 0;
    }
    break;
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
          index == activePane_ ? kAccentColor : kBorderColor, radius,
          index == activePane_
              ? MulDiv(2, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI)
              : 1);
    }

    HPEN separator = CreatePen(PS_SOLID, 1, kBorderColor);
    const HGDIOBJ oldPen = SelectObject(dc, separator);
    const int toolbarBottom = MulDiv(kToolbarHeight - 1, static_cast<int>(dpi_),
                                     USER_DEFAULT_SCREEN_DPI);
    MoveToEx(dc, 0, toolbarBottom, nullptr);
    LineTo(dc, client.right, toolbarBottom);
    SelectObject(dc, oldPen);
    DeleteObject(separator);
    EndPaint(window_, &paint);
    return 0;
  }
  case WM_DRAWITEM: {
    LRESULT result = 0;
    if (HandleOwnerDraw(wParam, lParam, result))
      return result;
    if (shellMenuController_.HandleMenuMessage(message, wParam, lParam, result))
      return result;
    break;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORSTATIC: {
    LRESULT result = 0;
    if (HandleControlColor(message, wParam, lParam, result))
      return result;
    break;
  }
  case WM_LBUTTONDOWN:
  case WM_MOUSEMOVE:
  case WM_LBUTTONUP:
  case WM_SETCURSOR: {
    LRESULT result = 0;
    if (HandleSplitterMessage(message, wParam, lParam, result))
      return result;
    break;
  }
  case WM_COMMAND:
    return HandleCommand(wParam, lParam);
  case WM_APPCOMMAND: {
    // マウスの戻る/進むボタン(XBUTTON1/2)は WM_APPCOMMAND として届く。
    // ここでの戻る/進むは訪問履歴ではなく、パスの階層(IdUpとその取り消し)を意味する。
    // HIWORD は 0(アクセラレータ経由の呼び出しは 1)だが、
    // HandleCommand は IdUp/IdDown でその値を参照しないため無害。
    const short appCommand = GET_APPCOMMAND_LPARAM(lParam);
    if (appCommand == APPCOMMAND_BROWSER_BACKWARD) {
      HandleCommand(MAKEWPARAM(IdUp, 0), 0);
      return TRUE;
    }
    if (appCommand == APPCOMMAND_BROWSER_FORWARD) {
      HandleCommand(MAKEWPARAM(IdDown, 0), 0);
      return TRUE;
    }
    break;
  }
  case WM_NOTIFY:
    return HandleNotify(lParam);
  case WM_CONTEXTMENU:
    if (reinterpret_cast<HWND>(wParam) == sidebar_) {
      sidebarController_.ShowContextMenu(
          window_, sidebar_, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)},
          settings_,
          {IdOpenSidebar, IdOpenSidebarAdmin, IdEditSidebar, IdRemoveSidebar,
           IdMoveSidebarUp, IdMoveSidebarDown});
      PostMessageW(window_, kMessageRestoreFocus, 0, 0);
      return 0;
    }
    break;
  case WM_COPYDATA: {
    const auto *copyData = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
    if (copyData != nullptr && copyData->dwData == kOpenPathCopyDataId) {
      std::wstring path;
      if (copyData->lpData != nullptr && copyData->cbData > 0) {
        path.assign(static_cast<const wchar_t *>(copyData->lpData),
                    copyData->cbData / sizeof(wchar_t));
      }
      OpenExternalPath(path);
      return TRUE;
    }
    return FALSE;
  }
  case WM_CLOSE:
    if (fileOperationController_.PendingOperationCount() +
            zipController_.PendingOperationCount() >
        0) {
      const std::wstring closeWarning =
          std::format(L"ファイル処理が {} 件進行中です。\n"
                      L"終了処理はファイル処理の完了まで待機します。\n\n"
                      L"それでも終了しますか？",
                      fileOperationController_.PendingOperationCount() +
                          zipController_.PendingOperationCount());
      if (MessageBoxW(window_, closeWarning.c_str(), L"SimpleFiler を終了",
                      MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        PostMessageW(window_, kMessageRestoreFocus, 0, 0);
        return 0;
      }
    }
    SaveSettings();
    // Child windows (the pane ListViews) are destroyed before this window's
    // own WM_DESTROY fires, so their drop targets must be revoked here while
    // the HWNDs are still valid.
    for (int pane = 0; pane < 2; ++pane) {
      if (dropTargets_[pane] != nullptr) {
        RevokeDragDrop(paneController_.ListHandle(pane));
        dropTargets_[pane]->Release();
        dropTargets_[pane] = nullptr;
      }
    }
    DestroyWindow(window_);
    return 0;
  case WM_DESTROY:
    KillTimer(window_, kDriveCapacityTimerId);
    PostQuitMessage(0);
    return 0;
  case WM_INITMENUPOPUP:
  case WM_UNINITMENUPOPUP:
  case WM_MEASUREITEM:
  case WM_MENUCHAR: {
    LRESULT shellMenuResult = 0;
    if (shellMenuController_.HandleMenuMessage(message, wParam, lParam,
                                               shellMenuResult))
      return shellMenuResult;
    break;
  }
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT App::HandleCommand(WPARAM wParam, LPARAM) {
  const auto notify = [this](const std::wstring &message, bool error) {
    Notify(message, error);
  };
  const auto searchState = [this](int pane, bool searchMode, bool busy) {
    UpdatePaneSearchState(pane, searchMode, busy);
  };
  const ShellMenuIds shellMenuIds{
      IdAddFolderLink,  IdAddFileLink,  IdAddAppLink, IdPaste,
      IdNewFolder,      IdOpen,         IdCopy,       IdCut,
      IdRename,         IdDelete,       IdProperties, IdRegisteredAppBase,
      IdShellMenuFirst, IdShellMenuLast};

  const int command = LOWORD(wParam);
  if (HIWORD(wParam) == LBN_SELCHANGE && command == IdSidebar) {
    sidebarController_.ActivateSidebarItem(
        window_, sidebar_, settings_, false,
        [this](const std::wstring &path) { NavigatePane(activePane_, path); },
        [this](const std::string &id, bool administrator) {
          LaunchRegisteredApplication(id, administrator, true);
        },
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    return 0;
  }
  if (HIWORD(wParam) == LBN_DBLCLK && command == IdCommandSuggestions) {
    AcceptCommandSuggestion(false, false);
    return 0;
  }
  if (HIWORD(wParam) == EN_CHANGE && command == IdSearchEdit) {
    commandController_.RebuildCommandSuggestions(
        searchEdit_, commandSuggestions_, settings_,
        paneController_.EffectivePath(activePane_),
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    return 0;
  }
  if (HIWORD(wParam) == EN_SETFOCUS &&
      (command == IdLeftAddress || command == IdRightAddress)) {
    commandController_.HideCommandSuggestions(commandSuggestions_);
    activePane_ = command == IdLeftAddress ? 0 : 1;
    UpdateActivePaneVisuals();
  }
  switch (command) {
  case IdBack:
    paneController_.NavigateHistory(window_, activePane_, -1, notify,
                                    searchState);
    break;
  case IdForward:
    paneController_.NavigateHistory(window_, activePane_, 1, notify,
                                    searchState);
    break;
  case IdUp:
    paneController_.NavigateUp(window_, activePane_, notify, searchState);
    break;
  case IdDown:
    paneController_.NavigateDown(window_, activePane_, notify, searchState);
    break;
  case IdRefresh:
    RefreshPaneView(activePane_);
    break;
  case IdDrives:
    paneController_.ShowDrives(window_, activePane_, true, notify, searchState);
    break;
  case IdTogglePanes: {
    twoPanes_ = !twoPanes_;
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
    sidebarController_.AddBookmarkForPath(
        sidebar_, settings_, paneController_.EffectivePath(activePane_),
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        [this] { SaveSettings(); });
    break;
  case IdAddLink:
    shellMenuController_.ShowLinkMenu(window_, toolbar_[4], shellMenuIds);
    break;
  case IdAddFolderLink:
    sidebarController_.AddLinkedFolder(
        window_, sidebar_, settings_,
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        [this] { SaveSettings(); });
    break;
  case IdAddFileLink:
    sidebarController_.AddLink(
        window_, sidebar_, settings_, false,
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        [this] { SaveSettings(); });
    break;
  case IdAddAppLink:
    sidebarController_.AddLink(
        window_, sidebar_, settings_, true,
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        [this] { SaveSettings(); });
    break;
  case IdTerminal:
    terminalController_.ShowTerminalMenu(
        window_, toolbar_[5], paneController_.HasPath(activePane_),
        {IdCmd, IdCmdAdmin, IdPowerShell, IdPowerShellAdmin});
    break;
  case IdCmd:
    terminalController_.LaunchSelectedTerminal(
        window_, paneController_.EffectivePath(activePane_),
        TerminalKind::CommandPrompt, false,
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdCmdAdmin:
    terminalController_.LaunchSelectedTerminal(
        window_, paneController_.EffectivePath(activePane_),
        TerminalKind::CommandPrompt, true,
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdPowerShell:
    terminalController_.LaunchSelectedTerminal(
        window_, paneController_.EffectivePath(activePane_),
        TerminalKind::PowerShell, false,
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdPowerShellAdmin:
    terminalController_.LaunchSelectedTerminal(
        window_, paneController_.EffectivePath(activePane_),
        TerminalKind::PowerShell, true,
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdSearch:
    if (commandController_.HasCommandInput(searchEdit_)) {
      AcceptCommandSuggestion((GetKeyState(VK_CONTROL) & 0x8000) != 0,
                              (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    } else if (paneController_.IsSearchMode(activePane_) &&
               paneController_.IsBusy(activePane_)) {
      paneController_.CancelSearch(activePane_, notify, searchState);
    } else {
      StartPaneSearch(activePane_, GetWindowTextString(searchEdit_));
    }
    break;
  case IdCopy:
    if (fileOperationController_.CopySelection(
            window_, paneController_.SelectedPaths(activePane_), false,
            notify)) {
      paneController_.ClearCutPaths();
    }
    break;
  case IdCut: {
    std::vector<std::wstring> paths =
        paneController_.SelectedPaths(activePane_);
    if (fileOperationController_.CopySelection(window_, paths, true, notify))
      paneController_.SetCutPaths(std::move(paths));
    break;
  }
  case IdPaste:
    if (fileOperationController_.Paste(
            window_, paneController_.EffectivePath(activePane_), notify)) {
      paneController_.ClearCutPaths();
    }
    break;
  case IdSelectAll:
    paneController_.SelectAll(activePane_);
    break;
  case IdCopyToOther: {
    fileOperationController_.TransferSelectionToOtherPane(
        window_, paneController_.SelectedPaths(activePane_),
        paneController_.EffectivePath(1 - activePane_), twoPanes_, false,
        notify);
    break;
  }
  case IdMoveToOther: {
    fileOperationController_.TransferSelectionToOtherPane(
        window_, paneController_.SelectedPaths(activePane_),
        paneController_.EffectivePath(1 - activePane_), twoPanes_, true,
        notify);
    break;
  }
  case IdDelete:
    fileOperationController_.DeleteSelection(
        window_, paneController_.SelectedPaths(activePane_), false, notify);
    break;
  case IdPermanentDelete:
    fileOperationController_.DeleteSelection(
        window_, paneController_.SelectedPaths(activePane_), true, notify);
    break;
  case IdRename:
    paneController_.BeginRename(activePane_);
    break;
  case IdNewFolder:
    fileOperationController_.NewFolder(
        window_, paneController_.EffectivePath(activePane_),
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        notify);
    break;
  case IdProperties:
    fileOperationController_.ShowSelectedProperties(
        window_, paneController_.SelectedPaths(activePane_));
    break;
  case IdOpen:
    OpenPaneSelection(activePane_);
    break;
  case IdZipCreate:
    zipController_.CreateZipFromSelection(
        window_, paneController_.SelectedPaths(activePane_),
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdZipExtract:
    zipController_.ExtractSelectedZip(
        window_, paneController_.SelectedPaths(activePane_),
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdShowHidden:
    paneController_.ToggleShowHidden(window_, activePane_, notify, searchState);
    break;
  case IdFocusAddress:
    SetFocus(paneController_.AddressHandle(activePane_));
    SendMessageW(paneController_.AddressHandle(activePane_), EM_SETSEL, 0, -1);
    break;
  case IdFocusSearch:
    SetFocus(searchEdit_);
    SendMessageW(searchEdit_, EM_SETSEL, 0, -1);
    break;
  case IdSwitchPane:
    if (twoPanes_) {
      activePane_ = 1 - activePane_;
      UpdateActivePaneVisuals();
      const std::wstring query = paneController_.SearchQuery(activePane_);
      SetWindowTextW(searchEdit_, paneController_.IsSearchMode(activePane_)
                                      ? query.c_str()
                                      : L"");
      UpdatePaneSearchState(activePane_,
                            paneController_.IsSearchMode(activePane_),
                            paneController_.IsBusy(activePane_));
      SetFocus(paneController_.ListHandle(activePane_));
    }
    break;
  case IdRemoveSidebar:
    sidebarController_.RemoveSidebarItem(sidebar_, settings_,
                                         [this] { SaveSettings(); });
    break;
  case IdEditSidebar:
    sidebarController_.EditSidebarItem(
        window_, sidebar_, settings_,
        [this](const std::wstring &title, const std::wstring &label,
               const std::wstring &initial) {
          return PromptText(title, label, initial);
        },
        [this] { SaveSettings(); });
    break;
  case IdOpenSidebar:
    sidebarController_.ActivateSidebarItem(
        window_, sidebar_, settings_, false,
        [this](const std::wstring &path) { NavigatePane(activePane_, path); },
        [this](const std::string &id, bool administrator) {
          LaunchRegisteredApplication(id, administrator, true);
        },
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdOpenSidebarAdmin:
    sidebarController_.ActivateSidebarItem(
        window_, sidebar_, settings_, true,
        [this](const std::wstring &path) { NavigatePane(activePane_, path); },
        [this](const std::string &id, bool administrator) {
          LaunchRegisteredApplication(id, administrator, true);
        },
        [this](const std::wstring &message, bool error) {
          Notify(message, error);
        });
    break;
  case IdMoveSidebarUp:
    sidebarController_.MoveSidebarItem(sidebar_, settings_, true,
                                       [this] { SaveSettings(); });
    break;
  case IdMoveSidebarDown:
    sidebarController_.MoveSidebarItem(sidebar_, settings_, false,
                                       [this] { SaveSettings(); });
    break;
  }
  PostMessageW(window_, kMessageRestoreFocus, 0, 0);
  return 0;
}

LRESULT App::HandleNotify(LPARAM lParam) {
  const auto *header = reinterpret_cast<NMHDR *>(lParam);
  const int paneIndex =
      paneController_.PaneIndexFromControl(header->hwndFrom, activePane_);
  if (header->hwndFrom != paneController_.ListHandle(paneIndex))
    return 0;

  if (header->code == NM_CUSTOMDRAW) {
    auto *customDraw = reinterpret_cast<NMLVCUSTOMDRAW *>(lParam);
    if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
      return CDRF_NOTIFYITEMDRAW;
    if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
      const int item = static_cast<int>(customDraw->nmcd.dwItemSpec);
      if (paneController_.IsItemCut(paneIndex, item))
        customDraw->clrText = kMutedTextColor;
      return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
  } else if (header->code == NM_SETFOCUS) {
    activePane_ = paneIndex;
    UpdateActivePaneVisuals();
    const std::wstring query = paneController_.SearchQuery(activePane_);
    SetWindowTextW(searchEdit_, paneController_.IsSearchMode(activePane_)
                                    ? query.c_str()
                                    : L"");
    UpdatePaneSearchState(activePane_,
                          paneController_.IsSearchMode(activePane_),
                          paneController_.IsBusy(activePane_));
    Notify(paneIndex == 0 ? L"左ペイン" : L"右ペイン");
  } else if (header->code == NM_DBLCLK) {
    activePane_ = paneIndex;
    UpdateActivePaneVisuals();
    OpenPaneSelection(activePane_);
  } else if (header->code == NM_RCLICK) {
    activePane_ = paneIndex;
    UpdateActivePaneVisuals();
    const auto *click = reinterpret_cast<const NMITEMACTIVATE *>(lParam);
    paneController_.SelectContextItem(paneIndex, click->iItem);
    POINT point{};
    GetCursorPos(&point);
    const ShellMenuIds shellMenuIds{
        IdAddFolderLink,  IdAddFileLink,  IdAddAppLink, IdPaste,
        IdNewFolder,      IdOpen,         IdCopy,       IdCut,
        IdRename,         IdDelete,       IdProperties, IdRegisteredAppBase,
        IdShellMenuFirst, IdShellMenuLast};
    shellMenuController_.ShowFileMenu(
        window_, point, paneController_.SelectedPaths(activePane_),
        paneController_.EffectivePath(activePane_),
        paneController_.IsDriveView(activePane_), settings_, shellMenuIds,
        [this] { RefreshPaneView(activePane_); },
        [this] { OpenPaneSelection(activePane_); },
        [this] { paneController_.BeginRename(activePane_); },
        [this](const std::wstring &path) {
          paneController_.BeginRenameForNewItem(activePane_, path);
        },
        [this](const std::string &id) {
          LaunchRegisteredApplication(id, false, true);
        });
    PostMessageW(window_, kMessageRestoreFocus, 0, 0);
  } else if (header->code == LVN_BEGINDRAG) {
    activePane_ = paneIndex;
    UpdateActivePaneVisuals();
    const auto *drag = reinterpret_cast<const NMLISTVIEW *>(lParam);
    // Dragging an unselected item should drag just that item, matching
    // Explorer's behavior when the user starts a drag from an unselected row.
    if (drag->iItem >= 0 &&
        ListView_GetItemState(header->hwndFrom, drag->iItem, LVIS_SELECTED) ==
            0) {
      paneController_.SelectContextItem(paneIndex, drag->iItem);
    }
    const std::vector<std::wstring> paths =
        paneController_.SelectedPaths(paneIndex);
    // The ListView holds mouse capture when LVN_BEGINDRAG fires; SHDoDragDrop
    // needs mouse input itself, so release it first.
    ReleaseCapture();
    dragSourcePane_ = paneIndex;
    const DWORD dragEffect = paths.empty()
                                 ? DROPEFFECT_NONE
                                 : dragDropController_.BeginDrag(window_, paths);
    dragSourcePane_ = -1;
    if (dragEffect == DROPEFFECT_MOVE) {
      // The drop target (e.g. Explorer with Shift held, or the other pane)
      // moved the source files, so this pane's listing is now stale.
      RefreshPaneView(activePane_);
    }
  } else if (header->code == LVN_KEYDOWN) {
    const auto *key = reinterpret_cast<NMLVKEYDOWN *>(lParam);
    if (key->wVKey == VK_RETURN) {
      activePane_ = paneIndex;
      UpdateActivePaneVisuals();
      OpenPaneSelection(activePane_);
    } else if (key->wVKey == VK_ESCAPE && paneController_.HasCutPaths()) {
      paneController_.ClearCutPaths();
      if (ClearClipboard(window_))
        Notify(L"切り取りを取り消しました");
      else
        Notify(L"切り取りの取り消しに失敗しました", true);
    }
  } else if (header->code == LVN_COLUMNCLICK) {
    const auto *click = reinterpret_cast<NMLISTVIEW *>(lParam);
    paneController_.HandleColumnClick(paneIndex, click->iSubItem);
  } else if (header->code == LVN_ODFINDITEMW) {
    const auto *find = reinterpret_cast<NMLVFINDITEMW *>(lParam);
    return paneController_.FindItem(paneIndex, *find);
  } else if (header->code == LVN_GETDISPINFOW) {
    auto *display = reinterpret_cast<NMLVDISPINFOW *>(lParam);
    paneController_.PopulateDisplayInfo(paneIndex, *display);
  } else if (header->code == LVN_ENDLABELEDITW) {
    const auto *edit = reinterpret_cast<NMLVDISPINFOW *>(lParam);
    if (edit->item.pszText != nullptr &&
        fileOperationController_.RenameItem(
            window_, paneController_.ItemPath(paneIndex, edit->item.iItem),
            edit->item.pszText,
            [this](const std::wstring &message, bool error) {
              Notify(message, error);
            })) {
      return TRUE;
    }
  }
  return 0;
}

bool App::HandleAppMessage(UINT message, WPARAM wParam, LPARAM lParam,
                           LRESULT &result) {
  const auto notify = [this](const std::wstring &text, bool error) {
    Notify(text, error);
  };
  const auto searchState = [this](int pane, bool searchMode, bool busy) {
    UpdatePaneSearchState(pane, searchMode, busy);
  };
  switch (message) {
  case kMessageCommandType:
    result = commandController_.HandleCommandPrefixCharacter(
                 searchEdit_, static_cast<wchar_t>(wParam),
                 reinterpret_cast<HWND>(lParam))
                 ? TRUE
                 : FALSE;
    return true;
  case kMessageCommandAccept:
    AcceptCommandSuggestion((wParam & 1U) != 0, (wParam & 2U) != 0);
    result = 0;
    return true;
  case kMessageCommandMove:
    commandController_.MoveCommandSelection(
        commandSuggestions_, static_cast<int>(static_cast<INT_PTR>(lParam)));
    result = 0;
    return true;
  case kMessageCommandDismiss:
    commandController_.DismissCommandSuggestions(
        searchEdit_, commandSuggestions_,
        paneController_.ListHandle(activePane_), true);
    result = 0;
    return true;
  case kMessageCommandNew:
    switch (commandController_.RequestCommandRegistration(
        searchEdit_, commandSuggestions_)) {
    case CommandRegistrationKind::Bookmark:
      sidebarController_.AddBookmarkForPath(
          sidebar_, settings_, paneController_.EffectivePath(activePane_),
          [this](const std::wstring &title, const std::wstring &label,
                 const std::wstring &initial) {
            return PromptText(title, label, initial);
          },
          [this] { SaveSettings(); });
      break;
    case CommandRegistrationKind::Application:
      sidebarController_.AddLink(
          window_, sidebar_, settings_, true,
          [this](const std::wstring &title, const std::wstring &label,
                 const std::wstring &initial) {
            return PromptText(title, label, initial);
          },
          [this] { SaveSettings(); });
      break;
    case CommandRegistrationKind::None:
      notify(L"ff または aa コマンドを入力してから登録してください", true);
      break;
    }
    SetFocus(searchEdit_);
    commandController_.RebuildCommandSuggestions(
        searchEdit_, commandSuggestions_, settings_,
        paneController_.EffectivePath(activePane_), notify);
    result = 0;
    return true;
  case kMessageSidebarMove:
    if (reinterpret_cast<HWND>(lParam) == sidebar_)
      sidebarController_.MoveSidebarItem(sidebar_, settings_, wParam != 0,
                                         [this] { SaveSettings(); });
    result = 0;
    return true;
  case kMessageRestoreFocus:
    RestorePaneFocusIfNeeded();
    result = 0;
    return true;
  case kMessageNavigateAddress: {
    const int pane = static_cast<int>(wParam);
    activePane_ = pane;
    UpdateActivePaneVisuals();
    const std::wstring text =
        GetWindowTextString(paneController_.AddressHandle(pane));
    if (_wcsicmp(text.c_str(), L"PC") == 0) {
      paneController_.ShowDrives(window_, pane, true, notify, searchState);
    } else {
      NavigatePane(pane, text);
    }
    result = 0;
    return true;
  }
  case kMessageCancelAddress: {
    // wParam is always the address bar that currently has focus, which the
    // EN_SETFOCUS handler keeps in sync with activePane_.
    const int pane = static_cast<int>(wParam);
    paneController_.RestoreAddressText(pane);
    SetFocus(paneController_.ListHandle(activePane_));
    result = 0;
    return true;
  }
  case kMessageNavigateBreadcrumb: {
    const int pane = static_cast<int>(wParam);
    const auto *path = reinterpret_cast<const std::wstring *>(lParam);
    if (pane < 0 || pane > 1 || path == nullptr) {
      result = 0;
      return true;
    }
    commandController_.HideCommandSuggestions(commandSuggestions_);
    activePane_ = pane;
    UpdateActivePaneVisuals();
    if (path->empty()) {
      paneController_.ShowDrives(window_, pane, true, notify, searchState);
    } else {
      NavigatePane(pane, *path);
    }
    result = 0;
    return true;
  }
  case kMessageSearch:
    StartPaneSearch(activePane_, GetWindowTextString(searchEdit_));
    result = 0;
    return true;
  case kMessageEnumerationBatch:
    paneController_.HandleEnumerationBatch(lParam);
    result = 0;
    return true;
  case kMessageEnumerationDone:
    paneController_.HandleEnumerationDone(lParam, notify, searchState);
    result = 0;
    return true;
  case kMessageOperationDone:
    fileOperationController_.HandleOperationDone(
        lParam, notify, [this](int pane) { RefreshPaneView(pane); });
    SetFocus(paneController_.ListHandle(activePane_));
    result = 0;
    return true;
  case kMessageZipDone:
    zipController_.HandleZipDone(lParam, notify,
                                 [this](int pane) { RefreshPaneView(pane); });
    SetFocus(paneController_.ListHandle(activePane_));
    result = 0;
    return true;
  default:
    return false;
  }
}

} // namespace sf::win
