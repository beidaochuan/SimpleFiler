#include "win/ShellOperations.h"

#include "core/DuplicateName.h"
#include "win/AppMessages.h"
#include "win/WinUtils.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <thread>

#if defined(_MSC_VER)
// TaskDialogIndirect requires Common Controls v6, which is only loaded when
// the running binary carries a matching manifest dependency. SimpleFiler.exe
// gets this from resources/SimpleFiler.manifest, but test executables that
// link this file do not embed a manifest, so declare the dependency here to
// cover every binary that calls ShowDuplicateConflictDialog.
#pragma comment(linker,                                                      \
                "\"/manifestdependency:type='win32' "                       \
                "name='Microsoft.Windows.Common-Controls' "                  \
                "version='6.0.0.0' processorArchitecture='*' "               \
                "publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace sf::win {
namespace {

constexpr wchar_t kPreferredDropEffect[] = L"Preferred DropEffect";

DWORD NotificationProcessId(HWND window) {
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  return processId;
}

void Notify(HWND window, DWORD expectedProcessId, OperationId operationId,
            HRESULT result, bool aborted) {
  auto *operation = new OperationResult{operationId, result, aborted};
  if (expectedProcessId == 0 ||
      NotificationProcessId(window) != expectedProcessId) {
    delete operation;
    return;
  }
  if (!PostMessageW(window, kMessageOperationDone, 0,
                    reinterpret_cast<LPARAM>(operation))) {
    delete operation;
  }
}

IShellItem *ItemFromPath(const std::wstring &path) {
  IShellItem *item = nullptr;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                         IID_PPV_ARGS(&item)))) {
    return nullptr;
  }
  return item;
}

IFileOperation *CreateOperation(HWND owner, bool recycle) {
  IFileOperation *operation = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&operation)))) {
    return nullptr;
  }
  FILEOP_FLAGS flags = static_cast<FILEOP_FLAGS>(
      FOF_NOCONFIRMMKDIR | FOFX_SHOWELEVATIONPROMPT | FOFX_EARLYFAILURE |
      FOFX_PRESERVEFILEEXTENSIONS);
  if (recycle) {
    flags = static_cast<FILEOP_FLAGS>(
        flags | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE);
  }
  if (FAILED(operation->SetOperationFlags(flags))) {
    operation->Release();
    return nullptr;
  }
  operation->SetOwnerWindow(owner);
  return operation;
}

// Compares two directory paths ignoring case and any trailing separator.
bool SameDirectory(const std::wstring &left, const std::wstring &right) {
  const auto trim = [](std::wstring value) {
    while (value.size() > 1 &&
           (value.back() == L'\\' || value.back() == L'/')) {
      value.pop_back();
    }
    return value;
  };
  const std::wstring a = trim(left);
  const std::wstring b = trim(right);
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> PathsFromHDrop(HDROP drop) {
  std::vector<std::wstring> paths;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT index = 0; index < count; ++index) {
    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
    std::wstring path(static_cast<std::size_t>(length + 1), L'\0');
    DragQueryFileW(drop, index, path.data(), length + 1);
    path.resize(length);
    paths.push_back(std::move(path));
  }
  return paths;
}

// Picks a unique "- コピー" name for sourcePath inside destination, avoiding
// both names already present on disk and names already reserved earlier in
// the same batch.
std::wstring ResolveDuplicateName(const std::wstring &destination,
                                  const std::wstring &sourcePath,
                                  const std::vector<std::wstring> &reservedNames) {
  const std::filesystem::path source(sourcePath);
  const std::wstring originalName = source.filename().wstring();
  const bool isDirectory = IsDirectory(sourcePath);
  const auto nameExists = [&](const std::wstring &candidate) {
    for (const std::wstring &used : reservedNames) {
      if (_wcsicmp(used.c_str(), candidate.c_str()) == 0)
        return true;
    }
    return PathExists((std::filesystem::path(destination) / candidate).wstring());
  };
  return GenerateDuplicateName(originalName, isDirectory, nameExists);
}

