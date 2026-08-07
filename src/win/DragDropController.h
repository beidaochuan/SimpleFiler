#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sf::win {

class DragDropController final {
public:
  DragDropController() = default;
  ~DragDropController() = default;

  DragDropController(const DragDropController &) = delete;
  DragDropController &operator=(const DragDropController &) = delete;

  // Builds an Explorer-compatible IDataObject for the given paths and runs
  // a blocking OLE drag-and-drop loop (SHDoDragDrop). Returns the resulting
  // DROPEFFECT (DROPEFFECT_NONE if paths is empty, the shell objects cannot
  // be built, or the drag was cancelled).
  DWORD BeginDrag(HWND window, const std::vector<std::wstring> &paths) const;
};

} // namespace sf::win
