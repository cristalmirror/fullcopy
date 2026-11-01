// UI tests: they drive the real Controller, Window and Decision windows.
//
// The main thread runs the Controller and its message loop, as the program does. A driver
// thread plays the user: it writes in the text boxes, presses the buttons, answers the
// Name too long window and reads the message boxes. It only uses window messages, so every
// window keeps running on the main thread.
#include "Common.hpp"
#include "Controller.hpp"
#include "Copier.hpp"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using fullcopy::Controller;
using fullcopy::Copier;
using fullcopy::ID_ACCEPT_BUTTON;
using fullcopy::ID_DECISION_LABEL;
using fullcopy::ID_DECISION_TEXT_BOX;
using fullcopy::ID_DESTINATION_EDIT;
using fullcopy::ID_PATH_LABEL;
using fullcopy::ID_SOURCE_EDIT;
using fullcopy::ID_START_BUTTON;
using fullcopy::ID_STATUS_LABEL;

// Window classes, and the id of the text inside a standard message box.
const wchar_t *const MAIN_CLASS = L"FullCopyMainWindow";
const wchar_t *const DECISION_CLASS = L"FullCopyDecisionWindow";
const wchar_t *const MESSAGE_BOX_CLASS = L"#32770";
const int MESSAGE_BOX_TEXT_ID = 0xFFFF;

const ULONGLONG TIMEOUT_MS = 30000; // Longest wait for a window or a state.
const DWORD POLL_MS = 20;
const UINT SEND_TIMEOUT_MS = 10000;

// Lengths used to build a name that fits in the source but not in the destination.
const std::size_t DESTINATION_LENGTH = 200;
const std::size_t LONG_NAME_LENGTH = 80;

const std::wstring INITIAL_STATUS = L"Select the directories and press Start.";
const std::wstring FINISHED_TEXT = L"The copy has finished.";

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Converts a wide string to UTF-8, to print it in an error message.
std::string narrow(const std::wstring &text) {
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &result[0], size, nullptr, nullptr);
    return result;
}

bool contains(const std::wstring &text, const std::wstring &part) {
    return text.find(part) != std::wstring::npos;
}

bool startsWith(const std::wstring &text, const std::wstring &prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::wstring upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return text;
}

// Temporary source and destination directories, removed at the end of the test.
struct Fixture {
    fs::path root;
    fs::path source;
    fs::path destination;

    Fixture() {
        wchar_t temporary[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathW(MAX_PATH + 1, temporary);
        require(length > 0 && length <= MAX_PATH, "GetTempPathW failed");
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::path(temporary) / (L"fullcopy-ui-test-" + std::to_wstring(GetCurrentProcessId())
                   + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (fs::create_directory(root)) {
                source = root / L"source";
                destination = root / L"destination";
                fs::create_directory(source);
                return;
            }
        }
        throw std::runtime_error("Could not create isolated fixture");
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void write(const fs::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(stream), "Fixture write failed");
}

std::string read(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Copied file missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Makes the destination root DESTINATION_LENGTH characters long and adds two items to the
// source: "keep.txt" and an item with a name of LONG_NAME_LENGTH characters. The long name fits
// in the source, but its destination path is too long, so the Name too long window appears.
void prepareLongName(Fixture &fixture, bool directory) {
    const std::wstring prefix = L"destination-";
    const std::size_t used = fixture.root.wstring().size() + 1 + prefix.size();
    require(used + 10 <= DESTINATION_LENGTH, "The temporary directory path is too long for this test");
    fixture.destination = fixture.root / (prefix + std::wstring(DESTINATION_LENGTH - used, L'd'));
    require(fixture.destination.wstring().size() == DESTINATION_LENGTH, "Unexpected destination length");

    write(fixture.source / L"keep.txt", "keep");
    const fs::path item = fixture.source / std::wstring(LONG_NAME_LENGTH, L'n');
    if (directory) {
        fs::create_directory(item);
        write(item / L"child.txt", "child");
    } else {
        write(item, "long");
    }
}

// Called by EnumThreadWindows for every top-level window of the main thread.
BOOL CALLBACK collectWindow(HWND window, LPARAM lParam) {
    reinterpret_cast<std::vector<HWND>*>(lParam)->push_back(window);
    return TRUE;
}

std::wstring className(HWND window) {
    wchar_t name[256] = {};
    const int length = GetClassNameW(window, name, 256);
    return std::wstring(name, length > 0 ? static_cast<std::size_t>(length) : 0);
}

// Sends a message to a window of the main thread, failing instead of waiting forever.
LRESULT send(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(window, message, wParam, lParam, SMTO_NORMAL, SEND_TIMEOUT_MS, &result) == 0) {
        throw std::runtime_error("A window did not answer message " + std::to_string(message));
    }
    return static_cast<LRESULT>(result);
}

// Presses the button of a message box (FullCopy only shows message boxes with one button).
// The id of that button is not always IDOK, so the command uses the real id and handle of the
// button, like a mouse click does. Without a button, it asks the box to close.
void pressMessageBoxButton(HWND box) {
    const HWND button = FindWindowExW(box, nullptr, L"Button", nullptr);
    if (button == nullptr) {
        PostMessageW(box, WM_CLOSE, 0, 0);
        return;
    }
    PostMessageW(box, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED), reinterpret_cast<LPARAM>(button));
}

// Plays the user from the driver thread.
class Driver {
public:
    struct MessageBoxInfo {
        HWND window;
        std::wstring caption;
        std::wstring text;
    };

