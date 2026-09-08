#include "core/Settings.h"
#include "core/TerminalCommand.h"
#include "win/AppMessages.h"
#include "win/ShellOperations.h"

#include <objbase.h>
#include <ole2.h>
#include <shlobj.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<sf::win::OperationResult> operationResult;

LRESULT CALLBACK TestWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  if (message == sf::win::kMessageOperationDone) {
    std::unique_ptr<sf::win::OperationResult> result(
        reinterpret_cast<sf::win::OperationResult *>(lParam));
    operationResult = *result;
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

bool WaitForOperation(sf::win::OperationId expectedId,
                      std::chrono::seconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!operationResult && std::chrono::steady_clock::now() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
  }
  return operationResult && operationResult->operationId == expectedId &&
         SUCCEEDED(operationResult->result) &&
         !operationResult->aborted;
}

void ResetOperation() {
  operationResult.reset();
}

bool WriteTestFile(const std::filesystem::path &path, std::string text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
  return static_cast<bool>(stream);
}

bool RunCmdInDirectory(const std::filesystem::path &directory) {
  std::wstring parameters =
      sf::BuildCmdUncParameters(directory.wstring());
  const std::size_t keep = parameters.find(L"/K");
  if (keep == std::wstring::npos)
    return false;
  parameters.replace(keep, 2, L"/C");
  parameters += L" & type nul > cmd-marker.txt";
  std::wstring commandLine = L"cmd.exe " + parameters;

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    return false;
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
  DWORD exitCode = 1;
  if (wait == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0 && exitCode == 0 &&
         std::filesystem::exists(directory / L"cmd-marker.txt");
}

// Minimal IEnumFORMATETC used by TestFileDataObject::EnumFormatEtc. OLE's
// clipboard calls this while OleSetClipboard is running to learn which
// formats to pre-register as delayed-render entries on the classic
// clipboard, which is what lets IsClipboardFormatAvailable see them later.
class TestFormatEnumerator : public IEnumFORMATETC {
public:
  explicit TestFormatEnumerator(std::vector<FORMATETC> formats)
      : formats_(std::move(formats)) {}

  HRESULT __stdcall QueryInterface(REFIID riid, void **object) override {
    if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
      *object = static_cast<IEnumFORMATETC *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override { return ++refCount_; }
  ULONG __stdcall Release() override {
    const ULONG count = --refCount_;
    if (count == 0)
      delete this;
    return count;
  }
  HRESULT __stdcall Next(ULONG count, FORMATETC *result,
                         ULONG *fetched) override {
    ULONG copied = 0;
    while (copied < count && index_ < formats_.size()) {
      result[copied] = formats_[index_];
      ++copied;
      ++index_;
    }
    if (fetched != nullptr)
      *fetched = copied;
    return copied == count ? S_OK : S_FALSE;
  }
  HRESULT __stdcall Skip(ULONG count) override {
    index_ += count;
    return S_OK;
  }
  HRESULT __stdcall Reset() override {
    index_ = 0;
    return S_OK;
  }
  HRESULT __stdcall Clone(IEnumFORMATETC **result) override {
    *result = new TestFormatEnumerator(formats_);
    return S_OK;
  }

private:
  std::vector<FORMATETC> formats_;
  std::size_t index_ = 0;
  std::atomic<ULONG> refCount_{1};
};

// A minimal IDataObject exposing CFSTR_FILEDESCRIPTORW / CFSTR_FILECONTENTS,
// the virtual-file clipboard formats Remote Desktop's clipboard redirection
// uses instead of CF_HDROP when copying files from the remote session. Used
// to exercise PasteFilesAsync's virtual-file branch without a real RDP
// session.
class TestFileDataObject : public IDataObject {
public:
  struct Entry {
    std::wstring name;
    bool isDirectory = false;
    std::string content;
    // Real clipboard sources may offer contents as TYMED_HGLOBAL instead of
    // TYMED_ISTREAM; set this to exercise WriteVirtualFileContents' other
    // branch.
    bool contentsAsHGlobal = false;
  };

  explicit TestFileDataObject(std::vector<Entry> entries)
      : entries_(std::move(entries)) {}

  HRESULT __stdcall QueryInterface(REFIID riid, void **object) override {
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *object = static_cast<IDataObject *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override { return ++refCount_; }
  ULONG __stdcall Release() override {
    const ULONG count = --refCount_;
    if (count == 0)
      delete this;
    return count;
  }

  HRESULT __stdcall GetData(FORMATETC *format, STGMEDIUM *medium) override {
    *medium = STGMEDIUM{};
    if (format->cfFormat == DescriptorFormat() &&
        (format->tymed & TYMED_HGLOBAL) != 0) {
      return BuildDescriptor(medium);
    }
    if (format->cfFormat == ContentsFormat())
      return BuildContents(format->lindex, medium);
    return DV_E_FORMATETC;
  }
  HRESULT __stdcall GetDataHere(FORMATETC *, STGMEDIUM *) override {
    return E_NOTIMPL;
  }
  HRESULT __stdcall QueryGetData(FORMATETC *format) override {
    if (format->cfFormat == DescriptorFormat() ||
        format->cfFormat == ContentsFormat()) {
      return S_OK;
    }
    return DV_E_FORMATETC;
  }
  HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC *,
                                          FORMATETC *) override {
    return E_NOTIMPL;
  }
  HRESULT __stdcall SetData(FORMATETC *, STGMEDIUM *, BOOL) override {
    return E_NOTIMPL;
  }
  HRESULT __stdcall EnumFormatEtc(DWORD direction,
                                  IEnumFORMATETC **result) override {
    if (direction != DATADIR_GET) {
      *result = nullptr;
      return E_NOTIMPL;
    }
    std::vector<FORMATETC> formats = {
        FORMATETC{static_cast<CLIPFORMAT>(DescriptorFormat()), nullptr,
                  DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
        FORMATETC{static_cast<CLIPFORMAT>(ContentsFormat()), nullptr,
                  DVASPECT_CONTENT, -1, TYMED_ISTREAM},
    };
    *result = new TestFormatEnumerator(std::move(formats));
    return S_OK;
  }
  HRESULT __stdcall DAdvise(FORMATETC *, DWORD, IAdviseSink *,
                            DWORD *) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT __stdcall DUnadvise(DWORD) override { return E_NOTIMPL; }
  HRESULT __stdcall EnumDAdvise(IEnumSTATDATA **) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

private:
  static UINT DescriptorFormat() {
    static const UINT format = RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    return format;
  }
  static UINT ContentsFormat() {
    static const UINT format = RegisterClipboardFormatW(CFSTR_FILECONTENTS);
    return format;
  }

  HRESULT BuildDescriptor(STGMEDIUM *medium) {
    const std::size_t count = entries_.size();
    const std::size_t bytes = sizeof(FILEGROUPDESCRIPTORW) +
                              (count > 0 ? count - 1 : 0) *
                                  sizeof(FILEDESCRIPTORW);
    const HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (global == nullptr)
      return E_OUTOFMEMORY;
    auto *group = static_cast<FILEGROUPDESCRIPTORW *>(GlobalLock(global));
    if (group == nullptr) {
      GlobalFree(global);
      return E_FAIL;
    }
    group->cItems = static_cast<UINT>(count);
    for (std::size_t index = 0; index < count; ++index) {
      wcsncpy_s(group->fgd[index].cFileName, entries_[index].name.c_str(),
                _TRUNCATE);
      if (entries_[index].isDirectory) {
        group->fgd[index].dwFlags = FD_ATTRIBUTES;
        group->fgd[index].dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      }
    }
    GlobalUnlock(global);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    medium->pUnkForRelease = nullptr;
    return S_OK;
  }

  HRESULT BuildContents(LONG index, STGMEDIUM *medium) {
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size())
      return DV_E_LINDEX;
    const Entry &entry = entries_[static_cast<std::size_t>(index)];
    if (entry.isDirectory)
      return DV_E_LINDEX;
    if (entry.contentsAsHGlobal) {
      const HGLOBAL global =
          GlobalAlloc(GMEM_MOVEABLE, entry.content.size());
      if (global == nullptr)
        return E_OUTOFMEMORY;
      void *data = GlobalLock(global);
      if (data == nullptr) {
        GlobalFree(global);
        return E_FAIL;
      }
      if (!entry.content.empty())
        memcpy(data, entry.content.data(), entry.content.size());
      GlobalUnlock(global);
      medium->tymed = TYMED_HGLOBAL;
      medium->hGlobal = global;
      medium->pUnkForRelease = nullptr;
      return S_OK;
    }
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
      return E_FAIL;
    if (!entry.content.empty()) {
      ULONG written = 0;
      stream->Write(entry.content.data(),
                    static_cast<ULONG>(entry.content.size()), &written);
    }
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    medium->tymed = TYMED_ISTREAM;
    medium->pstm = stream;
    return S_OK;
  }

  std::vector<Entry> entries_;
  std::atomic<ULONG> refCount_{1};
};

// Places entries on the OLE clipboard as virtual files (as Remote Desktop's
// clipboard redirection would) and drives PasteFilesAsync's virtual-file
// branch against them.
bool PasteVirtualFiles(HWND window, sf::win::OperationId operationId,
                       const std::wstring &destination,
                       std::vector<TestFileDataObject::Entry> entries,
                       sf::win::OverwriteConfirmFn confirmOverwrite) {
  auto *dataObject = new TestFileDataObject(std::move(entries));
  // OleSetClipboard wraps OpenClipboard, which can transiently fail with
  // CLIPBRD_E_CANT_OPEN if another thread/process (or Windows' own clipboard
  // bookkeeping from the previous test case) briefly holds it; retry rather
  // than treat that as a real failure.
  HRESULT setResult = E_FAIL;
  for (int attempt = 0; attempt < 25 && FAILED(setResult); ++attempt) {
    if (attempt > 0) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(15);
    }
    setResult = OleSetClipboard(dataObject);
  }
  dataObject->Release();
  if (FAILED(setResult))
    return false;
  std::jthread task = sf::win::PasteFilesAsync(
      window, operationId, destination, sf::win::ShowDuplicateConflictDialog,
      std::move(confirmOverwrite));
  const bool completed = WaitForOperation(operationId);
  task.join();
  OleSetClipboard(nullptr);
  return completed;
}

} // namespace

