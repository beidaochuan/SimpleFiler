#include "win/AddressBar.h"

#include "core/PathBreadcrumbs.h"
#include "win/AppMessages.h"
#include "win/WinUtils.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace sf::win {
namespace {

constexpr COLORREF kTextColor = RGB(30, 41, 59);
constexpr COLORREF kMutedTextColor = RGB(100, 116, 139);
constexpr COLORREF kAccentColor = RGB(37, 99, 235);
constexpr COLORREF kAccentSoftColor = RGB(219, 234, 254);
constexpr std::size_t kOverflowBreadcrumb =
    std::numeric_limits<std::size_t>::max();

struct AddressBarState final {
  int pane = 0;
  int hover = -1;
  bool trackingMouse = false;
};

struct BreadcrumbLayout final {
  RECT button{};
  POINT separator{};
  std::wstring label;
  std::wstring target;
  std::size_t breadcrumb = 0;
  bool drawSeparator = false;
  bool current = false;
};

int Scale(HWND window, int value) {
  return MulDiv(value, static_cast<int>(GetDpiForWindow(window)),
                USER_DEFAULT_SCREEN_DPI);
}

int TextWidth(HDC dc, const std::wstring &text) {
  SIZE size{};
  return GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                               &size)
             ? size.cx
             : 0;
}

std::vector<BreadcrumbLayout> CalculateLayout(HWND window, HDC dc) {
  constexpr std::wstring_view searchPrefix = L"検索: ";
  const std::wstring displayPath = GetWindowTextString(window);
  std::vector<PathBreadcrumb> breadcrumbs =
      BuildPathBreadcrumbs(displayPath);
  if (displayPath.starts_with(searchPrefix)) {
    breadcrumbs.insert(
        breadcrumbs.begin(),
        {L"検索", std::wstring(displayPath).substr(searchPrefix.size())});
  }
  if (breadcrumbs.empty())
    return {};

  RECT client{};
  GetClientRect(window, &client);
  const int inset = Scale(window, 3);
  const int padding = Scale(window, 6);
  const int separatorWidth = Scale(window, 16);
  const int clientWidth = static_cast<int>(client.right - client.left);
  const int available = std::max(0, clientWidth - inset * 2);

  std::vector<int> buttonWidths;
  buttonWidths.reserve(breadcrumbs.size());
  int totalWidth = 0;
  for (std::size_t index = 0; index < breadcrumbs.size(); ++index) {
    const int width = TextWidth(dc, breadcrumbs[index].label) + padding * 2;
    buttonWidths.push_back(width);
    totalWidth += width;
    if (index != 0)
      totalWidth += separatorWidth;
  }

  std::size_t firstVisible = 0;
  const bool collapsed = totalWidth > available && breadcrumbs.size() > 1;
  bool showOverflow = collapsed;
  const int overflowWidth = TextWidth(dc, L"…") + padding * 2;
  if (collapsed) {
    int used = overflowWidth;
    firstVisible = breadcrumbs.size();
    for (std::size_t index = breadcrumbs.size(); index-- > 0;) {
      const int candidate = separatorWidth + buttonWidths[index];
      if (used + candidate > available)
        break;
      used += candidate;
      firstVisible = index;
    }
    if (firstVisible == breadcrumbs.size())
      firstVisible = breadcrumbs.size() - 1;
    if (available <
        overflowWidth + separatorWidth + Scale(window, 24)) {
      showOverflow = false;
    }
    if (firstVisible == 0)
      showOverflow = false;
  }

  std::vector<BreadcrumbLayout> layout;
  int x = client.left + inset;
  const auto append = [&](std::vector<BreadcrumbLayout> &entries,
                          const std::wstring &label,
                          const std::wstring &target,
                          std::size_t breadcrumb, bool separator,
                          int requestedWidth, int &left) {
    BreadcrumbLayout entry;
    entry.label = label;
    entry.target = target;
    entry.breadcrumb = breadcrumb;
    entry.drawSeparator = separator;
    entry.current =
        breadcrumb != kOverflowBreadcrumb &&
        breadcrumb + 1 == breadcrumbs.size();
    if (separator) {
      entry.separator = {left + separatorWidth / 2,
                         (client.top + client.bottom) / 2};
      left += separatorWidth;
    }
    const int remaining =
        std::max(0, static_cast<int>(client.right) - inset - left);
    const int width = std::min(requestedWidth, remaining);
    entry.button = {left, client.top + Scale(window, 2), left + width,
                    client.bottom - Scale(window, 2)};
    left += width;
    entries.push_back(std::move(entry));
  };

  if (showOverflow) {
    append(layout, L"…", breadcrumbs[firstVisible - 1].path,
           kOverflowBreadcrumb, false, overflowWidth, x);
  }
  const std::size_t begin = collapsed ? firstVisible : 0;
  for (std::size_t index = begin; index < breadcrumbs.size(); ++index) {
    append(layout, breadcrumbs[index].label, breadcrumbs[index].path, index,
           showOverflow || index != begin, buttonWidths[index], x);
    if (x >= client.right - inset)
      break;
  }
  return layout;
}

int HitTest(HWND window, POINT point) {
  HDC dc = GetDC(window);
  if (dc == nullptr)
    return -1;
  const HFONT font =
      reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
  const HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
  const std::vector<BreadcrumbLayout> layout = CalculateLayout(window, dc);
  if (oldFont != nullptr)
    SelectObject(dc, oldFont);
  ReleaseDC(window, dc);

  for (std::size_t index = 0; index < layout.size(); ++index) {
    if (PtInRect(&layout[index].button, point))
      return static_cast<int>(index);
  }
  return -1;
}

