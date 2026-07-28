#include "win/ShellOperations.h"

#include "win/AppMessages.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstring>
#include <memory>
#include <thread>

namespace sf::win {
namespace {

constexpr wchar_t kPreferredDropEffect[] = L"Preferred DropEffect";

void Notify(HWND window, HRESULT result, bool aborted) {
  auto *operation = new OperationResult{result, aborted};
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

void RunPaste(HWND notifyWindow, std::wstring destination,
              std::vector<std::wstring> paths, bool move) {
  const HRESULT initialize = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IFileOperation *operation = CreateOperation(notifyWindow, true);
  IShellItem *destinationItem = ItemFromPath(destination);
  HRESULT result =
      operation != nullptr && destinationItem != nullptr ? S_OK : E_FAIL;
  if (SUCCEEDED(result)) {
    for (const std::wstring &path : paths) {
      IShellItem *source = ItemFromPath(path);
      if (source == nullptr) {
        result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        break;
      }
      result =
          move ? operation->MoveItem(source, destinationItem, nullptr, nullptr)
               : operation->CopyItem(source, destinationItem, nullptr, nullptr);
      source->Release();
      if (FAILED(result))
        break;
    }
  }
  if (SUCCEEDED(result))
    result = operation->PerformOperations();
  BOOL aborted = FALSE;
  if (operation != nullptr)
    operation->GetAnyOperationsAborted(&aborted);
  if (destinationItem != nullptr)
    destinationItem->Release();
  if (operation != nullptr)
    operation->Release();
  if (SUCCEEDED(initialize))
    CoUninitialize();
  Notify(notifyWindow, result, aborted != FALSE);
}

} // namespace

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

std::vector<std::wstring> ReadFilesFromClipboard(bool *cut) {
  std::vector<std::wstring> paths;
  if (cut != nullptr)
    *cut = false;
  if (!OpenClipboard(nullptr))
    return paths;
  const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (drop != nullptr) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT index = 0; index < count; ++index) {
      const UINT length = DragQueryFileW(drop, index, nullptr, 0);
      std::wstring path(static_cast<std::size_t>(length + 1), L'\0');
      DragQueryFileW(drop, index, path.data(), length + 1);
      path.resize(length);
      paths.push_back(std::move(path));
    }
  }
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

void PasteFilesAsync(HWND notifyWindow, const std::wstring &destination) {
  bool cut = false;
  std::vector<std::wstring> paths = ReadFilesFromClipboard(&cut);
  if (paths.empty()) {
    Notify(notifyWindow, HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS), false);
    return;
  }
  std::thread(RunPaste, notifyWindow, destination, std::move(paths), cut)
      .detach();
}

void DeleteFilesAsync(HWND notifyWindow, std::vector<std::wstring> paths,
                      bool permanent) {
  std::thread([notifyWindow, paths = std::move(paths), permanent]() {
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
    Notify(notifyWindow, result, aborted != FALSE);
  }).detach();
}

void RenameFileAsync(HWND notifyWindow, std::wstring path,
                     std::wstring newName) {
  std::thread([notifyWindow, path = std::move(path),
               newName = std::move(newName)] {
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
    Notify(notifyWindow, result, aborted != FALSE);
  }).detach();
}

void CreateFolderAsync(HWND notifyWindow, std::wstring parent,
                       std::wstring name) {
  std::thread([notifyWindow, parent = std::move(parent),
               name = std::move(name)] {
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
    Notify(notifyWindow, result, aborted != FALSE);
  }).detach();
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