int main() {
  // OleSetClipboard/OleGetClipboard (used by the virtual-file paste tests
  // below) require OleInitialize, not just CoInitializeEx; OleInitialize
  // initializes COM itself (as COINIT_APARTMENTTHREADED), matching the
  // app's own main.cpp.
  const HRESULT initialized = OleInitialize(nullptr);
  if (FAILED(initialized)) {
    std::cerr << "COM initialization failed\n";
    return 1;
  }

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = TestWindowProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"SimpleFiler.FileOperationTestWindow";
  if (RegisterClassW(&windowClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::cerr << "Test window class registration failed\n";
    OleUninitialize();
    return 1;
  }
  const HWND window =
      CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  if (window == nullptr) {
    std::cerr << "Test message window creation failed\n";
    OleUninitialize();
    return 1;
  }

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("simplefiler-file-operations-" + sf::MakeStableId());
  const std::filesystem::path source = root / L"source";
  const std::filesystem::path destination = root / L"destination";
  std::error_code error;
  std::filesystem::create_directories(source, error);
  std::filesystem::create_directories(destination, error);

  int failures = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };

  const std::filesystem::path cmdDirectory =
      root / L"A&B 100% !bang ^(x)";
  std::filesystem::create_directories(cmdDirectory, error);
  check(RunCmdInDirectory(cmdDirectory),
        "CMD should preserve legal metacharacters in a quoted path");

  const std::filesystem::path copySource = source / L"copy-日本語.txt";
  check(WriteTestFile(copySource, "copy"), "Copy source should be created");
  ResetOperation();
  constexpr sf::win::OperationId copyOperationId = 1;
  std::jthread copyTask = sf::win::TransferFilesAsync(
      window, copyOperationId, {copySource.wstring()}, destination.wstring(),
      false);
  check(WaitForOperation(copyOperationId), "Direct shell copy should complete");
  copyTask.join();
  check(std::filesystem::exists(copySource),
        "Copy should preserve the source file");
  check(std::filesystem::exists(destination / copySource.filename()),
        "Copy should create the destination file");

  const std::filesystem::path moveSource = source / L"move.txt";
  check(WriteTestFile(moveSource, "move"), "Move source should be created");
  ResetOperation();
  constexpr sf::win::OperationId moveOperationId = 2;
  std::jthread moveTask = sf::win::TransferFilesAsync(
      window, moveOperationId, {moveSource.wstring()}, destination.wstring(),
      true);
  check(WaitForOperation(moveOperationId), "Direct shell move should complete");
  moveTask.join();
  check(!std::filesystem::exists(moveSource),
        "Move should remove the source file");
  check(std::filesystem::exists(destination / moveSource.filename()),
        "Move should create the destination file");

  ResetOperation();
  constexpr sf::win::OperationId folderOperationId = 3;
  std::jthread folderTask = sf::win::CreateFolderAsync(
      window, folderOperationId, destination.wstring(), L"new-folder");
  check(WaitForOperation(folderOperationId),
        "Shell folder creation should complete");
  folderTask.join();
  check(std::filesystem::is_directory(destination / L"new-folder"),
        "Shell folder creation should create a directory");

  const std::filesystem::path renameSource = destination / L"rename-before.txt";
  check(WriteTestFile(renameSource, "rename"),
        "Rename source should be created");
  ResetOperation();
  constexpr sf::win::OperationId renameOperationId = 4;
  std::jthread renameTask =
      sf::win::RenameFileAsync(window, renameOperationId,
                               renameSource.wstring(), L"rename-after.txt");
  check(WaitForOperation(renameOperationId), "Shell rename should complete");
  renameTask.join();
  check(!std::filesystem::exists(renameSource) &&
            std::filesystem::exists(destination / L"rename-after.txt"),
        "Shell rename should change the file name");

  const std::filesystem::path duplicateSource = destination / L"dup-日本語.txt";
  check(WriteTestFile(duplicateSource, "dup"),
        "Duplicate source should be created");
  ResetOperation();
  constexpr sf::win::OperationId duplicateOperationId = 5;
  std::jthread duplicateTask = sf::win::TransferFilesAsync(
      window, duplicateOperationId, {duplicateSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::CopyOnce;
      });
  check(WaitForOperation(duplicateOperationId),
        "Same-folder copy should complete instead of failing");
  duplicateTask.join();
  check(std::filesystem::exists(duplicateSource),
        "Same-folder copy should keep the original file");
  check(std::filesystem::exists(destination / L"dup-日本語 - コピー.txt"),
        "Same-folder copy should create a \"- コピー\" duplicate");

  ResetOperation();
  constexpr sf::win::OperationId duplicateAgainOperationId = 6;
  std::jthread duplicateAgainTask = sf::win::TransferFilesAsync(
      window, duplicateAgainOperationId, {duplicateSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::CopyOnce;
      });
  check(WaitForOperation(duplicateAgainOperationId),
        "Repeated same-folder copy should complete");
  duplicateAgainTask.join();
  check(std::filesystem::exists(destination / L"dup-日本語 - コピー (2).txt"),
        "Repeated same-folder copy should number the next duplicate");

  const std::filesystem::path applyAllSource =
      destination / L"apply-all.txt";
  check(WriteTestFile(applyAllSource, "apply-all"),
        "Apply-all source should be created");
  ResetOperation();
  int confirmCalls = 0;
  constexpr sf::win::OperationId applyAllOperationId = 7;
  std::jthread applyAllTask = sf::win::TransferFilesAsync(
      window, applyAllOperationId,
      {applyAllSource.wstring(), applyAllSource.wstring()},
      destination.wstring(), false,
      [&confirmCalls](HWND, const std::wstring &) {
        ++confirmCalls;
        return sf::win::DuplicateConflictChoice::ApplyToAll;
      });
  check(WaitForOperation(applyAllOperationId),
        "Same-folder copy batch with ApplyToAll should complete");
  applyAllTask.join();
  check(confirmCalls == 1,
        "ApplyToAll should suppress the prompt for the rest of the batch");
  check(std::filesystem::exists(destination / L"apply-all - コピー.txt") &&
            std::filesystem::exists(
                destination / L"apply-all - コピー (2).txt"),
        "ApplyToAll should duplicate every same-folder item in the batch");

  const std::filesystem::path cancelSource = destination / L"cancel.txt";
  check(WriteTestFile(cancelSource, "cancel"),
        "Cancel source should be created");
  ResetOperation();
  constexpr sf::win::OperationId cancelOperationId = 8;
  std::jthread cancelTask = sf::win::TransferFilesAsync(
      window, cancelOperationId, {cancelSource.wstring()},
      destination.wstring(), false,
      [](HWND, const std::wstring &) {
        return sf::win::DuplicateConflictChoice::Cancel;
      });
  const bool cancelled =
      WaitForOperation(cancelOperationId) == false && operationResult &&
      operationResult->operationId == cancelOperationId &&
      operationResult->aborted;
  check(cancelled, "Cancelling a same-folder conflict should abort the batch");
  cancelTask.join();
  check(!std::filesystem::exists(destination / L"cancel - コピー.txt"),
        "Cancelling should not create a duplicate for the conflicting file");

  // Remote Desktop's clipboard redirection never places CF_HDROP on the
  // clipboard when copying files from the remote session; it uses the
  // virtual-file formats CFSTR_FILEDESCRIPTORW/CFSTR_FILECONTENTS instead.
  // The following exercise PasteFilesAsync's fallback for that case.
  ResetOperation();
  constexpr sf::win::OperationId virtualFileOperationId = 9;
  check(PasteVirtualFiles(
            window, virtualFileOperationId, destination.wstring(),
            {{L"remote.txt", false, "from remote desktop"}},
            [](HWND, const std::wstring &) {
              return sf::win::OverwriteConflictChoice::Overwrite;
            }),
        "Pasting a virtual file (as Remote Desktop offers) should complete");
  check(std::filesystem::exists(destination / L"remote.txt"),
        "Virtual file paste should materialize the file at the destination");
  {
    std::ifstream stream(destination / L"remote.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
    check(content == "from remote desktop",
          "Virtual file paste should write the remote file's actual content");
  }

  ResetOperation();
  constexpr sf::win::OperationId virtualFileHGlobalOperationId = 14;
  check(PasteVirtualFiles(
            window, virtualFileHGlobalOperationId, destination.wstring(),
            {{L"remote-hglobal.txt", false, "hglobal contents", true}},
            [](HWND, const std::wstring &) {
              return sf::win::OverwriteConflictChoice::Overwrite;
            }),
        "Pasting a virtual file offered as TYMED_HGLOBAL should complete");
  {
    std::ifstream stream(destination / L"remote-hglobal.txt",
                         std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
    check(content == "hglobal contents",
          "TYMED_HGLOBAL contents should be written just like TYMED_ISTREAM");
  }

  ResetOperation();
  constexpr sf::win::OperationId virtualFolderOperationId = 10;
  check(PasteVirtualFiles(
            window, virtualFolderOperationId, destination.wstring(),
            {{L"remote-folder", true, ""},
             {L"remote-folder\\nested.txt", false, "nested contents"}},
            [](HWND, const std::wstring &) {
              return sf::win::OverwriteConflictChoice::Overwrite;
            }),
        "Pasting a virtual folder tree should complete");
  check(std::filesystem::is_directory(destination / L"remote-folder"),
        "Virtual folder paste should create the subfolder");
  check(std::filesystem::exists(destination / L"remote-folder" /
                                L"nested.txt"),
        "Virtual folder paste should create files nested inside the subfolder");

  ResetOperation();
  constexpr sf::win::OperationId virtualTraversalOperationId = 11;
  const bool traversalRejected =
      !PasteVirtualFiles(
          window, virtualTraversalOperationId, destination.wstring(),
          {{L"..\\escape.txt", false, "should not escape"}},
          [](HWND, const std::wstring &) {
            return sf::win::OverwriteConflictChoice::Overwrite;
          }) &&
      operationResult && FAILED(operationResult->result);
  check(traversalRejected,
        "A '..' entry in a virtual file descriptor should be rejected, not "
        "written outside the destination");
  check(!std::filesystem::exists(root / L"escape.txt"),
        "Rejecting a traversal entry should not create a file outside the "
        "destination");

  const std::filesystem::path overwriteTarget = destination / L"existing.txt";
  check(WriteTestFile(overwriteTarget, "original"),
        "Overwrite-conflict target should be created");
  ResetOperation();
  constexpr sf::win::OperationId virtualSkipOperationId = 12;
  check(PasteVirtualFiles(
            window, virtualSkipOperationId, destination.wstring(),
            {{L"existing.txt", false, "replacement"}},
            [](HWND, const std::wstring &) {
              return sf::win::OverwriteConflictChoice::Skip;
            }),
        "Skipping an overwrite conflict should still complete the batch");
  {
    std::ifstream stream(overwriteTarget, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
    check(content == "original",
          "Skip should leave the existing file's content untouched");
  }

  ResetOperation();
  constexpr sf::win::OperationId virtualCancelOperationId = 13;
  const bool overwriteCancelled =
      !PasteVirtualFiles(
          window, virtualCancelOperationId, destination.wstring(),
          {{L"existing.txt", false, "replacement"}},
          [](HWND, const std::wstring &) {
            return sf::win::OverwriteConflictChoice::Cancel;
          }) &&
      operationResult && operationResult->aborted;
  check(overwriteCancelled,
        "Cancelling an overwrite conflict should abort the virtual paste");
  {
    std::ifstream stream(overwriteTarget, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
    check(content == "original",
          "Cancelling an overwrite conflict should leave the file untouched");
  }

  DestroyWindow(window);
  std::filesystem::remove_all(root, error);
  OleUninitialize();
  if (failures == 0)
    std::cout << "Windows file operation tests passed\n";
  return failures == 0 ? 0 : 1;
}
