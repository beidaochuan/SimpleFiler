#include "win/ShellMenuSite.h"

#include <shlobj.h>

namespace sf::win {

ShellMenuSite::ShellMenuSite(HWND ownerWindow) : ownerWindow_(ownerWindow) {}

ShellMenuSite::~ShellMenuSite() {
  if (newItemView_ != nullptr)
    newItemView_->Release();
}

void ShellMenuSite::ArmNewItemDetection(
    const std::wstring &folderPath,
    std::function<void(std::wstring)> onNewItemPath) {
  PIDLIST_ABSOLUTE rawFolderPidl = nullptr;
  if (FAILED(SHParseDisplayName(folderPath.c_str(), nullptr, &rawFolderPidl,
                                0, nullptr)) ||
      rawFolderPidl == nullptr) {
    return;
  }
  armedFolderPidl_.reset(rawFolderPidl);
  onNewItemPath_ = std::move(onNewItemPath);
}

void ShellMenuSite::DisarmNewItemDetection() {
  armedFolderPidl_.reset();
  onNewItemPath_ = nullptr;
}

void ShellMenuSite::NotifyNewItemPath(PCUITEMID_CHILD pidlItem) {
  if (!armedFolderPidl_ || !onNewItemPath_ || pidlItem == nullptr)
    return;
  UniquePidl combined(ILCombine(armedFolderPidl_.get(), pidlItem));
  if (!combined)
    return;
  wchar_t path[MAX_PATH] = {};
  if (SHGetPathFromIDListW(combined.get(), path))
    onNewItemPath_(path);
}

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

HRESULT ShellMenuSite::QueryActiveShellView(IShellView **shellView) {
  if (shellView == nullptr)
    return E_POINTER;
  if (!armedFolderPidl_) {
    *shellView = nullptr;
    return E_NOTIMPL;
  }
  if (newItemView_ == nullptr)
    newItemView_ = new NewItemShellView(*this);
  newItemView_->AddRef();
  *shellView = newItemView_;
  return S_OK;
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

ShellMenuSite::NewItemShellView::NewItemShellView(ShellMenuSite &owner)
    : owner_(owner) {}

HRESULT ShellMenuSite::NewItemShellView::QueryInterface(REFIID riid,
                                                         void **object) {
  if (object == nullptr)
    return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IShellView) {
    *object = static_cast<IShellView *>(this);
  } else if (riid == IID_IOleWindow) {
    *object = static_cast<IOleWindow *>(this);
  } else {
    *object = nullptr;
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

ULONG ShellMenuSite::NewItemShellView::AddRef() { return ++refCount_; }

ULONG ShellMenuSite::NewItemShellView::Release() {
  const ULONG remaining = --refCount_;
  if (remaining == 0)
    delete this;
  return remaining;
}

HRESULT ShellMenuSite::NewItemShellView::GetWindow(HWND *window) {
  if (window == nullptr)
    return E_POINTER;
  *window = owner_.ownerWindow_;
  return S_OK;
}

HRESULT ShellMenuSite::NewItemShellView::ContextSensitiveHelp(BOOL) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::TranslateAccelerator(MSG *) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::EnableModeless(BOOL) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::UIActivate(UINT) { return E_NOTIMPL; }

HRESULT ShellMenuSite::NewItemShellView::Refresh() { return E_NOTIMPL; }

HRESULT ShellMenuSite::NewItemShellView::CreateViewWindow(
    IShellView *, LPCFOLDERSETTINGS, IShellBrowser *, RECT *, HWND *) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::DestroyViewWindow() {
  return E_NOTIMPL;
}

HRESULT
ShellMenuSite::NewItemShellView::GetCurrentInfo(LPFOLDERSETTINGS) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::AddPropertySheetPages(
    DWORD, LPFNSVADDPROPSHEETPAGE, LPARAM) {
  return E_NOTIMPL;
}

HRESULT ShellMenuSite::NewItemShellView::SaveViewState() { return E_NOTIMPL; }

HRESULT
ShellMenuSite::NewItemShellView::SelectItem(PCUITEMID_CHILD pidlItem,
                                            SVSIF flags) {
  if ((flags & SVSI_EDIT) != 0)
    owner_.NotifyNewItemPath(pidlItem);
  return S_OK;
}

HRESULT ShellMenuSite::NewItemShellView::GetItemObject(UINT, REFIID,
                                                        void **) {
  return E_NOTIMPL;
}

} // namespace sf::win