void PaintBreadcrumbs(HWND window, AddressBarState &state) {
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  if (dc == nullptr)
    return;

  RECT client{};
  GetClientRect(window, &client);
  HBRUSH background = reinterpret_cast<HBRUSH>(
      SendMessageW(GetParent(window), WM_CTLCOLOREDIT,
                   reinterpret_cast<WPARAM>(dc),
                   reinterpret_cast<LPARAM>(window)));
  if (background == nullptr)
    background = GetSysColorBrush(COLOR_WINDOW);
  FillRect(dc, &client, background);

  const HFONT font =
      reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
  const HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  const std::vector<BreadcrumbLayout> layout = CalculateLayout(window, dc);
  HPEN separatorPen =
      CreatePen(PS_SOLID, std::max(1, Scale(window, 2)), kMutedTextColor);
  const HGDIOBJ previousPen = SelectObject(dc, separatorPen);

  for (std::size_t index = 0; index < layout.size(); ++index) {
    const BreadcrumbLayout &entry = layout[index];
    if (entry.drawSeparator) {
      const int halfWidth = Scale(window, 2);
      const int halfHeight = Scale(window, 4);
      const POINT points[]{
          {entry.separator.x - halfWidth,
           entry.separator.y - halfHeight},
          {entry.separator.x + halfWidth, entry.separator.y},
          {entry.separator.x - halfWidth,
           entry.separator.y + halfHeight},
      };
      Polyline(dc, points, static_cast<int>(std::size(points)));
    }

    const bool hovered = state.hover == static_cast<int>(index);
    if (hovered || entry.current) {
      HBRUSH highlight = CreateSolidBrush(
          hovered ? kAccentSoftColor : RGB(239, 246, 255));
      const HGDIOBJ oldBrush = SelectObject(dc, highlight);
      const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
      RoundRect(dc, entry.button.left, entry.button.top, entry.button.right,
                entry.button.bottom, Scale(window, 5), Scale(window, 5));
      SelectObject(dc, oldPen);
      SelectObject(dc, oldBrush);
      DeleteObject(highlight);
    }

    RECT textRect = entry.button;
    textRect.left += Scale(window, 6);
    textRect.right -= Scale(window, 6);
    SetTextColor(dc, hovered || entry.current ? kAccentColor : kTextColor);
    DrawTextW(dc, entry.label.c_str(), static_cast<int>(entry.label.size()),
              &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                             DT_END_ELLIPSIS | DT_NOPREFIX);
  }

  SelectObject(dc, previousPen);
  DeleteObject(separatorPen);
  if (oldFont != nullptr)
    SelectObject(dc, oldFont);
  EndPaint(window, &paint);
}

void NavigateBreadcrumb(HWND window, AddressBarState &state,
                        int layoutIndex) {
  HDC dc = GetDC(window);
  if (dc == nullptr)
    return;
  const HFONT font =
      reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
  const HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
  const std::vector<BreadcrumbLayout> layout = CalculateLayout(window, dc);
  if (oldFont != nullptr)
    SelectObject(dc, oldFont);
  ReleaseDC(window, dc);
  if (layoutIndex < 0 ||
      layoutIndex >= static_cast<int>(layout.size())) {
    return;
  }

  const std::wstring target = layout[layoutIndex].target;
  state.hover = -1;
  SendMessageW(GetParent(window), kMessageNavigateBreadcrumb,
               static_cast<WPARAM>(state.pane),
               reinterpret_cast<LPARAM>(&target));
}

LRESULT CALLBACK AddressSubclass(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam, UINT_PTR,
                                 DWORD_PTR reference) {
  auto *state = reinterpret_cast<AddressBarState *>(reference);
  switch (message) {
  case WM_PAINT:
    if (GetFocus() != window) {
      PaintBreadcrumbs(window, *state);
      return 0;
    }
    break;
  case WM_ERASEBKGND:
    if (GetFocus() != window)
      return 1;
    break;
  case WM_MOUSEMOVE:
    if (GetFocus() != window) {
      const int hover =
          HitTest(window, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
      if (hover != state->hover) {
        state->hover = hover;
        InvalidateRect(window, nullptr, FALSE);
      }
      if (!state->trackingMouse) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        state->trackingMouse = TrackMouseEvent(&tracking) != FALSE;
      }
      return 0;
    }
    break;
  case WM_MOUSELEAVE:
    state->trackingMouse = false;
    if (state->hover != -1) {
      state->hover = -1;
      InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  case WM_LBUTTONDOWN:
    if (GetFocus() != window) {
      const int hit =
          HitTest(window, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
      if (hit >= 0) {
        NavigateBreadcrumb(window, *state, hit);
        return 0;
      }
    }
    break;
  case WM_SETCURSOR:
    if (GetFocus() != window) {
      POINT cursor{};
      GetCursorPos(&cursor);
      ScreenToClient(window, &cursor);
      if (HitTest(window, cursor) >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_SETFOCUS:
    state->hover = -1;
    InvalidateRect(window, nullptr, TRUE);
    break;
  case WM_KILLFOCUS:
    InvalidateRect(window, nullptr, TRUE);
    break;
  case WM_KEYDOWN:
    if (wParam == VK_RETURN) {
      PostMessageW(GetParent(window), kMessageNavigateAddress,
                   static_cast<WPARAM>(state->pane), 0);
      return 0;
    }
    break;
  case WM_NCDESTROY:
    RemoveWindowSubclass(window, AddressSubclass, 1);
    delete state;
    break;
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

void AttachAddressBar(HWND address, int pane) {
  if (address == nullptr)
    return;
  auto *state = new AddressBarState;
  state->pane = pane;
  if (!SetWindowSubclass(address, AddressSubclass, 1,
                         reinterpret_cast<DWORD_PTR>(state))) {
    delete state;
  }
}

} // namespace sf::win
