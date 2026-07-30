#pragma once

#include "win/AsyncTaskTracker.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

class ZipController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using RefreshPaneFn = std::function<void(int pane)>;

  ZipController() = default;

  void CreateZipFromSelection(HWND window,
                              const std::vector<std::wstring> &paths,
                              const NotifyFn &notify);
  void ExtractSelectedZip(HWND window,
                          const std::vector<std::wstring> &paths,
                          const NotifyFn &notify);
  void HandleZipDone(LPARAM lParam, const NotifyFn &notify,
                     const RefreshPaneFn &refreshPane);

  [[nodiscard]] std::size_t PendingOperationCount() const {
    return tasks_.Size();
  }

private:
  AsyncTaskTracker tasks_;
};

} // namespace sf::win
