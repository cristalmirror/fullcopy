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

    // Only stores the values. The window and its controls are created by create().
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

    // Registers the window class, creates the main window with its controls and shows it.
    bool Window::create(int showCommand) {
        // Describe the window class: procedure, icon, cursor and background.
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

        // Fixed-size window: compute the outer size that gives the wanted client area.
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
        
        AdjustWindowRect(&rect, style, FALSE);

        // Passing this object lets windowProcedure find it (window_ is set during WM_NCCREATE).
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

    // Message loop of the program. IsDialogMessageW makes Tab and Enter work like in a dialog box.
    // Returns the exit code given to PostQuitMessage.
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

    // Moves the progress bar and shows the percentage in the status line.
    void Window::updateProgressBar(int percentage) {
        // Keep the value between 0 and 100.
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

    // Window procedure of the class: finds the Window object of the window and passes it
    // the message.
    LRESULT CALLBACK Window::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return dispatchWindowMessage(window,
                                     message, 
                                     wParam, 
                                     lParam,
                                     &Window::window_, 
                                     &Window::handleMessage);
    }

    // Turns each Windows message into an action. The view handles its own controls, and
    // everything else goes to the Controller.
    LRESULT Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            // Buttons and keys.
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

        // X button, Alt+F4 or Accept button.
        case WM_CLOSE:
            controller_.onClose();
            return 0;

        // The window is gone: end the message loop.
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(window_, message, wParam, lParam);
    }

    // Creates every control of the main window at a fixed position.
    void Window::createControls() {
        // Helper that creates one child control of this window with the given id.
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

        // Source directory: label, text box and Browse button.
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

        // Destination directory: label, text box and Browse button.
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

        // Progress bar, from 0 to 100.
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

        // Path being copied. SS_PATHELLIPSIS shortens long paths with "...".
        pathLabel_ = createControl(0,
                                L"STATIC",
                                L"",
                                SS_NOPREFIX | SS_PATHELLIPSIS,
                                12,
                                150,
                                498,
                                18,
                                ID_PATH_LABEL);

        // Status line.
        statusLabel_ = createControl(0, 
                                L"STATIC", 
                                L"Select the directories and press Start.",
                                SS_NOPREFIX | SS_ENDELLIPSIS, 
                                12, 
                                174, 
                                498, 
                                18, 
                                ID_STATUS_LABEL);

        // Start and Accept buttons.
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

        // Use the system font in every control.
        applyUiFont(window_);
    }

    // Browse button: opens the folder selection dialog and writes the chosen folder in targetEdit.
    void Window::onBrowse(HWND targetEdit) {
        // Create the Windows folder dialog (a COM object).
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            showError(L"The folder dialog could not be opened.");
            return;
        }

        // Pick only existing folders of the file system.
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(targetEdit == sourceDirectory_ ? L"Select the source directory"
                                                    : L"Select the destination directory");

        // Show it. If the user picks a folder, write its path in the text box.
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

        // COM objects are released with Release(), and the strings they return with CoTaskMemFree().
        dialog->Release();
    }

    // Destroys the window. WM_DESTROY then ends the message loop.
    void Window::close() {
        DestroyWindow(window_);
    }

    // Handle of the main window, used by the copy thread to send it messages.
    HWND Window::getHandle() const {
        return window_;
    }

    // Text of the source text box, as the user wrote it.
    std::wstring Window::getSourceDirectory() const {
        return getWindowString(sourceDirectory_);
    }

    // Text of the destination text box, as the user wrote it.
    std::wstring Window::getDestinationDirectory() const {
        return getWindowString(destinationDirectory_);
    }

    // Opens the Decision window for an entry whose name is too long and waits until it is closed.
    // Returns true if the user renamed the entry.
    bool Window::askNewName(const RenameRequest &request) {
        decision_.reset(new Decision(instance_, controller_, request));
        const bool renamed = decision_->show(window_);
        decision_.reset();

        return renamed;
    }

    // Shows the path being copied under the progress bar.
    void Window::setCurrentPath(const std::wstring &path) {
        SetWindowTextW(pathLabel_, path.c_str());
    }

    // Explains to the user why the directories can not be used.
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
            case DirectoryResult::DestinationUnavailable:
                showError(L"The destination directory could not be resolved or accessed.");
                return;
            case DirectoryResult::SameDirectory:
                showError(L"The source and the destination directories must be different.");
                return;
            case DirectoryResult::DestinationInsideSource:
                showError(L"The destination directory can not be inside the source directory.");
                return;
        }
    }

    // Disables the controls during the copy and shows that the source is being read.
    void Window::showCopyStarted() {
        setControlsEnabled(false);
        updateProgressBar(0);
        setStatus(L"Reading the source directory...");
        setCurrentPath(L"");
    }

    // The copy thread could not be created: enables the controls again and shows an error.
    void Window::showStartFailed() {
        setControlsEnabled(true);
        showError(L"The copy could not be started.");
    }

    // Shows that the copy is being cancelled.
    void Window::showCancelling() {
        setStatus(L"Cancelling...");
    }

    // Enables the controls again and shows a summary of the copy.
    void Window::showCopyFinished(std::size_t failed, std::size_t skipped) {
        restoreControls();
        setStatus(L"Finished.");

        // Add the skipped and the failed items to the summary, when there are any.
        std::wstring text = L"The copy has finished.";
        if (skipped > 0) {
            text += L"\n\nNot copied (skipped by the user or impossible to shorten): "
                 + std::to_wstring(skipped) + L" item(s), with all their content.";
        }
        if (failed > 0) {
            text += L"\n\nItems that could not be copied because of an error: " + std::to_wstring(failed) + L".";
        }

        // Warning icon if something was not copied.
        const UINT icon = (failed > 0 || skipped > 0) ? MB_ICONWARNING : MB_ICONINFORMATION;
        MessageBoxW(window_, text.c_str(), L"FullCopy", MB_OK | icon);
    }

    // Enables the controls again and explains that the copy could not be completed.
    void Window::showCopyFailed() {
        restoreControls();
        setStatus(L"The copy could not be done.");
        showError(L"The copy could not be completed. "
                  L"Some destination files or directories may already have been created.");
    }

    // Accept button: closes the window, like the X button.
    void Window::onAccept() {
        SendMessageW(window_, WM_CLOSE, 0, 0);
    }

    // Leaves the window ready for a new copy.
    void Window::restoreControls() {
        setControlsEnabled(true);
        setCurrentPath(L"");
    }

    // Enables or disables the text boxes and the buttons.
    void Window::setControlsEnabled(bool enabled) {
        const BOOL value = enabled ? TRUE : FALSE;
        EnableWindow(sourceDirectory_, value);
        EnableWindow(destinationDirectory_, value);
        EnableWindow(browseButton_, value);
        EnableWindow(browseDestinationButton_, value);
        EnableWindow(startButton_, value);
        EnableWindow(acceptButton_, value);
    }

    // Writes a text in the status line.
    void Window::setStatus(const std::wstring &text) {
        SetWindowTextW(statusLabel_, text.c_str());
    }

    // Shows an error message over the main window.
    void Window::showError(const std::wstring &text) {
        MessageBoxW(window_, text.c_str(), L"FullCopy", MB_OK | MB_ICONERROR);
    }

} // namespace fullcopy
