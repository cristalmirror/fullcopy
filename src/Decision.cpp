#include "Decision.hpp"

#include <string>

#include "Common.hpp"
#include "Copier.hpp"

namespace fullcopy {

    const wchar_t* Decision::CLASS_NAME = L"FullCopyDecisionWindow";

    namespace {

        const int CLIENT_WIDTH  = 480;
        const int CLIENT_HEIGHT = 210;

    } // namespace

    Decision::Decision(HINSTANCE instance, Copier& copier, std::size_t entryIndex)
    : instance_(instance),
      copier_(copier),
      entryIndex_(entryIndex),
      parent_(nullptr),
      window_(nullptr),
      textBox_(nullptr),
      acceptButton_(nullptr),
      renamed_(false),
      running_(false) {}

    Decision::~Decision() {
        if (window_ != nullptr) {
            selfDestroy();
        }
    }

    bool Decision::show(HWND parent) {
        parent_ = parent;
        registerClass(instance_);

        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD extendedStyle = WS_EX_DLGMODALFRAME;

        RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
        AdjustWindowRectEx(&rect, style, FALSE, extendedStyle);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;

        // Centered over the main window.
        RECT parentRect;
        GetWindowRect(parent_, &parentRect);
        const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

        window_ = CreateWindowExW(extendedStyle, CLASS_NAME, L"Name too long", style,
                              x, y, width, height, parent_, nullptr, instance_, this);
        if (window_ == nullptr) {
            return false;
        }

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
            if (result == -1) {
                break;
            }

            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (window_ != nullptr) {
            selfDestroy();
        }
        return renamed_;
    }

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

    void Decision::registerClass(HINSTANCE instance) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = CLASS_NAME;

        // Fails harmlessly when the class is already registered.
        RegisterClassExW(&windowClass);
    }

    LRESULT CALLBACK Decision::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        Decision* self = nullptr;

        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Decision*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        } else {
            self = reinterpret_cast<Decision*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->window_ = nullptr;
            return DefWindowProcW(window, message, wParam, lParam);
        }

        return self->handleMessage(message, wParam, lParam);
    }

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

    void Decision::createControls() {
        const CopyEntry &entry = copier_.getEntry(entryIndex_);
        const std::wstring currentName = copier_.getDestinationName(entryIndex_);
        const std::size_t maxLength = copier_.getMaxNameLength(entryIndex_);

        const std::wstring text = std::wstring(entry.isDirectory ? L"This directory name" : L"This file name")
                                    + L" is too long to be copied (" 
                                    + std::to_wstring(copier_.getTempSize())
                                    + L" characters in the full path):\r\n\r\n" 
                                    + currentName
                                    + L"\r\n\r\nWrite a new name (" 
                                    + std::to_wstring(maxLength) 
                                    + L" characters maximum):";

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

    textBox_ = CreateWindowExW( WS_EX_CLIENTEDGE,
                                L"EDIT",
                                currentName.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                12,
                                130,
                                456, 
                                24, 
                                window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DECISION_TEXT_BOX)),
                                instance_, 
                                nullptr);

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

    void Decision::onAccept() {
        const std::wstring newName = getWindowString(textBox_);
        std::wstring error;

        switch (copier_.rename(entryIndex_, newName)) {
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
                error = L"The name is still too long. Use " + std::to_wstring(copier_.getMaxNameLength(entryIndex_))
                        + L" characters or less (it has " + std::to_wstring(newName.size()) + L").";
            break;

            case RenameResult::AlreadyExists:
            error = L"Another file or directory already has this name.";
            break;
        }

        MessageBoxW(window_, error.c_str(), L"Invalid name", MB_OK | MB_ICONWARNING);
        SetFocus(textBox_);
        SendMessageW(textBox_, EM_SETSEL, 0, -1);
    }

} // namespace fullcopy
