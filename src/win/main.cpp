#include "win/App.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <string>

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

  sf::win::App app(instance);
  const int result = app.Run(showCommand, initialPath);
  if (SUCCEEDED(initialized))
    CoUninitialize();
  return result;
}
