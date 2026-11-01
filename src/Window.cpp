#include "Window.hpp"

#include <commctrl.h>
#include <shobjidl.h>

#include "Common.hpp"
#include "Controller.hpp"
#include "Copier.hpp"

namespace fullcopy {

    const wchar_t *Window::CLASS_NAME = L"FullCopyMainWindow";

    namespace {

        const int CLIENT_WIDTH  = 522;
        const int CLIENT_HEIGHT = 244;

    } // namespace

    Window::Window(HINSTANCE instance, Controller &controller)
        : instance_(instance),
        controller_(controller),
        window_(nullptr),
        loadPercentage_(0),
        sourceDirectory_(nullptr),
        destinationDirectory_(nullptr),
        acceptButton_(nullptr),
        browseButton_(nullptr),
        browseDestinationButton_(nullptr),
        startButton_(nullptr),
        progressBar_(nullptr),
        pathLabel_(nullptr),
        statusLabel_(nullptr) {
    }

    bool Window::create(int showCommand) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = CLASS_NAME;

        if (RegisterClassExW(&windowClass) == 0) {
            return false;
        }

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
        
        AdjustWindowRect(&rect, style, FALSE);

        CreateWindowExW(0, 
                        CLASS_NAME, 
                        L"FullCopy", 
                        style, CW_USEDEFAULT, 
                        CW_USEDEFAULT,
                        rect.right - rect.left, 
                        rect.bottom - rect.top,
                        nullptr, 
                        nullptr, 
                        instance_, 
                        this);

        if (window_ == nullptr) {
            return false;
        }

