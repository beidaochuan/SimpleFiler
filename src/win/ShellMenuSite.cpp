#include "win/ShellMenuSite.h"

namespace sf::win {

ShellMenuSite::ShellMenuSite(HWND ownerWindow) : ownerWindow_(ownerWindow) {}

HRESULT ShellMenuSite::QueryInterface(REFIID riid, void **object) {
  if (object == nullptr)
    return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IShellBrowser) {
    *object = static_cast<IShellBrowser *>(this);
  } else if (riid == IID_IOleWindow) {
    *object = static_cast<IOleWindow *>(static_cast<IShellBrowser *>(this));
  } else if (riid == IID_IServiceProvider) {
    *object = static_cast<IServiceProvider *>(this);
  } else {
    *object = nullptr;
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

ULONG ShellMenuSite::AddRef() { return ++refCount_; }

ULONG ShellMenuSite::Release() {
  const ULONG remaining = --refCount_;
  if (remaining == 0)
    delete this;
  return remaining;
}

HRESULT ShellMenuSite::GetWindow(HWND *window) {
  if (window == nullptr)
    return E_POINTER;
  *window = ownerWindow_;
  return S_OK;
}

HRESULT ShellMenuSite::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }

HRESULT ShellMenuSite::InsertMenusSB(HMENU, LPOLEMENUGROUPWIDTHS) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::SetMenuSB(HMENU, HOLEMENU, HWND) { return E_NOTIMPL; }

HRESULT ShellMenuSite::RemoveMenusSB(HMENU) { return E_NOTIMPL; }

HRESULT ShellMenuSite::SetStatusTextSB(LPCOLESTR) { return E_NOTIMPL; }

HRESULT ShellMenuSite::EnableModelessSB(BOOL) { return E_NOTIMPL; }

HRESULT ShellMenuSite::TranslateAcceleratorSB(LPMSG, WORD) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::BrowseObject(LPCITEMIDLIST, UINT) { return E_NOTIMPL; }

HRESULT ShellMenuSite::GetViewStateStream(DWORD, IStream **) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::GetControlWindow(UINT, HWND *) { return E_NOTIMPL; }

HRESULT ShellMenuSite::SendControlMsg(UINT, UINT, WPARAM, LPARAM,
                                      LRESULT *) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::QueryActiveShellView(IShellView **) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::OnViewWindowActive(IShellView *) { return E_NOTIMPL; }

HRESULT ShellMenuSite::SetToolbarItems(LPTBBUTTONSB, UINT, UINT) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::QueryService(REFGUID /*guidService*/, REFIID riid,
                                    void **object) {
  // This minimal site only ever plays the role of an IShellBrowser site, so
  // guidService is intentionally not checked here.
  return QueryInterface(riid, object);
}

} // namespace sf::win