void RunPaste(HWND notifyWindow, DWORD notificationProcessId,
              OperationId operationId, std::wstring destination,
              std::vector<std::wstring> paths, bool move,
              ConflictConfirmFn confirmConflict) {
  const HRESULT initialize = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOperation *operation = CreateOperation(notifyWindow, true);
  IShellItem *destinationItem = ItemFromPath(destination);
  HRESULT result =
      operation != nullptr && destinationItem != nullptr ? S_OK : E_FAIL;
  std::vector<std::wstring> reservedNames;
  bool applyToAllConfirmed = false;
  bool userCancelled = false;
  if (SUCCEEDED(result)) {
    for (const std::wstring &path : paths) {
      IShellItem *source = ItemFromPath(path);
      if (source == nullptr) {
        result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        break;
      }
      std::wstring newName;
      if (!move &&
          SameDirectory(std::filesystem::path(path).parent_path().wstring(),
                        destination)) {
        DuplicateConflictChoice choice = DuplicateConflictChoice::CopyOnce;
        if (!applyToAllConfirmed) {
          const std::wstring fileName =
              std::filesystem::path(path).filename().wstring();
          choice = confirmConflict(notifyWindow, fileName);
        }
        if (choice == DuplicateConflictChoice::Cancel) {
          source->Release();
          userCancelled = true;
          break;
        }
        if (choice == DuplicateConflictChoice::ApplyToAll)
          applyToAllConfirmed = true;
        newName = ResolveDuplicateName(destination, path, reservedNames);
        reservedNames.push_back(newName);
      }
      result =
          move ? operation->MoveItem(source, destinationItem, nullptr, nullptr)
               : operation->CopyItem(source, destinationItem,
                                     newName.empty() ? nullptr : newName.c_str(),
                                     nullptr);
      source->Release();
      if (FAILED(result))
        break;
    }
  }
  if (!userCancelled && SUCCEEDED(result))
    result = operation->PerformOperations();
  BOOL aborted = FALSE;
  if (operation != nullptr)
    operation->GetAnyOperationsAborted(&aborted);
  if (userCancelled)
    aborted = TRUE;
  if (destinationItem != nullptr)
    destinationItem->Release();
  if (operation != nullptr)
    operation->Release();
  if (SUCCEEDED(initialize))
    CoUninitialize();
  Notify(notifyWindow, notificationProcessId, operationId, result,
         aborted != FALSE);
}

} // namespace

DuplicateConflictChoice ShowDuplicateConflictDialog(HWND owner,
                                                    const std::wstring &fileName) {
  constexpr int kCopyOnceId = 1001;
  constexpr int kApplyToAllId = 1002;
  TASKDIALOG_BUTTON buttons[] = {
      {kCopyOnceId, L"コピーを作成"},
      {kApplyToAllId, L"すべての項目に適用してコピーを作成"},
  };
  const std::wstring content =
      L"「" + fileName + L"」は貼り付け先と同じフォルダーにあります。";
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = static_cast<TASKDIALOG_FLAGS>(TDF_USE_COMMAND_LINKS |
                                                 TDF_ALLOW_DIALOG_CANCELLATION);
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.pszWindowTitle = L"SimpleFiler";
  config.pszMainIcon = TD_INFORMATION_ICON;
  config.pszMainInstruction = L"コピーを作成しますか?";
  config.pszContent = content.c_str();
  config.pButtons = buttons;
  config.cButtons = static_cast<UINT>(std::size(buttons));
  config.nDefaultButton = kCopyOnceId;
  int pressedButton = IDCANCEL;
  if (FAILED(TaskDialogIndirect(&config, &pressedButton, nullptr, nullptr)))
    return DuplicateConflictChoice::Cancel;
  switch (pressedButton) {
  case kCopyOnceId:
    return DuplicateConflictChoice::CopyOnce;
  case kApplyToAllId:
    return DuplicateConflictChoice::ApplyToAll;
  default:
    return DuplicateConflictChoice::Cancel;
  }
}

