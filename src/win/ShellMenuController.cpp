#include "win/ShellMenuController.h"

#include "win/ShellMenuSite.h"
#include "win/ShellPidlUtils.h"
#include "win/WinUtils.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <array>

namespace sf::win {
namespace {

class ActiveMenuScope final {
public:
  ActiveMenuScope(IContextMenu2 *&activeMenu2, IContextMenu3 *&activeMenu3,
                  IContextMenu2 *menu2, IContextMenu3 *menu3)
      : activeMenu2_(activeMenu2), activeMenu3_(activeMenu3) {
    activeMenu2_ = menu2;
    activeMenu3_ = menu3;
  }

  ~ActiveMenuScope() {
    activeMenu2_ = nullptr;
    activeMenu3_ = nullptr;
  }

  ActiveMenuScope(const ActiveMenuScope &) = delete;
  ActiveMenuScope &operator=(const ActiveMenuScope &) = delete;

private:
  IContextMenu2 *&activeMenu2_;
  IContextMenu3 *&activeMenu3_;
};

} // namespace

ShellMenuController::~ShellMenuController() {
  ClearCachedBackgroundMenu();
  if (menuSite_ != nullptr)
    menuSite_->Release();
}

ShellMenuSite &ShellMenuController::EnsureMenuSite(HWND window) {
  // The controller is used with a single, long-lived main window, so the
  // site is created once and reused rather than tracked per window.
  if (menuSite_ == nullptr)
    menuSite_ = new ShellMenuSite(window);
  return *menuSite_;
}

void ShellMenuController::ShowLinkMenu(HWND window, HWND sourceButton,
                                       const ShellMenuIds &ids) const {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ids.addFolderLink,
              L"フォルダーリンクを追加");
  AppendMenuW(menu, MF_STRING, ids.addFileLink, L"ファイルリンクを追加");
  AppendMenuW(menu, MF_STRING, ids.addApplicationLink,
              L"アプリリンクを追加");
  RECT rectangle{};
  GetWindowRect(sourceButton, &rectangle);
  TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rectangle.left,
                 rectangle.bottom, 0, window, nullptr);
  DestroyMenu(menu);
}

void ShellMenuController::AppendFallbackBackgroundMenu(
    HWND window, POINT screenPoint, const ShellMenuIds &ids) const {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ids.paste, L"貼り付け");
  AppendMenuW(menu, MF_STRING, ids.newFolder, L"新しいフォルダー");
  const UINT selectedCommand =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                     screenPoint.y, 0, window, nullptr);
  DestroyMenu(menu);
  if (selectedCommand != 0)
    SendMessageW(window, WM_COMMAND, selectedCommand, 0);
}

void ShellMenuController::ShowBackgroundShellMenu(
    HWND window, const std::wstring &folderPath, POINT screenPoint,
    const ShellMenuIds &ids, const RefreshPaneFn &refreshPane) {
  IContextMenu *contextMenu = nullptr;
  if (cachedBackgroundMenu_ != nullptr &&
      cachedBackgroundMenuFolder_ == folderPath) {
    contextMenu = cachedBackgroundMenu_;
  } else {
    ClearCachedBackgroundMenu();
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(folderPath.c_str(), nullptr, &pidl, 0,
                                  nullptr)) ||
        pidl == nullptr) {
      AppendFallbackBackgroundMenu(window, screenPoint, ids);
      return;
    }
    ComPtr<IShellFolder> desktop;
    ComPtr<IShellFolder> folder;
    ComPtr<IContextMenu> freshContextMenu;
    const bool bound =
        SUCCEEDED(SHGetDesktopFolder(desktop.AddressOf())) &&
        SUCCEEDED(desktop->BindToObject(pidl, nullptr,
                                        IID_PPV_ARGS(folder.AddressOf()))) &&
        SUCCEEDED(folder->CreateViewObject(
            window, IID_PPV_ARGS(freshContextMenu.AddressOf())));
    ILFree(pidl);
    if (!bound) {
      AppendFallbackBackgroundMenu(window, screenPoint, ids);
      return;
    }
    // Verbs bridged through IExecuteCommand (e.g. "Share") resolve their
    // owner window via the site, not via CMINVOKECOMMANDINFOEX, and do
    // nothing silently if no site is set.
    IUnknown_SetSite(freshContextMenu.Get(),
                     static_cast<IShellBrowser *>(&EnsureMenuSite(window)));
    cachedBackgroundMenuFolder_ = folderPath;
    cachedBackgroundMenu_ = freshContextMenu.Detach();
    contextMenu = cachedBackgroundMenu_;
  }

  HMENU menu = CreatePopupMenu();
  // The background menu has no selected item, so CMF_CANRENAME (which
  // Explorer only sets for a single-item selection) is intentionally omitted.
  if (FAILED(contextMenu->QueryContextMenu(
          menu, 0, ids.shellMenuFirst, ids.shellMenuLast, CMF_NORMAL))) {
    DestroyMenu(menu);
    ClearCachedBackgroundMenu();
    AppendFallbackBackgroundMenu(window, screenPoint, ids);
    return;
  }

  ComPtr<IContextMenu2> contextMenu2;
  ComPtr<IContextMenu3> contextMenu3;
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu2.AddressOf())));
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu3.AddressOf())));

  UINT selectedCommand = 0;
  {
    ActiveMenuScope activeMenu(activeShellMenu2_, activeShellMenu3_,
                               contextMenu2.Get(), contextMenu3.Get());
    selectedCommand =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                       screenPoint.y, 0, window, nullptr);
  }

  if (selectedCommand >= ids.shellMenuFirst &&
      selectedCommand <= ids.shellMenuLast) {
    // lpVerb/lpVerbW must carry the offset from idCmdFirst, packed via
    // MAKEINTRESOURCE, not the raw command ID or a string pointer.
    const UINT_PTR verbOffset = selectedCommand - ids.shellMenuFirst;
    CMINVOKECOMMANDINFOEX invoke{};
    invoke.cbSize = sizeof(invoke);
    invoke.fMask = CMIC_MASK_UNICODE;
    invoke.hwnd = window;
    invoke.lpVerb = MAKEINTRESOURCEA(verbOffset);
    invoke.lpVerbW = MAKEINTRESOURCEW(verbOffset);
    invoke.nShow = SW_SHOWNORMAL;
    if (FAILED(contextMenu->InvokeCommand(
            reinterpret_cast<CMINVOKECOMMANDINFO *>(&invoke)))) {
      ClearCachedBackgroundMenu();
    }
    refreshPane();
  }
  // Dynamically populated submenus retain state until InvokeCommand returns.
  DestroyMenu(menu);
}

