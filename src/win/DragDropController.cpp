#include "win/DragDropController.h"

#include "win/ShellPidlUtils.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <memory>

namespace sf::win {
namespace {

// Minimal IDropSource: cancels on Escape, drops on left-button release,
// always defers cursor feedback to the shell/target.
class FileDropSource final : public IDropSource {
public:
  HRESULT __stdcall QueryInterface(REFIID riid, void **object) override {
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *object = static_cast<IDropSource *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override { return ++refCount_; }
  ULONG __stdcall Release() override {
    const ULONG remaining = --refCount_;
    if (remaining == 0)
      delete this;
    return remaining;
  }

  HRESULT __stdcall QueryContinueDrag(BOOL escapePressed,
                                      DWORD keyState) override {
    if (escapePressed)
      return DRAGDROP_S_CANCEL;
    if ((keyState & (MK_LBUTTON | MK_RBUTTON)) == 0)
      return DRAGDROP_S_DROP;
    return S_OK;
  }

  HRESULT __stdcall GiveFeedback(DWORD) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

private:
  ULONG refCount_ = 1;
};

struct DropSourceReleaser final {
  void operator()(IDropSource *dropSource) const {
    if (dropSource != nullptr)
      dropSource->Release();
  }
};
using UniqueDropSource = std::unique_ptr<IDropSource, DropSourceReleaser>;

} // namespace

DWORD DragDropController::BeginDrag(
    HWND window, const std::vector<std::wstring> &paths) const {
  if (paths.empty())
    return DROPEFFECT_NONE;

  std::vector<UniquePidl> itemPidls;
  std::vector<PCUITEMID_CHILD> childPidls;
  if (!BuildChildPidlsSharingParent(paths, itemPidls, childPidls))
    return DROPEFFECT_NONE;

  ComPtr<IShellFolder> parentFolder;
  PCUITEMID_CHILD ignoredChild = nullptr;
  if (FAILED(SHBindToParent(
          itemPidls.front().get(), IID_IShellFolder,
          reinterpret_cast<void **>(parentFolder.AddressOf()),
          &ignoredChild))) {
    return DROPEFFECT_NONE;
  }

  ComPtr<IDataObject> dataObject;
  if (FAILED(parentFolder->GetUIObjectOf(
          window, static_cast<UINT>(childPidls.size()), childPidls.data(),
          IID_IDataObject, nullptr,
          reinterpret_cast<void **>(dataObject.AddressOf())))) {
    return DROPEFFECT_NONE;
  }

  const UniqueDropSource dropSource(new FileDropSource());
  DWORD effect = DROPEFFECT_NONE;
  SHDoDragDrop(window, dataObject.Get(), dropSource.get(),
              DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
  return effect;
}

} // namespace sf::win
