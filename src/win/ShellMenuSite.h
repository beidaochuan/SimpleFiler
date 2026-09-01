#pragma once

#include "win/ShellPidlUtils.h"

#include <windows.h>

#include <shobjidl.h>

#include <functional>
#include <string>

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
  ~ShellMenuSite();

  // Arms detection of an item created by a "New" verb invoked through this
  // site's context menu. CNewMenu (shell32's "New" submenu handler) resolves
  // the active view via QueryActiveShellView and calls IShellView::SelectItem
  // with SVSI_EDIT on the newly created item to put it into rename mode; that
  // callback only makes sense while a "New" verb is being invoked, so it is
  // armed immediately before InvokeCommand and disarmed right after.
  void ArmNewItemDetection(const std::wstring &folderPath,
                           std::function<void(std::wstring)> onNewItemPath);
  void DisarmNewItemDetection();

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
  // Minimal IShellView implementation returned from QueryActiveShellView
  // while a "New" verb is being invoked, solely so CNewMenu can call
  // SelectItem(..., SVSI_EDIT) on the item it just created.
  class NewItemShellView final : public IShellView {
  public:
    explicit NewItemShellView(ShellMenuSite &owner);

    HRESULT __stdcall QueryInterface(REFIID riid, void **object) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    // IOleWindow
    HRESULT __stdcall GetWindow(HWND *window) override;
    HRESULT __stdcall ContextSensitiveHelp(BOOL enterMode) override;

    // IShellView
    HRESULT __stdcall TranslateAccelerator(MSG *message) override;
    HRESULT __stdcall EnableModeless(BOOL enable) override;
    HRESULT __stdcall UIActivate(UINT state) override;
    HRESULT __stdcall Refresh() override;
    HRESULT __stdcall CreateViewWindow(IShellView *previous,
                                       LPCFOLDERSETTINGS settings,
                                       IShellBrowser *browser, RECT *rect,
                                       HWND *window) override;
    HRESULT __stdcall DestroyViewWindow() override;
    HRESULT __stdcall GetCurrentInfo(LPFOLDERSETTINGS settings) override;
    HRESULT __stdcall
    AddPropertySheetPages(DWORD reserved, LPFNSVADDPROPSHEETPAGE callback,
                          LPARAM lparam) override;
    HRESULT __stdcall SaveViewState() override;
    HRESULT __stdcall SelectItem(PCUITEMID_CHILD pidlItem,
                                 SVSIF flags) override;
    HRESULT __stdcall GetItemObject(UINT item, REFIID riid,
                                    void **object) override;

  private:
    ShellMenuSite &owner_;
    ULONG refCount_ = 1;
  };

  void NotifyNewItemPath(PCUITEMID_CHILD pidlItem);

  ULONG refCount_ = 1;
  HWND ownerWindow_;
  UniquePidl armedFolderPidl_;
  std::function<void(std::wstring)> onNewItemPath_;
  NewItemShellView *newItemView_ = nullptr;
};

} // namespace sf::win