bool ShellMenuController::ShowItemShellMenu(
    HWND window, const std::vector<std::wstring> &paths, POINT screenPoint,
    const ShellMenuIds &ids, const RefreshPaneFn &refreshPane,
    const OpenSelectedFn &openSelected,
    const BeginRenameFn &beginRename) {
  if (paths.empty())
    return false;

  std::vector<UniquePidl> itemPidls;
  std::vector<PCUITEMID_CHILD> childPidls;
  if (!BuildChildPidlsSharingParent(paths, itemPidls, childPidls))
    return false;

  ComPtr<IShellFolder> parentFolder;
  PCUITEMID_CHILD ignoredChild = nullptr;
  if (FAILED(SHBindToParent(
          itemPidls.front().get(), IID_IShellFolder,
          reinterpret_cast<void **>(parentFolder.AddressOf()),
          &ignoredChild))) {
    return false;
  }

  ComPtr<IContextMenu> contextMenu;
  if (FAILED(parentFolder->GetUIObjectOf(
          window, static_cast<UINT>(childPidls.size()), childPidls.data(),
          IID_IContextMenu, nullptr,
          reinterpret_cast<void **>(contextMenu.AddressOf())))) {
    return false;
  }
  // Verbs bridged through IExecuteCommand (e.g. "Share") resolve their
  // owner window via the site, not via CMINVOKECOMMANDINFOEX, and do
  // nothing silently if no site is set.
  IUnknown_SetSite(contextMenu.Get(),
                   static_cast<IShellBrowser *>(&EnsureMenuSite(window)));

  HMENU menu = CreatePopupMenu();
  if (menu == nullptr)
    return false;

  UINT queryFlags = CMF_NORMAL | CMF_ITEMMENU;
  if (paths.size() == 1)
    queryFlags |= CMF_CANRENAME;
  if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    queryFlags |= CMF_EXTENDEDVERBS;
  if (FAILED(contextMenu->QueryContextMenu(
          menu, 0, ids.shellMenuFirst, ids.shellMenuLast, queryFlags))) {
    DestroyMenu(menu);
    return false;
  }

  ComPtr<IContextMenu2> contextMenu2;
  ComPtr<IContextMenu3> contextMenu3;
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu2.AddressOf())));
  static_cast<void>(
      contextMenu->QueryInterface(IID_PPV_ARGS(contextMenu3.AddressOf())));

  UINT selectedCommand = 0;
  {
    ActiveMenuScope activeMenu(activeShellMenu2_, activeShellMenu3_,
                               contextMenu2.Get(), contextMenu3.Get());
    selectedCommand =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                       screenPoint.y, 0, window, nullptr);
  }

  bool openInSimpleFiler = false;
  bool renameInSimpleFiler = false;
  if (selectedCommand >= ids.shellMenuFirst &&
      selectedCommand <= ids.shellMenuLast) {
    const UINT_PTR verbOffset = selectedCommand - ids.shellMenuFirst;
    std::array<wchar_t, 128> canonicalVerb{};
    if (SUCCEEDED(contextMenu->GetCommandString(
            verbOffset, GCS_VERBW, nullptr,
            reinterpret_cast<char *>(canonicalVerb.data()),
            static_cast<UINT>(canonicalVerb.size())))) {
      openInSimpleFiler =
          paths.size() == 1 && _wcsicmp(canonicalVerb.data(), L"open") == 0;
      renameInSimpleFiler =
          paths.size() == 1 && _wcsicmp(canonicalVerb.data(), L"rename") == 0;
    }

    if (!openInSimpleFiler && !renameInSimpleFiler) {
      CMINVOKECOMMANDINFOEX invoke{};
      invoke.cbSize = sizeof(invoke);
      invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
      if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        invoke.fMask |= CMIC_MASK_CONTROL_DOWN;
      invoke.hwnd = window;
      invoke.lpVerb = MAKEINTRESOURCEA(verbOffset);
      invoke.lpVerbW = MAKEINTRESOURCEW(verbOffset);
      invoke.nShow = SW_SHOWNORMAL;
      invoke.ptInvoke = screenPoint;
      static_cast<void>(contextMenu->InvokeCommand(
          reinterpret_cast<CMINVOKECOMMANDINFO *>(&invoke)));
      refreshPane();
    }
  }

  // Cascading shell extensions retain command state until invocation ends.
  DestroyMenu(menu);
  if (openInSimpleFiler)
    openSelected();
  else if (renameInSimpleFiler)
    beginRename();
  return true;
}

