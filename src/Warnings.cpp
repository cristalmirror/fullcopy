#include "Warnings.hpp"

#include "Common.hpp"

namespace fullcopy {

    const wchar_t *Warnings::CLASS_NAME = L"FullCopyWarningsWindow";

    namespace {

        const int CLIENT_WIDTH  = 480;
        const int CLIENT_HEIGHT = 250;

    } // namespace

    // Only stores the data. The window is created by show().
    Warnings::Warnings(HINSTANCE instance, WarningKind kind, const std::vector<std::wstring> &paths)
        : instance_(instance),
        kind_(kind),
        paths_(paths),
        parent_(nullptr),
        window_(nullptr),
        acceptButton_(nullptr),
        running_(false) {
    }

    // Makes sure the window does not outlive this object.
    Warnings::~Warnings() {
        if (window_ != nullptr) {
            selfDestroy();
        }
    }

    // Creates the window centered over the parent, disables the parent, and runs its own
    // message loop until the user accepts.
    void Warnings::show(HWND parent) {
        parent_ = parent;
        if (!registerClass(instance_)) {
            showMessageBox();
            return;
        }

        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD extendedStyle = WS_EX_DLGMODALFRAME;

        // Outer size of the window that gives the wanted client area.
        RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
        AdjustWindowRectEx(&rect, style, FALSE, extendedStyle);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;

        // Centered over the main window.
        RECT parentRect = {};
        GetWindowRect(parent_, &parentRect);
        const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

        // Passing this object lets windowProcedure find it (window_ is set during WM_NCCREATE).
        window_ = CreateWindowExW(extendedStyle, CLASS_NAME, getTitle(), style,
                                  x, y, width, height, parent_, nullptr, instance_, this);
        if (window_ == nullptr) {
            showMessageBox();
            return;
        }
        if (!createControls()) {
            selfDestroy();
            showMessageBox();
            return;
        }

        // Disable the parent so it can not be used while this window is open (modal).
        ShowWindow(window_, SW_SHOW);
        EnableWindow(parent_, FALSE);
        SetFocus(acceptButton_);

        // Modal loop: runs until the window destroys itself.
        running_ = true;
        MSG message = {};
        while (running_) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == 0) {
                // Give WM_QUIT back to the main loop.
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            // GetMessageW failed.
            if (result == -1) {
                break;
            }

            // IsDialogMessageW handles Tab, Enter and Escape like in a dialog box.
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        // The loop can also end without destroying the window (WM_QUIT or an error).
        if (window_ != nullptr) {
            selfDestroy();
        }
    }

    // Ends the modal loop, gives the control back to the parent window and destroys this one.
    void Warnings::selfDestroy() {
        running_ = false;

        // Enable the parent before destroying, so Windows gives it the focus back.
        if (parent_ != nullptr) {
            EnableWindow(parent_, TRUE);
        }

        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }

        if (parent_ != nullptr) {
            SetForegroundWindow(parent_);
        }
    }

    // Registers the window class. A class registered by an earlier Warnings window is reused.
    bool Warnings::registerClass(HINSTANCE instance) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = CLASS_NAME;

        return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    // Window procedure of the class: finds the Warnings object of the window and passes it
    // the message.
    LRESULT CALLBACK Warnings::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return dispatchWindowMessage(window, message, wParam, lParam,
                                     &Warnings::window_, &Warnings::handleMessage);
    }

    // Accept, Enter, Escape and the X button all close the window. The paths are not copied
    // in any case, so there is nothing else to choose.
    LRESULT Warnings::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                    selfDestroy();
                    return 0;
                }
                break;

            case WM_CLOSE:
                selfDestroy();
                return 0;
        }

        return DefWindowProcW(window_, message, wParam, lParam);
    }

    // Creates the text, the read-only list of paths and the Accept button.
    // Returns false if any of them could not be created.
    bool Warnings::createControls() {
        // Helper that creates one child control of this window with the given id.
        const auto createControl = [this](DWORD extendedStyle, const wchar_t *className, const wchar_t *text,
                                          DWORD style, int x, int y, int width, int height, int id) {
            return CreateWindowExW(extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        };

        const HWND label = createControl(0, L"STATIC", getText(), SS_LEFT | SS_NOPREFIX,
                                         12, 12, 456, 40, ID_WARNINGS_LABEL);

        // The user can select and copy the paths, but not change them.
        const std::wstring pathList = getPathList();
        const HWND list = createControl(WS_EX_CLIENTEDGE, L"EDIT", pathList.c_str(),
                                        WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE
                                        | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                        12, 60, 456, 136, ID_WARNINGS_PATH_LIST);

        // Accept button. Its IDOK id also lets the Enter key press it.
        acceptButton_ = createControl(0, L"BUTTON", L"Aceptar", WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      378, 210, 90, 28, IDOK);

        applyUiFont(window_);
        return label != nullptr && list != nullptr && acceptButton_ != nullptr;
    }

    // Used when the window can not be created, so the warning is never lost.
    void Warnings::showMessageBox() const {
        const std::wstring text = std::wstring(getText()) + L"\r\n\r\n" + getPathList();
        MessageBoxW(parent_, text.c_str(), getTitle(), MB_OK | MB_ICONWARNING);
    }

    // Title of the window.
    const wchar_t *Warnings::getTitle() const {
        switch (kind_) {
            case WarningKind::InaccessibleReference:
                return L"Referencia Inaccesible";
            case WarningKind::Recursion:
                return L"Recursion";
        }
        return L"FullCopy";
    }

    // Explanation shown above the list of paths.
    const wchar_t *Warnings::getText() const {
        switch (kind_) {
            case WarningKind::InaccessibleReference:
                return L"Referencia Inaccesible: el archivo o directorio no es accesible, "
                       L"pertenece a OneDrive u otro dispositivo.";
            case WarningKind::Recursion:
                return L"Recursion: el archivo se autoreferencia en loop infinito, ¿acepta ignorarlo?";
        }
        return L"";
    }

    // One path per line, as the edit control expects it.
    std::wstring Warnings::getPathList() const {
        std::wstring list;
        for (const std::wstring &path : paths_) {
            if (!list.empty()) {
                list += L"\r\n";
            }
            list += path;
        }
        return list;
    }

} // namespace fullcopy
