#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include "Controller.hpp"

// Entry point of the program: prepares the common controls and COM, creates the Controller
// (which creates the window) and runs it until the window is closed.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Register the common controls used by the window, like the progress bar.
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&commonControls);

    // Needed by the folder selection dialog.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // The block destroys the Controller (and its window) before COM is released.
    int exitCode = 1;
    {
        fullcopy::Controller controller(instance);
        if (controller.create(showCommand)) {
            exitCode = controller.run();
        } else {
            MessageBoxW(nullptr, L"The main window could not be created.", L"FullCopy", MB_OK | MB_ICONERROR);
        }
    }

    // Release COM only if it was initialized here.
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return exitCode;
}
