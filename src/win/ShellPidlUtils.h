#pragma once

#include <windows.h>

#include <shlobj.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sf::win {

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

// Builds one child PIDL per path, requiring they all share the same parent
// folder (as GetUIObjectOf demands for a single-call multi-selection).
// itemPidls owns the storage; childPidls are views into it and only valid
// as long as itemPidls remains alive. Returns false if paths is empty, any
// path fails to resolve, or the paths do not share a common parent.
inline bool BuildChildPidlsSharingParent(
    const std::vector<std::wstring> &paths, std::vector<UniquePidl> &itemPidls,
    std::vector<PCUITEMID_CHILD> &childPidls) {
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
      // GetUIObjectOf requires all selected child PIDLs to share one parent.
      return false;
    }

    childPidls.push_back(ILFindLastID(itemPidl.get()));
    itemPidls.push_back(std::move(itemPidl));
  }
  return !itemPidls.empty();
}

} // namespace sf::win