bool PutFilesOnClipboard(HWND owner, const std::vector<std::wstring> &paths,
                         bool cut) {
  if (paths.empty())
    return false;
  std::size_t characters = 1;
  for (const std::wstring &path : paths)
    characters += path.size() + 1;
  const std::size_t bytes = sizeof(DROPFILES) + characters * sizeof(wchar_t);
  HGLOBAL dropMemory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
  if (dropMemory == nullptr)
    return false;
  auto *drop = static_cast<DROPFILES *>(GlobalLock(dropMemory));
  if (drop == nullptr) {
    GlobalFree(dropMemory);
    return false;
  }
  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;
  auto *output = reinterpret_cast<wchar_t *>(
      reinterpret_cast<unsigned char *>(drop) + sizeof(DROPFILES));
  for (const std::wstring &path : paths) {
    memcpy(output, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    output += path.size() + 1;
  }
  GlobalUnlock(dropMemory);

  HGLOBAL effectMemory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
  if (effectMemory == nullptr) {
    GlobalFree(dropMemory);
    return false;
  }
  auto *effect = static_cast<DWORD *>(GlobalLock(effectMemory));
  if (effect == nullptr) {
    GlobalFree(dropMemory);
    GlobalFree(effectMemory);
    return false;
  }
  *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
  GlobalUnlock(effectMemory);

  if (!OpenClipboard(owner)) {
    GlobalFree(dropMemory);
    GlobalFree(effectMemory);
    return false;
  }
  EmptyClipboard();
  const UINT effectFormat = RegisterClipboardFormatW(kPreferredDropEffect);
  const bool dropSet = SetClipboardData(CF_HDROP, dropMemory) != nullptr;
  const bool effectSet =
      dropSet && SetClipboardData(effectFormat, effectMemory) != nullptr;
  CloseClipboard();
  // Ownership transfers independently for each successful SetClipboardData
  // call.
  if (!dropSet)
    GlobalFree(dropMemory);
  if (!effectSet)
    GlobalFree(effectMemory);
  return dropSet && effectSet;
}

bool ClearClipboard(HWND owner) {
  if (!OpenClipboard(owner))
    return false;
  // Only clear the clipboard if it still holds what this window put there.
  // Otherwise another application may have taken ownership since the cut
  // (e.g. by copying its own data), and clearing it would destroy that.
  const bool ownedByCaller = GetClipboardOwner() == owner;
  if (ownedByCaller)
    EmptyClipboard();
  CloseClipboard();
  return ownedByCaller;
}

std::vector<std::wstring> ReadFilesFromClipboard(bool *cut) {
  std::vector<std::wstring> paths;
  if (cut != nullptr)
    *cut = false;
  if (!OpenClipboard(nullptr))
    return paths;
  const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (drop != nullptr)
    paths = PathsFromHDrop(drop);
  const UINT effectFormat = RegisterClipboardFormatW(kPreferredDropEffect);
  if (cut != nullptr) {
    const HGLOBAL effectMemory = GetClipboardData(effectFormat);
    if (effectMemory != nullptr) {
      const auto *effect = static_cast<const DWORD *>(GlobalLock(effectMemory));
      if (effect != nullptr) {
        *cut = (*effect & DROPEFFECT_MOVE) != 0;
        GlobalUnlock(effectMemory);
      }
    }
  }
  CloseClipboard();
  return paths;
}

std::vector<std::wstring> PathsFromDataObject(IDataObject *dataObject) {
  if (dataObject == nullptr)
    return {};
  FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium{};
  if (FAILED(dataObject->GetData(&format, &medium)))
    return {};
  std::vector<std::wstring> paths;
  // A non-conforming IDataObject could report success with a different
  // tymed despite the TYMED_HGLOBAL request; guard against misreading the
  // STGMEDIUM union as the wrong member.
  if (medium.tymed == TYMED_HGLOBAL) {
    if (const auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        drop != nullptr) {
      paths = PathsFromHDrop(drop);
      GlobalUnlock(medium.hGlobal);
    }
  }
  ReleaseStgMedium(&medium);
  return paths;
}

std::jthread PasteFilesAsync(HWND notifyWindow, OperationId operationId,
                             const std::wstring &destination,
                             ConflictConfirmFn confirmConflict) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  bool cut = false;
  std::vector<std::wstring> paths = ReadFilesFromClipboard(&cut);
  if (paths.empty()) {
    Notify(notifyWindow, notificationProcessId, operationId,
           HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS), false);
    return {};
  }
  return std::jthread(RunPaste, notifyWindow, notificationProcessId,
                      operationId, destination, std::move(paths), cut,
                      std::move(confirmConflict));
}

std::jthread TransferFilesAsync(HWND notifyWindow, OperationId operationId,
                                std::vector<std::wstring> paths,
                                std::wstring destination, bool move,
                                ConflictConfirmFn confirmConflict) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  if (paths.empty() || destination.empty()) {
    Notify(notifyWindow, notificationProcessId, operationId,
           HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER), false);
    return {};
  }
  return std::jthread(RunPaste, notifyWindow, notificationProcessId,
                      operationId, std::move(destination), std::move(paths),
                      move, std::move(confirmConflict));
}

