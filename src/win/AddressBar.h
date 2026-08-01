#pragma once

#include <windows.h>

namespace sf::win {

// Adds breadcrumb painting and hit-testing to an address EDIT control while
// preserving its normal editable behavior whenever it has keyboard focus.
void AttachAddressBar(HWND address, int pane);

} // namespace sf::win
