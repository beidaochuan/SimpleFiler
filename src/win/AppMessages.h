#pragma once

#include <windows.h>

namespace sf::win {

inline constexpr UINT kMessageEnumerationBatch = WM_APP + 1;
inline constexpr UINT kMessageEnumerationDone = WM_APP + 2;
inline constexpr UINT kMessageOperationDone = WM_APP + 3;
inline constexpr UINT kMessageNavigateAddress = WM_APP + 4;
inline constexpr UINT kMessageSearch = WM_APP + 5;
inline constexpr UINT kMessageZipDone = WM_APP + 6;
inline constexpr UINT kMessageCommandType = WM_APP + 7;
inline constexpr UINT kMessageCommandAccept = WM_APP + 8;
inline constexpr UINT kMessageCommandMove = WM_APP + 9;
inline constexpr UINT kMessageCommandDismiss = WM_APP + 10;
inline constexpr UINT kMessageCommandNew = WM_APP + 11;
inline constexpr UINT kMessageSidebarMove = WM_APP + 12;
inline constexpr UINT kMessageRestoreFocus = WM_APP + 13;
// Sent synchronously; lParam points to a std::wstring for the duration of
// SendMessage.
inline constexpr UINT kMessageNavigateBreadcrumb = WM_APP + 14;

} // namespace sf::win
