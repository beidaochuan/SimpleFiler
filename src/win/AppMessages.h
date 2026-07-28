#pragma once

#include <windows.h>

namespace sf::win {

inline constexpr UINT kMessageEnumerationBatch = WM_APP + 1;
inline constexpr UINT kMessageEnumerationDone = WM_APP + 2;
inline constexpr UINT kMessageOperationDone = WM_APP + 3;
inline constexpr UINT kMessageNavigateAddress = WM_APP + 4;
inline constexpr UINT kMessageSearch = WM_APP + 5;
inline constexpr UINT kMessageZipDone = WM_APP + 6;

} // namespace sf::win