void ShellMenuController::ShowFileMenu(
    HWND window, POINT screenPoint, const std::vector<std::wstring> &paths,
    const std::wstring &backgroundFolder, bool driveView,
    const AppSettings &settings, const ShellMenuIds &ids,
    const RefreshPaneFn &refreshPane, const OpenSelectedFn &openSelected,
    const BeginRenameFn &beginRename,
    const LaunchApplicationFn &launchApplication) {
  if (paths.empty()) {
    if (driveView || backgroundFolder.empty())
      AppendFallbackBackgroundMenu(window, screenPoint, ids);
    else
      ShowBackgroundShellMenu(window, backgroundFolder, screenPoint, ids,
                              refreshPane);
    return;
  }
  if (ShowItemShellMenu(window, paths, screenPoint, ids, refreshPane,
                        openSelected, beginRename)) {
    return;
  }

  // Preserve core commands when Windows cannot build one shell context menu.
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ids.open, L"開く");

  HMENU applications = CreatePopupMenu();
  std::vector<std::string> applicationIds;
  for (const RegisteredLink &link : settings.links) {
    if (link.type != LinkType::Application ||
        ids.registeredApplicationBase + applicationIds.size() > 0x7fff) {
      continue;
    }
    const std::wstring name = Utf8ToWide(link.name);
    AppendMenuW(applications, MF_STRING,
                ids.registeredApplicationBase + applicationIds.size(),
                name.c_str());
    applicationIds.push_back(link.id);
  }
  if (applicationIds.empty())
    AppendMenuW(applications, MF_STRING | MF_GRAYED, 0, L"登録アプリなし");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(applications),
              L"登録アプリで開く");
  AppendMenuW(menu, MF_STRING, ids.addApplicationLink, L"アプリを登録...");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ids.copy, L"コピー");
  AppendMenuW(menu, MF_STRING, ids.cut, L"切り取り");
  AppendMenuW(menu, MF_STRING, ids.rename, L"名前の変更");
  AppendMenuW(menu, MF_STRING, ids.deleteItem, L"削除");
  AppendMenuW(menu, MF_STRING, ids.properties, L"プロパティ");
  if (paths.size() != 1) {
    EnableMenuItem(menu, ids.open, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, ids.rename, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, ids.properties, MF_BYCOMMAND | MF_GRAYED);
  }

  const UINT selectedCommand =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                     screenPoint.y, 0, window, nullptr);
  DestroyMenu(menu);
  if (selectedCommand >= ids.registeredApplicationBase &&
      selectedCommand - ids.registeredApplicationBase <
          applicationIds.size()) {
    launchApplication(
        applicationIds[selectedCommand - ids.registeredApplicationBase]);
  } else if (selectedCommand != 0) {
    SendMessageW(window, WM_COMMAND, selectedCommand, 0);
  }
}

bool ShellMenuController::HandleMenuMessage(UINT message, WPARAM wParam,
                                            LPARAM lParam,
                                            LRESULT &result) const {
  if (message == WM_MENUCHAR && activeShellMenu3_ != nullptr) {
    return SUCCEEDED(activeShellMenu3_->HandleMenuMsg2(
        message, wParam, lParam, &result));
  }
  if (activeShellMenu2_ != nullptr &&
      SUCCEEDED(activeShellMenu2_->HandleMenuMsg(message, wParam, lParam))) {
    result = 0;
    return true;
  }
  return false;
}

void ShellMenuController::ClearCachedBackgroundMenu() {
  if (cachedBackgroundMenu_ != nullptr) {
    cachedBackgroundMenu_->Release();
    cachedBackgroundMenu_ = nullptr;
  }
  cachedBackgroundMenuFolder_.clear();
}

} // namespace sf::win
