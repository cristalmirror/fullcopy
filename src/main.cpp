#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include "Window.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&commonControls);

    // Needed by the folder selection dialog.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int exitCode = 1;
    {
        fullcopy::Window window(instance);
        if (window.create(showCommand))
        {
            exitCode = window.run();
        }
        else
        {
            MessageBoxW(nullptr, L"The main window could not be created.", L"FullCopy", MB_OK | MB_ICONERROR);
        }
    }

    if (SUCCEEDED(comResult))
    {
        CoUninitialize();
    }
    return exitCode;
}
