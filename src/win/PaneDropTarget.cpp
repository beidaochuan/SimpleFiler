#include "win/PaneDropTarget.h"

#include "win/ShellOperations.h"
#include "win/WinUtils.h"

#include <utility>

namespace sf::win {

DWORD ComputeDropEffect(bool sameVolume, DWORD keyState,
                        DWORD offeredEffectMask) {
  DWORD effect = DROPEFFECT_NONE;
  if ((keyState & MK_SHIFT) != 0)
    effect = DROPEFFECT_MOVE;
  else if ((keyState & MK_CONTROL) != 0)
    effect = DROPEFFECT_COPY;
  else
    effect = sameVolume ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
  return (effect & offeredEffectMask) != 0 ? effect : DROPEFFECT_NONE;
}

PaneDropTarget::PaneDropTarget(int pane, const int *dragSourcePane,
                               IsDriveViewFn isDriveView,
                               EffectivePathFn effectivePath,
                               PerformTransferFn performTransfer)
    : pane_(pane), dragSourcePane_(dragSourcePane),
      isDriveView_(std::move(isDriveView)),
      effectivePath_(std::move(effectivePath)),
      performTransfer_(std::move(performTransfer)) {}

HRESULT __stdcall PaneDropTarget::QueryInterface(REFIID riid, void **object) {
  if (riid == IID_IUnknown || riid == IID_IDropTarget) {
    *object = static_cast<IDropTarget *>(this);
    AddRef();
    return S_OK;
  }
  *object = nullptr;
  return E_NOINTERFACE;
}

ULONG __stdcall PaneDropTarget::AddRef() { return ++refCount_; }

ULONG __stdcall PaneDropTarget::Release() {
  const ULONG remaining = --refCount_;
  if (remaining == 0)
    delete this;
  return remaining;
}

bool PaneDropTarget::CanAcceptDrop() const {
  return !draggedPaths_.empty() && pane_ != *dragSourcePane_ &&
         !isDriveView_(pane_);
}

void PaneDropTarget::UpdateEffect(DWORD keyState, DWORD *effect) const {
  // *effect enters as the set of effects the drag source offered
  // (DoDragDrop's dwOKEffects); only ever narrow it, never widen it.
  const DWORD offeredMask = *effect & (DROPEFFECT_COPY | DROPEFFECT_MOVE);
  if (!CanAcceptDrop()) {
    *effect = DROPEFFECT_NONE;
    return;
  }
  const bool sameVolume =
      SameVolume(draggedPaths_.front(), effectivePath_(pane_));
  *effect = ComputeDropEffect(sameVolume, keyState, offeredMask);
}

HRESULT __stdcall PaneDropTarget::DragEnter(IDataObject *dataObject,
                                            DWORD keyState, POINTL,
                                            DWORD *effect) {
  draggedPaths_ = PathsFromDataObject(dataObject);
  UpdateEffect(keyState, effect);
  return S_OK;
}

HRESULT __stdcall PaneDropTarget::DragOver(DWORD keyState, POINTL,
                                           DWORD *effect) {
  UpdateEffect(keyState, effect);
  return S_OK;
}

HRESULT __stdcall PaneDropTarget::DragLeave() {
  draggedPaths_.clear();
  return S_OK;
}

HRESULT __stdcall PaneDropTarget::Drop(IDataObject *, DWORD keyState, POINTL,
                                       DWORD *effect) {
  UpdateEffect(keyState, effect);
  if (*effect != DROPEFFECT_NONE) {
    performTransfer_(draggedPaths_, effectivePath_(pane_),
                     *effect == DROPEFFECT_MOVE);
  }
  draggedPaths_.clear();
  return S_OK;
}

} // namespace sf::win
