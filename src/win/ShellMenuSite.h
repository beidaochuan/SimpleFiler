#pragma once

#include <windows.h>

#include <shobjidl.h>

namespace sf::win {

// Minimal site object for hosting shell context menus obtained via
// IShellFolder::GetUIObjectOf/CreateViewObject. Static verbs (open, copy,
// delete, ...) work fine without a site because they resolve their owner
// window from CMINVOKECOMMANDINFOEX. Newer verbs bridged through
// IExecuteCommand (e.g. "Share") instead resolve their owner window through
// the site set via IObjectWithSite::SetSite, and silently do nothing if no
// site is set.
class ShellMenuSite final : public IShellBrowser, public IServiceProvider {
public:
  explicit ShellMenuSite(HWND ownerWindow);

  // IUnknown
  HRESULT __stdcall QueryInterface(REFIID riid, void **object) override;
  ULONG __stdcall AddRef() override;
  ULONG __stdcall Release() override;

  // IOleWindow
  HRESULT __stdcall GetWindow(HWND *window) override;
  HRESULT __stdcall ContextSensitiveHelp(BOOL enterMode) override;

  // IShellBrowser
  HRESULT __stdcall InsertMenusSB(HMENU hmenuShared,
                                  LPOLEMENUGROUPWIDTHS menuWidths) override;
  HRESULT __stdcall SetMenuSB(HMENU hmenuShared, HOLEMENU oleMenuReserved,
                              HWND activeObjectWindow) override;
  HRESULT __stdcall RemoveMenusSB(HMENU hmenuShared) override;
  HRESULT __stdcall SetStatusTextSB(LPCOLESTR statusText) override;
  HRESULT __stdcall EnableModelessSB(BOOL enable) override;
  HRESULT __stdcall TranslateAcceleratorSB(LPMSG message, WORD id) override;
  HRESULT __stdcall BrowseObject(LPCITEMIDLIST pidl, UINT flags) override;
  HRESULT __stdcall GetViewStateStream(DWORD mode, IStream **stream) override;
  HRESULT __stdcall GetControlWindow(UINT id, HWND *window) override;
  HRESULT __stdcall SendControlMsg(UINT id, UINT message, WPARAM wParam,
                                   LPARAM lParam, LRESULT *result) override;
  HRESULT __stdcall QueryActiveShellView(IShellView **shellView) override;
  HRESULT __stdcall OnViewWindowActive(IShellView *shellView) override;
  HRESULT __stdcall SetToolbarItems(LPTBBUTTONSB buttons, UINT count,
                                    UINT flags) override;

  // IServiceProvider
  HRESULT __stdcall QueryService(REFGUID guidService, REFIID riid,
                                 void **object) override;

private:
  ULONG refCount_ = 1;
  HWND ownerWindow_;
};

} // namespace sf::win