    explicit Driver(DWORD uiThread) : uiThread_(uiThread), main_(nullptr) {}

    // Finds the main window created by the Controller.
    void attach() {
        main_ = waitFor(MAIN_CLASS, "main window");
    }

    HWND mainWindow() const {
        return main_;
    }

    HWND control(int id) const {
        return GetDlgItem(main_, id);
    }

    // Top-level windows of the main thread: the main window, the Decision window and message boxes.
    std::vector<HWND> windows() const {
        std::vector<HWND> result;
        EnumThreadWindows(uiThread_, collectWindow, reinterpret_cast<LPARAM>(&result));
        return result;
    }

    // First visible window of the class, or nullptr.
    HWND find(const wchar_t *windowClass) const {
        for (HWND window : windows()) {
            if (IsWindowVisible(window) && className(window) == windowClass) {
                return window;
            }
        }
        return nullptr;
    }

    void waitUntil(const std::function<bool()> &condition, const std::string &what) const {
        const ULONGLONG deadline = GetTickCount64() + TIMEOUT_MS;
        while (!condition()) {
            require(GetTickCount64() < deadline, "Timed out waiting for " + what);
            Sleep(POLL_MS);
        }
    }

    HWND waitFor(const wchar_t *windowClass, const std::string &what) const {
        HWND window = nullptr;
        waitUntil([&] {
            window = find(windowClass);
            return window != nullptr;
        }, "the " + what);
        return window;
    }

    void waitGone(HWND window, const std::string &what) const {
        waitUntil([window] { return IsWindow(window) == FALSE; }, "the " + what + " to close");
    }

    std::wstring getText(HWND window) const {
        const LRESULT length = send(window, WM_GETTEXTLENGTH, 0, 0);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        const LRESULT copied = send(window, WM_GETTEXT, static_cast<WPARAM>(length) + 1,
                                    reinterpret_cast<LPARAM>(&text[0]));
        text.resize(static_cast<std::size_t>(copied));
        return text;
    }

