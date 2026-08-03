#include "win/App.h"
#include "win/AppMessages.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kSingleInstanceMutexName[] = L"SimpleFiler.SingleInstanceMutex";

// Forwards initialPath to an already-running SimpleFiler window and asks it
// to come to the foreground. Returns true if an existing window was found
// and notified.
bool ForwardToExistingInstance(const std::wstring &initialPath) {
  const HWND existing = FindWindowW(sf::win::kMainWindowClass, nullptr);
  if (existing == nullptr)
    return false;

  DWORD processId = 0;
  GetWindowThreadProcessId(existing, &processId);
  if (processId != 0)
    AllowSetForegroundWindow(processId);

  COPYDATASTRUCT copyData{};
  copyData.dwData = sf::win::kOpenPathCopyDataId;
  copyData.cbData =
      static_cast<DWORD>(initialPath.size() * sizeof(wchar_t));
  copyData.lpData =
      initialPath.empty()
          ? nullptr
          : const_cast<wchar_t *>(initialPath.c_str());
  SendMessageW(existing, WM_COPYDATA, 0,
              reinterpret_cast<LPARAM>(&copyData));
  return true;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&controls);
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  std::wstring initialPath;
  int argumentCount = 0;
  wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments != nullptr && argumentCount > 1)
    initialPath = arguments[1];
  if (arguments != nullptr)
    LocalFree(arguments);

  // Intentionally never closed: the handle must stay open (and this process
  // must remain the owner) for the lifetime of the process so later launches
  // observe ERROR_ALREADY_EXISTS. Released automatically by the OS on exit.
  const HANDLE singleInstanceMutex =
      CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
  const bool alreadyRunning =
      singleInstanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
  if (alreadyRunning) {
    // The existing process may still be starting up (window not created
    // yet) when launched again in quick succession. Retry briefly before
    // giving up and starting a second instance.
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (ForwardToExistingInstance(initialPath)) {
        if (SUCCEEDED(initialized))
          CoUninitialize();
        return 0;
      }
      Sleep(50);
    }
  }

  int result = 0;
  {
    sf::win::App app(instance);
    result = app.Run(showCommand, initialPath);
  }
  if (SUCCEEDED(initialized))
    CoUninitialize();
  return result;
}
