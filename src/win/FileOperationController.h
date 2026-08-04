#pragma once

#include "win/AsyncTaskTracker.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

class FileOperationController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using RefreshPaneFn = std::function<void(int pane)>;
  using PromptTextFn = std::function<std::wstring(const std::wstring &title,
                                                  const std::wstring &label,
                                                  const std::wstring &initial)>;

  [[nodiscard]] bool CopySelection(HWND window,
                                   const std::vector<std::wstring> &paths,
                                   bool cut, const NotifyFn &notify) const;
  void TransferSelectionToOtherPane(HWND window,
                                    const std::vector<std::wstring> &paths,
                                    const std::wstring &destination,
                                    bool twoPanes, bool move,
                                    const NotifyFn &notify);
  [[nodiscard]] bool Paste(HWND window, const std::wstring &destination,
                           const NotifyFn &notify);
  void DeleteSelection(HWND window, std::vector<std::wstring> paths,
                       bool permanent, const NotifyFn &notify);
  [[nodiscard]] bool RenameItem(HWND window, const std::wstring &path,
                                const std::wstring &newName,
                                const NotifyFn &notify);
  void NewFolder(HWND window, const std::wstring &parent,
                 const PromptTextFn &promptText, const NotifyFn &notify);
  void ShowSelectedProperties(HWND window,
                              const std::vector<std::wstring> &paths) const;
  void HandleOperationDone(LPARAM lParam, const NotifyFn &notify,
                           const RefreshPaneFn &refreshPane);

  [[nodiscard]] std::size_t PendingOperationCount() const noexcept {
    return tasks_.Size();
  }

private:
  AsyncTaskTracker tasks_;
};

} // namespace sf::win