    void setText(HWND window, const std::wstring &text) const {
        send(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    // Presses a button of the main window. With IDOK it works like the Enter key.
    void click(int id) const {
        PostMessageW(main_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(control(id)));
    }

    void startCopy(const std::wstring &source, const std::wstring &destination) const {
        setText(control(ID_SOURCE_EDIT), source);
        setText(control(ID_DESTINATION_EDIT), destination);
        click(ID_START_BUTTON);
    }

    MessageBoxInfo waitMessageBox() const {
        const HWND box = waitFor(MESSAGE_BOX_CLASS, "message box");
        return {box, getText(box), getText(GetDlgItem(box, MESSAGE_BOX_TEXT_ID))};
    }

    // Presses the button of a message box and waits until it is gone. If the box is still open
    // after a moment, it also asks it to close, like the X button.
    void closeMessageBox(HWND box) const {
        pressMessageBoxButton(box);
        const ULONGLONG fallback = GetTickCount64() + 2000;
        while (IsWindow(box) != FALSE && GetTickCount64() < fallback) {
            Sleep(POLL_MS);
        }
        if (IsWindow(box) != FALSE) {
            PostMessageW(box, WM_CLOSE, 0, 0);
        }
        waitGone(box, "message box");
    }

    // Fails if a message box appears during the given time.
    void expectNoMessageBox(DWORD milliseconds) const {
        const ULONGLONG deadline = GetTickCount64() + milliseconds;
        while (GetTickCount64() < deadline) {
            failOnMessageBox();
            Sleep(POLL_MS);
        }
    }

    // Writes a name in the Decision window and presses Accept. The error must start with
    // errorStart, and the Decision window must stay open after it.
    void expectRenameError(HWND decision, const std::wstring &name, const std::wstring &errorStart) const {
        setText(GetDlgItem(decision, ID_DECISION_TEXT_BOX), name);
        PostMessageW(decision, WM_COMMAND, IDOK, 0);
        const MessageBoxInfo box = waitMessageBox();
        require(box.caption == L"Invalid name", "Unexpected message box for " + narrow(name) + ": " + narrow(box.text));
        require(startsWith(box.text, errorStart), "Unexpected error for " + narrow(name) + ": " + narrow(box.text));
        closeMessageBox(box.window);
        require(IsWindow(decision) != FALSE, "The Decision window closed after an error");
    }

    // Presses Accept in the main window, which closes the program.
    void closeWithAccept() const {
        click(ID_ACCEPT_BUTTON);
        waitGone(main_, "main window");
    }

    // Waits until the main window is destroyed. Fails if a message box appears meanwhile.
    void waitClosedWithoutMessageBox() const {
        waitUntil([this] {
            failOnMessageBox();
            return IsWindow(main_) == FALSE;
        }, "the main window to close");
    }

    // Closes every window that is left, so the message loop of the main thread ends even when a
    // test fails in the middle. Ends the process if the program does not close.
    void forceClose() const {
        const ULONGLONG deadline = GetTickCount64() + 2 * TIMEOUT_MS;
        while (true) {
            bool mainOpen = false;
            for (HWND window : windows()) {
                const std::wstring name = className(window);
                if (name == MESSAGE_BOX_CLASS) {
                    pressMessageBoxButton(window);
                    PostMessageW(window, WM_CLOSE, 0, 0);
                } else if (name == DECISION_CLASS) {
                    PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                } else if (name == MAIN_CLASS) {
                    mainOpen = true;
                    PostMessageW(window, WM_CLOSE, 0, 0);
                }
            }
            if (!mainOpen) {
                return;
            }
            if (GetTickCount64() > deadline) {
                std::cerr << "FATAL: the program did not close" << std::endl;
                ExitProcess(2);
            }
            Sleep(200);
        }
    }

private:
    void failOnMessageBox() const {
        const HWND box = find(MESSAGE_BOX_CLASS);
        if (box != nullptr) {
            throw std::runtime_error("Unexpected message box: " + narrow(getText(GetDlgItem(box, MESSAGE_BOX_TEXT_ID))));
        }
    }

    DWORD uiThread_;
    HWND main_;
};

// Data shared by the main thread and the driver thread of one test.
struct Session {
    DWORD uiThread;
    std::function<void(Driver&)> script;
    std::string error;
};

// Entry point of the driver thread: runs the script, then closes whatever is left.
DWORD WINAPI driverThread(LPVOID parameter) {
    auto *session = static_cast<Session*>(parameter);
    Driver driver(session->uiThread);
    try {
        driver.attach();
        session->script(driver);
    } catch (const std::exception &error) {
        session->error = error.what();
    } catch (...) {
        session->error = "Unknown exception in the driver thread";
    }
    driver.forceClose();
    return 0;
}

// Every test creates the windows again, so their classes must not stay registered.
void unregisterClasses() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    UnregisterClassW(MAIN_CLASS, instance);
    UnregisterClassW(DECISION_CLASS, instance);
}

// Runs the program on this thread and the script on a driver thread, until the window closes.
void runWithUi(const std::function<void(Driver&)> &script) {
    unregisterClasses();
    Session session = {GetCurrentThreadId(), script, std::string()};
    {
        Controller controller(GetModuleHandleW(nullptr));
        require(controller.create(SW_SHOWNOACTIVATE), "The main window could not be created");

        const HANDLE thread = CreateThread(nullptr, 0, driverThread, &session, 0, nullptr);
        if (thread == nullptr) {
            DestroyWindow(FindWindowW(MAIN_CLASS, nullptr));
            controller.run();
            unregisterClasses();
            throw std::runtime_error("The driver thread could not be started");
        }

        controller.run();
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    unregisterClasses();
    require(session.error.empty(), session.error);
}

// Every invalid start shows its error and changes nothing.
void rejectedStarts() {
    Fixture f;
    const std::wstring source = f.source.wstring();
    const std::wstring destination = f.destination.wstring();
    struct Case {
        std::wstring source;
        std::wstring destination;
        std::wstring error;
    };
    const Case cases[] = {
        {L"", L"", L"Select both the source and the destination directories."},
        {L"   ", destination, L"Select both the source and the destination directories."},
        {source, L"\"\"", L"Select both the source and the destination directories."},
        {(f.root / L"missing").wstring(), destination, L"The source directory does not exist."},
        {source, upper(source) + L"\\", L"The source and the destination directories must be different."},
        {source, (f.source / L"inner").wstring(), L"The destination directory can not be inside the source directory."}
    };

    runWithUi([&cases](Driver &d) {
        for (const Case &c : cases) {
            d.startCopy(c.source, c.destination);
            const Driver::MessageBoxInfo box = d.waitMessageBox();
            require(box.caption == L"FullCopy" && box.text == c.error,
                    "Unexpected error for \"" + narrow(c.source) + "\": " + narrow(box.text));
            d.closeMessageBox(box.window);
            require(IsWindowEnabled(d.control(ID_START_BUTTON)) != FALSE, "Controls disabled after a rejected start");
            require(d.getText(d.control(ID_STATUS_LABEL)) == INITIAL_STATUS, "A rejected start changed the status");
        }
        d.closeWithAccept();
    });

    require(!fs::exists(f.destination) && !fs::exists(f.source / L"inner"), "A rejected start created a directory");
}

// A copy with quoted paths: the Controller trims them, copies everything and shows the summary.
// After the program closes, the destination can be deleted at once.
void copyWithQuotedPaths() {
    Fixture f;
    fs::create_directories(f.source / L"nested" / L"empty");
    write(f.source / L"nested" / L"file.txt", "nested");
    write(f.source / L"top.txt", "top");

    runWithUi([&f](Driver &d) {
        d.startCopy(L"  \"" + f.source.wstring() + L"\"  ", L"\"" + f.destination.wstring() + L"\"");
        const Driver::MessageBoxInfo box = d.waitMessageBox();
        require(box.caption == L"FullCopy" && box.text == FINISHED_TEXT, "Unexpected summary: " + narrow(box.text));
        require(d.getText(d.control(ID_STATUS_LABEL)) == L"Finished.", "Unexpected status after the copy");
        require(d.getText(d.control(ID_PATH_LABEL)).empty(), "The path label was not cleared");
        require(IsWindowEnabled(d.control(ID_START_BUTTON)) != FALSE, "Controls still disabled after the copy");
        d.closeMessageBox(box.window);
        d.closeWithAccept();
    });

    require(read(f.destination / L"top.txt") == "top", "Top file not copied");
    require(read(f.destination / L"nested" / L"file.txt") == "nested", "Nested file not copied");
    require(fs::is_directory(f.destination / L"nested" / L"empty"), "Empty directory not copied");
    require(fs::remove_all(f.destination) == 5, "Destination could not be deleted after closing");
}

// The Name too long window shows the right data, rejects wrong names without closing, and a
// valid name lets the copy go on. Start and Enter are ignored while the copy runs.
void renameLongName() {
    Fixture f;
    prepareLongName(f, false);
    const std::size_t fullLength = DESTINATION_LENGTH + 1 + LONG_NAME_LENGTH;
    const std::size_t maxName = Copier::MAX_FILE_PATH_LENGTH - (DESTINATION_LENGTH + 1);

    runWithUi([&](Driver &d) {
        d.startCopy(f.source.wstring(), f.destination.wstring());
        const HWND decision = d.waitFor(DECISION_CLASS, "Name too long window");

        const std::wstring label = d.getText(GetDlgItem(decision, ID_DECISION_LABEL));
        require(startsWith(label, L"This file name is too long"), "Unexpected explanation: " + narrow(label));
        require(contains(label, L"(" + std::to_wstring(fullLength) + L" characters in the full path)"),
                "Wrong path length in: " + narrow(label));
        require(contains(label, L"(" + std::to_wstring(maxName) + L" characters maximum)"),
                "Wrong maximum length in: " + narrow(label));
        require(d.getText(GetDlgItem(decision, ID_DECISION_TEXT_BOX)) == std::wstring(LONG_NAME_LENGTH, L'n'),
                "The text box does not show the current name");
        require(IsWindowEnabled(d.control(ID_START_BUTTON)) == FALSE, "Start enabled during a copy");

        d.click(ID_START_BUTTON);
        d.click(IDOK);
        d.expectNoMessageBox(300);

        d.expectRenameError(decision, L"bad:name", L"The name is not valid.");
        d.expectRenameError(decision, std::wstring(maxName + 1, L'x'),
                            L"The name is still too long. Use " + std::to_wstring(maxName)
                            + L" characters or less (it has " + std::to_wstring(maxName + 1) + L").");
        d.expectRenameError(decision, L"KEEP.TXT", L"Another file or directory already has this name.");

        d.setText(GetDlgItem(decision, ID_DECISION_TEXT_BOX), L"short.txt");
        PostMessageW(decision, WM_COMMAND, IDOK, 0);
        d.waitGone(decision, "Name too long window");

        const Driver::MessageBoxInfo box = d.waitMessageBox();
        require(box.text == FINISHED_TEXT, "Unexpected summary: " + narrow(box.text));
        d.closeMessageBox(box.window);
        d.closeWithAccept();
    });

    require(read(f.destination / L"short.txt") == "long", "Renamed file not copied");
    require(read(f.destination / L"keep.txt") == "keep", "Other file not copied");
    require(fs::remove_all(f.destination) == 3, "Unexpected destination content after the rename");
}

// Closing the Name too long window skips the directory with all its content, and the summary
// counts it.
void skipLongDirectory() {
    Fixture f;
    prepareLongName(f, true);
    const std::size_t maxName = Copier::MAX_DIRECTORY_PATH_LENGTH - (DESTINATION_LENGTH + 1);

    runWithUi([&](Driver &d) {
        d.startCopy(f.source.wstring(), f.destination.wstring());
        const HWND decision = d.waitFor(DECISION_CLASS, "Name too long window");

        const std::wstring label = d.getText(GetDlgItem(decision, ID_DECISION_LABEL));
        require(startsWith(label, L"This directory name is too long"), "Unexpected explanation: " + narrow(label));
        require(contains(label, L"(" + std::to_wstring(maxName) + L" characters maximum)"),
                "Wrong maximum length in: " + narrow(label));

        PostMessageW(decision, WM_COMMAND, IDCANCEL, 0); // Escape key.
        d.waitGone(decision, "Name too long window");

        const Driver::MessageBoxInfo box = d.waitMessageBox();
        require(startsWith(box.text, FINISHED_TEXT)
                && contains(box.text, L"Not copied (skipped by the user or impossible to shorten): 1 item(s)")
                && !contains(box.text, L"because of an error"),
                "Unexpected summary: " + narrow(box.text));
        d.closeMessageBox(box.window);
        d.closeWithAccept();
    });

    require(read(f.destination / L"keep.txt") == "keep", "Other file not copied");
    require(fs::remove_all(f.destination) == 2, "The skipped directory or its content was copied");
}

// Closing the program while the Name too long window is open cancels the copy. The main window
// waits for the copy thread, then closes without showing the summary.
void closeDuringRename() {
    Fixture f;
    prepareLongName(f, false);

    runWithUi([&f](Driver &d) {
        d.startCopy(f.source.wstring(), f.destination.wstring());
        const HWND decision = d.waitFor(DECISION_CLASS, "Name too long window");

        PostMessageW(d.mainWindow(), WM_CLOSE, 0, 0);
        d.waitUntil([&d] { return d.getText(d.control(ID_STATUS_LABEL)) == L"Cancelling..."; },
                    "the Cancelling status");
        require(IsWindow(d.mainWindow()) != FALSE, "The window closed before the copy thread stopped");

        PostMessageW(decision, WM_COMMAND, IDCANCEL, 0);
        d.waitClosedWithoutMessageBox();
    });

    // Nothing may keep the destination open after the program ends.
    fs::remove_all(f.destination);
    require(!fs::exists(f.destination), "Destination could not be deleted after closing");
}

int main() {
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&commonControls);

    unsigned failures = 0;
    const struct { const char *name; void (*run)(); } tests[] = {
        {"rejected starts show their error", rejectedStarts},
        {"copy with quoted paths and folder release", copyWithQuotedPaths},
        {"rename a long name in the Decision window", renameLongName},
        {"skip a long directory with its content", skipLongDirectory},
        {"close while the Decision window is open", closeDuringRename}
    };
    for (const auto &test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << std::endl;
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << std::endl;
        }
    }
    return failures == 0 ? 0 : 1;
}