        createControls();
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
        return true;
    }

    int Window::run() {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (window_ == nullptr || !IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return static_cast<int>(message.wParam);
    }

    void Window::updateProgressBar(int percentage) {
        if (percentage < 0) {
            percentage = 0;
        }
        if (percentage > 100) {
            percentage = 100;
        }

        loadPercentage_ = percentage;
        SendMessageW(progressBar_, 
                     PBM_SETPOS, 
                     static_cast<WPARAM>(loadPercentage_),
                     0);

        setStatus(L"Copying... " + std::to_wstring(loadPercentage_) + L"%");
    }

    LRESULT CALLBACK Window::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return dispatchWindowMessage(window,
                                     message, 
                                     wParam, 
                                     lParam,
                                     &Window::window_, 
                                     &Window::handleMessage);
    }

    LRESULT Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case ID_BROWSE_SOURCE_BUTTON:
                        onBrowse(sourceDirectory_);
                        return 0;
                    case ID_BROWSE_DESTINATION_BUTTON:
                        onBrowse(destinationDirectory_);
                        return 0;
                    case IDOK: // Enter key.
                    case ID_START_BUTTON:
                        if (IsWindowEnabled(startButton_)) {
                            controller_.onStart();
                        }
                    return 0;
            case ID_ACCEPT_BUTTON:
                onAccept();
                return 0;
            }
        break;

        // Messages from the copy thread: the Controller handles them on this thread.
        case WM_APP_PROGRESS:
            controller_.onProgress(static_cast<int>(wParam));
            return 0;

        case WM_APP_RENAME_REQUEST:
            return controller_.onRenameRequest(static_cast<std::size_t>(wParam)) ? TRUE : FALSE;

        case WM_APP_COPY_FINISHED:
            controller_.onCopyFinished(static_cast<std::size_t>(wParam), static_cast<std::size_t>(lParam));
            return 0;

        case WM_APP_COPY_FAILED:
            controller_.onCopyFailed();
            return 0;

        case WM_APP_CURRENT_ENTRY:
            controller_.onCurrentEntry(static_cast<std::size_t>(wParam));
            return 0;

        case WM_CLOSE:
            controller_.onClose();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void Window::createControls() {
        auto createControl = [this](DWORD extendedStyle, 
                                    const wchar_t *className,
                                    const wchar_t *text,
                                    DWORD style, 
                                    int x, 
                                    int y, 
                                    int width, 
                                    int height, 
                                    int id) {
                                            return CreateWindowExW( extendedStyle, 
                                                                    className,
                                                                    text, 
                                                                    WS_CHILD | WS_VISIBLE | style,
                                                                    x, 
                                                                    y, 
                                                                    width, 
                                                                    height, 
                                                                    window_,
                                                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), 
                                                                    instance_, 
                                                                    nullptr);
                                            };

        createControl(0, L"STATIC", L"Source directory:", 0, 12, 12, 400, 18, -1);
        sourceDirectory_ = createControl(   WS_EX_CLIENTEDGE, 
                                        L"EDIT", 
                                        L"", 
                                        WS_TABSTOP | ES_AUTOHSCROLL,
                                        12,
                                        32, 
                                        400, 
                                        24, 
                                        ID_SOURCE_EDIT);
        browseButton_ = createControl(  0, 
                                    L"BUTTON", 
                                    L"Browse...", 
                                    WS_TABSTOP | BS_PUSHBUTTON,
                                    420, 
                                    31, 
                                    90, 
                                    26, 
                                    ID_BROWSE_SOURCE_BUTTON);

        createControl(0, L"STATIC", L"Destination directory:", 0, 12, 66, 400, 18, -1);
        destinationDirectory_ = createControl(  WS_EX_CLIENTEDGE, 
                                            L"EDIT", 
                                            L"", 
                                            WS_TABSTOP | ES_AUTOHSCROLL,
                                            12, 
                                            86, 
                                            400, 
                                            24, 
                                            ID_DESTINATION_EDIT);
        browseDestinationButton_ = createControl(0, 
                                            L"BUTTON", 
                                            L"Browse...", 
                                            WS_TABSTOP | BS_PUSHBUTTON,
                                            420, 
                                            85, 
                                            90, 
                                            26, 
                                            ID_BROWSE_DESTINATION_BUTTON);

        progressBar_ = createControl(0, 
                                PROGRESS_CLASSW,
                                nullptr, 
                                PBS_SMOOTH, 
                                12, 
                                124, 
                                498, 
                                20, 
                                ID_PROGRESS_BAR);
        SendMessageW(progressBar_, PBM_SETRANGE32, 0, 100);

        pathLabel_ = createControl(0,
                                L"STATIC",
                                L"",
                                SS_NOPREFIX | SS_PATHELLIPSIS,
                                12,
                                150,
                                498,
                                18,
                                ID_PATH_LABEL);

        statusLabel_ = createControl(0, 
                                L"STATIC", 
                                L"Select the directories and press Start.",
                                SS_NOPREFIX | SS_ENDELLIPSIS, 
                                12, 
                                174, 
                                498, 
                                18, 
                                ID_STATUS_LABEL);

        startButton_ = createControl(0, 
                                L"BUTTON", 
                                L"Start", 
                                WS_TABSTOP | BS_DEFPUSHBUTTON,
                                320, 
                                204, 
                                90, 
                                28, 
                                ID_START_BUTTON);
        acceptButton_ = createControl(0, 
                                L"BUTTON", 
                                L"Accept", 
                                WS_TABSTOP | BS_PUSHBUTTON, 
                                420, 
                                204, 
                                90, 
                                28, 
                                ID_ACCEPT_BUTTON);

        applyUiFont(window_);
    }

    void Window::onBrowse(HWND targetEdit) {
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            showError(L"The folder dialog could not be opened.");
            return;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(targetEdit == sourceDirectory_ ? L"Select the source directory"
                                                    : L"Select the destination directory");

        if (SUCCEEDED(dialog->Show(window_))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    SetWindowTextW(targetEdit, path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }

        dialog->Release();
    }

    void Window::close() {
        DestroyWindow(window_);
    }

    HWND Window::getHandle() const {
        return window_;
    }

    std::wstring Window::getSourceDirectory() const {
        return getWindowString(sourceDirectory_);
    }

    std::wstring Window::getDestinationDirectory() const {
        return getWindowString(destinationDirectory_);
    }

    bool Window::askNewName(const RenameRequest &request) {
        decision_.reset(new Decision(instance_, controller_, request));
        const bool renamed = decision_->show(window_);
        decision_.reset();

        return renamed;
    }

    void Window::setCurrentPath(const std::wstring &path) {
        SetWindowTextW(pathLabel_, path.c_str());
    }

    void Window::showDirectoryError(DirectoryResult result) {
        switch (result) {
            case DirectoryResult::Ok:
                return;
            case DirectoryResult::Empty:
                showError(L"Select both the source and the destination directories.");
                return;
            case DirectoryResult::SourceNotFound:
                showError(L"The source directory does not exist.");
                return;
            case DirectoryResult::SameDirectory:
                showError(L"The source and the destination directories must be different.");
                return;
            case DirectoryResult::DestinationInsideSource:
                showError(L"The destination directory can not be inside the source directory.");
                return;
        }
    }

    void Window::showCopyStarted() {
        setControlsEnabled(false);
        updateProgressBar(0);
        setStatus(L"Reading the source directory...");
        setCurrentPath(L"");
    }

    void Window::showStartFailed() {
        setControlsEnabled(true);
        showError(L"The copy could not be started.");
    }

    void Window::showCancelling() {
        setStatus(L"Cancelling...");
    }

    void Window::showCopyFinished(std::size_t failed, std::size_t skipped) {
        restoreControls();
        setStatus(L"Finished.");

        std::wstring text = L"The copy has finished.";
        if (skipped > 0) {
            text += L"\n\nNot copied (skipped by the user or impossible to shorten): "
                 + std::to_wstring(skipped) + L" item(s), with all their content.";
        }
        if (failed > 0) {
            text += L"\n\nItems that could not be copied because of an error: " + std::to_wstring(failed) + L".";
        }

        const UINT icon = (failed > 0 || skipped > 0) ? MB_ICONWARNING : MB_ICONINFORMATION;
        MessageBoxW(window_, text.c_str(), L"FullCopy", MB_OK | icon);
    }

    void Window::showCopyFailed() {
        restoreControls();
        setStatus(L"The copy could not be done.");
        showError(L"The copy could not be completed. "
                  L"Some destination files or directories may already have been created.");
    }

    void Window::onAccept() {
        SendMessageW(window_, WM_CLOSE, 0, 0);
    }

    // Leaves the window ready for a new copy.
    void Window::restoreControls() {
        setControlsEnabled(true);
        setCurrentPath(L"");
    }

    void Window::setControlsEnabled(bool enabled) {
        const BOOL value = enabled ? TRUE : FALSE;
        EnableWindow(sourceDirectory_, value);
        EnableWindow(destinationDirectory_, value);
        EnableWindow(browseButton_, value);
        EnableWindow(browseDestinationButton_, value);
        EnableWindow(startButton_, value);
        EnableWindow(acceptButton_, value);
    }

    void Window::setStatus(const std::wstring &text) {
        SetWindowTextW(statusLabel_, text.c_str());
    }

    void Window::showError(const std::wstring &text) {
        MessageBoxW(window_, text.c_str(), L"FullCopy", MB_OK | MB_ICONERROR);
    }

} // namespace fullcopy