std::jthread DeleteFilesAsync(HWND notifyWindow, OperationId operationId,
                              std::vector<std::wstring> paths,
                              bool permanent) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  return std::jthread([notifyWindow, notificationProcessId, operationId,
                       paths = std::move(paths), permanent]() {
    const HRESULT initialize =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOperation *operation = CreateOperation(notifyWindow, !permanent);
    HRESULT result = operation != nullptr ? S_OK : E_FAIL;
    for (const std::wstring &path : paths) {
      if (FAILED(result))
        break;
      IShellItem *item = ItemFromPath(path);
      if (item == nullptr) {
        result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        break;
      }
      result = operation->DeleteItem(item, nullptr);
      item->Release();
      if (FAILED(result))
        break;
    }
    if (SUCCEEDED(result))
      result = operation->PerformOperations();
    BOOL aborted = FALSE;
    if (operation != nullptr)
      operation->GetAnyOperationsAborted(&aborted);
    if (operation != nullptr)
      operation->Release();
    if (SUCCEEDED(initialize))
      CoUninitialize();
    Notify(notifyWindow, notificationProcessId, operationId, result,
           aborted != FALSE);
  });
}

std::jthread RenameFileAsync(HWND notifyWindow, OperationId operationId,
                             std::wstring path, std::wstring newName) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  return std::jthread([notifyWindow, notificationProcessId, operationId,
                       path = std::move(path), newName = std::move(newName)] {
    const HRESULT initialize =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOperation *operation = CreateOperation(notifyWindow, true);
    IShellItem *item = ItemFromPath(path);
    HRESULT result = operation != nullptr && item != nullptr ? S_OK : E_FAIL;
    if (SUCCEEDED(result))
      result = operation->RenameItem(item, newName.c_str(), nullptr);
    if (SUCCEEDED(result))
      result = operation->PerformOperations();
    BOOL aborted = FALSE;
    if (operation != nullptr)
      operation->GetAnyOperationsAborted(&aborted);
    if (item != nullptr)
      item->Release();
    if (operation != nullptr)
      operation->Release();
    if (SUCCEEDED(initialize))
      CoUninitialize();
    Notify(notifyWindow, notificationProcessId, operationId, result,
           aborted != FALSE);
  });
}

std::jthread CreateFolderAsync(HWND notifyWindow, OperationId operationId,
                               std::wstring parent, std::wstring name) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  return std::jthread([notifyWindow, notificationProcessId, operationId,
                       parent = std::move(parent), name = std::move(name)] {
    const HRESULT initialize =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOperation *operation = CreateOperation(notifyWindow, true);
    IShellItem *destination = ItemFromPath(parent);
    HRESULT result =
        operation != nullptr && destination != nullptr ? S_OK : E_FAIL;
    if (SUCCEEDED(result)) {
      result = operation->NewItem(destination, FILE_ATTRIBUTE_DIRECTORY,
                                  name.c_str(), nullptr, nullptr);
    }
    if (SUCCEEDED(result))
      result = operation->PerformOperations();
    BOOL aborted = FALSE;
    if (operation != nullptr)
      operation->GetAnyOperationsAborted(&aborted);
    if (destination != nullptr)
      destination->Release();
    if (operation != nullptr)
      operation->Release();
    if (SUCCEEDED(initialize))
      CoUninitialize();
    Notify(notifyWindow, notificationProcessId, operationId, result,
           aborted != FALSE);
  });
}

bool OpenPath(HWND owner, const std::wstring &path,
              const std::wstring &arguments,
              const std::wstring &workingDirectory, bool runAsAdministrator) {
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.hwnd = owner;
  info.lpVerb = runAsAdministrator ? L"runas" : L"open";
  info.lpFile = path.c_str();
  info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  info.lpDirectory =
      workingDirectory.empty() ? nullptr : workingDirectory.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

void ShowProperties(HWND owner, const std::wstring &path) {
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_INVOKEIDLIST;
  info.hwnd = owner;
  info.lpVerb = L"properties";
  info.lpFile = path.c_str();
  info.nShow = SW_SHOWNORMAL;
  ShellExecuteExW(&info);
}

} // namespace sf::win
