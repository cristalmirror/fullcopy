#include "Decision.hpp"

#include <string>

#include "Common.hpp"
#include "Controller.hpp"

namespace fullcopy {

    const wchar_t *Decision::CLASS_NAME = L"FullCopyDecisionWindow";

    namespace {

        const int CLIENT_WIDTH  = 480;
        const int CLIENT_HEIGHT = 210;

    } // namespace

    // Only stores the data. The window is created by show().
    Decision::Decision(HINSTANCE instance, Controller &controller, const RenameRequest &request)
    : instance_(instance),
      controller_(controller),
      request_(request),
      parent_(nullptr),
      window_(nullptr),
      textBox_(nullptr),
      acceptButton_(nullptr),
      renamed_(false),
      running_(false) {}

    // Makes sure the window does not outlive this object.
    Decision::~Decision() {
        if (window_ != nullptr) {
            selfDestroy();
        }
    }

    // Creates the window centered over the parent, disables the parent, and runs its own
    // message loop until the window is closed. Returns true if the entry was renamed.
    bool Decision::show(HWND parent) {
        parent_ = parent;
        if (!registerClass(instance_)) {
            MessageBoxW(parent_, L"The rename window could not be opened.", L"FullCopy",
                        MB_OK | MB_ICONERROR);
            return false;
        }

        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD extendedStyle = WS_EX_DLGMODALFRAME;

        // Outer size of the window that gives the wanted client area.
        RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
        AdjustWindowRectEx(&rect, style, FALSE, extendedStyle);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;

        // Centered over the main window.
        RECT parentRect;
        GetWindowRect(parent_, &parentRect);
        const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

        // Passing this object lets windowProcedure find it (window_ is set during WM_NCCREATE).
        window_ = CreateWindowExW(extendedStyle, CLASS_NAME, L"Name too long", style,
                              x, y, width, height, parent_, nullptr, instance_, this);
        if (window_ == nullptr) {
            return false;
        }

        // Disable the parent so it can not be used while this window is open (modal), and select
        // the current name so the user can type over it.
        createControls();
        ShowWindow(window_, SW_SHOW);
        EnableWindow(parent_, FALSE);
        SetFocus(textBox_);
        SendMessageW(textBox_, EM_SETSEL, 0, -1);

        // Modal loop: runs until the window destroys itself.
        running_ = true;
        MSG message;
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
        return renamed_;
    }

    // Ends the modal loop, gives the control back to the parent window and destroys this one.
    void Decision::selfDestroy() {
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

    // Registers the window class. A class registered by an earlier Decision window is reused.
    bool Decision::registerClass(HINSTANCE instance) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = CLASS_NAME;

        return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    // Window procedure of the class: finds the Decision object of the window and passes it
    // the message.
    LRESULT CALLBACK Decision::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return dispatchWindowMessage(window, message, wParam, lParam,
                                     &Decision::window_, &Decision::handleMessage);
    }

    // Handles the messages of the window: Accept or Enter applies the name, and Escape or
    // the X button closes the window, so the entry is skipped.
    LRESULT Decision::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case IDOK: // Accept button or Enter key.
                        onAccept();
                        return 0;
                    case IDCANCEL: // Escape key.
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

    // Creates the explanation, the text box with the current name and the Accept button.
    void Decision::createControls() {
        // Explain which name is too long and how long the new one can be.
        const std::wstring text = std::wstring(request_.isDirectory ? L"This directory name" : L"This file name")
                                    + L" is too long to be copied (" 
                                    + std::to_wstring(request_.pathLength)
                                    + L" characters in the full path):\r\n\r\n" 
                                    + request_.currentName
                                    + L"\r\n\r\nWrite a new name (" 
                                    + std::to_wstring(request_.maxNameLength) 
                                    + L" characters maximum):";

    // Explanation text.
    CreateWindowExW(0,
                    L"STATIC",
                    text.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
                    12,
                    12,
                    456,
                    110,
                    window_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DECISION_LABEL)),
                    instance_,
                    nullptr);

    // Text box, filled with the current name.
    textBox_ = CreateWindowExW( WS_EX_CLIENTEDGE,
                                L"EDIT",
                                request_.currentName.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                12,
                                130,
                                456, 
                                24, 
                                window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DECISION_TEXT_BOX)),
                                instance_, 
                                nullptr);

    // Accept button. Its IDOK id also lets the Enter key press it.
    acceptButton_ = CreateWindowExW(0, 
                                    L"BUTTON",
                                    L"Accept",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    378, 
                                    168, 
                                    90, 
                                    28, 
                                    window_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), 
                                    instance_, 
                                    nullptr);

        applyUiFont(window_);
    }

    // Accept button: asks the Controller to rename the entry. On success the window closes;
    // otherwise it explains the error and lets the user try again.
    void Decision::onAccept() {
        const std::wstring newName = getWindowString(textBox_);
        std::wstring error;

        // Turn each result into a message for the user.
        switch (controller_.rename(request_.entryIndex, newName)) {
            case RenameResult::Ok:
                renamed_ = true;
                selfDestroy();
                return;

            case RenameResult::Empty:
                error = L"The name can not be empty.";
                break;

            case RenameResult::InvalidName:
                error = L"The name is not valid. It can not contain any of these characters:\n"
                        L"\\ / : * ? \" < > |\n"
                        L"It can not end with a space or a dot, nor be a name reserved by Windows (CON, NUL, ...).";
            break;

            case RenameResult::TooLong:
                error = L"The name is still too long. Use " + std::to_wstring(request_.maxNameLength)
                        + L" characters or less (it has " + std::to_wstring(newName.size()) + L").";
            break;

            case RenameResult::AlreadyExists:
            error = L"Another file or directory already has this name.";
            break;

            case RenameResult::DestinationUnavailable:
                error = L"The destination could not be checked. "
                        L"Verify its availability and your permissions.";
                break;
        }

        // Show the error and select the text, so the user can correct it.
        MessageBoxW(window_, error.c_str(), L"Invalid name", MB_OK | MB_ICONWARNING);
        SetFocus(textBox_);
        SendMessageW(textBox_, EM_SETSEL, 0, -1);
    }

} // namespace fullcopy
