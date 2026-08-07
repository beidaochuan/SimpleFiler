#pragma once

#include <windows.h>

#include <oleidl.h>

#include <functional>
#include <string>
#include <vector>

namespace sf::win {

// Pure decision helper (unit-testable without COM): given whether the drag
// source and drop destination are on the same volume, the current modifier
// key state, and the mask of effects the source offered, returns the single
// DROPEFFECT this target should report. Shift forces MOVE, Ctrl forces COPY
// (Shift takes priority over Ctrl when both are held, since link creation is
// not offered). Falls back to DROPEFFECT_NONE if offeredEffectMask excludes
// the computed effect entirely.
[[nodiscard]] DWORD ComputeDropEffect(bool sameVolume, DWORD keyState,
                                      DWORD offeredEffectMask);

// IDropTarget for one pane's ListView. Accepts CF_HDROP drags (from
// SimpleFiler's own DragDropController or from Explorer/desktop) and hands
// the resulting paths to performTransfer. Ignores drags whose source pane
// (as reported by dragSourcePane, -1 if none) is this same pane, and ignores
// drops onto a drive-view pane (no single destination folder).
class PaneDropTarget final : public IDropTarget {
public:
  using IsDriveViewFn = std::function<bool(int pane)>;
  using EffectivePathFn = std::function<std::wstring(int pane)>;
  using PerformTransferFn = std::function<void(
      const std::vector<std::wstring> &paths, const std::wstring &destination,
      bool move)>;

  // dragSourcePane must outlive this object; it is read (never written) on
  // every DragEnter/DragOver to detect same-pane drags.
  PaneDropTarget(int pane, const int *dragSourcePane, IsDriveViewFn isDriveView,
                EffectivePathFn effectivePath,
                PerformTransferFn performTransfer);

  HRESULT __stdcall QueryInterface(REFIID riid, void **object) override;
  ULONG __stdcall AddRef() override;
  ULONG __stdcall Release() override;

  HRESULT __stdcall DragEnter(IDataObject *dataObject, DWORD keyState,
                              POINTL point, DWORD *effect) override;
  HRESULT __stdcall DragOver(DWORD keyState, POINTL point,
                             DWORD *effect) override;
  HRESULT __stdcall DragLeave() override;
  HRESULT __stdcall Drop(IDataObject *dataObject, DWORD keyState,
                         POINTL point, DWORD *effect) override;

private:
  [[nodiscard]] bool CanAcceptDrop() const;
  void UpdateEffect(DWORD keyState, DWORD *effect) const;

  ULONG refCount_ = 1;
  int pane_ = 0;
  const int *dragSourcePane_ = nullptr;
  IsDriveViewFn isDriveView_;
  EffectivePathFn effectivePath_;
  PerformTransferFn performTransfer_;
  std::vector<std::wstring> draggedPaths_; // populated on DragEnter
};

} // namespace sf::win
