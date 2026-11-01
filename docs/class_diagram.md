# FullCopy - Class Diagram

Class diagram of the current code (`include/` and `src/`). Everything lives in the `fullcopy` namespace.

```mermaid
classDiagram
    direction TB

    class Controller {
        -Copier copier_
        -Window window_
        -HANDLE workerThread_
        -atomic_bool cancelRequested_
        -bool closePending_
        +Controller(HINSTANCE instance)
        +~Controller()
        +create(int showCommand) bool
        +run() int
        +onStart() void
        +onClose() void
        +onProgress(int percentage) void
        +onRenameRequest(size_t entryIndex) bool
        +onCurrentEntry(size_t entryIndex) void
        +onCopyFinished(size_t failed, size_t skipped) void
        +onCopyFailed() void
        +rename(size_t entryIndex, wstring newName) RenameResult
        -copyThread(LPVOID parameter)$ DWORD
        -runCopy() void
        -finishCopy() bool
        -finishWorker() void
    }

    class Window {
        -HINSTANCE instance_
        -Controller& controller_
        -HWND window_
        -int loadPercentage_
        -HWND sourceDirectory_
        -HWND destinationDirectory_
        -HWND acceptButton_
        -HWND browseButton_
        -HWND browseDestinationButton_
        -HWND startButton_
        -HWND progressBar_
        -HWND pathLabel_
        -HWND statusLabel_
        -unique_ptr~Decision~ decision_
        +Window(HINSTANCE instance, Controller& controller)
        +create(int showCommand) bool
        +run() int
        +close() void
        +getHandle() HWND
        +getSourceDirectory() wstring
        +getDestinationDirectory() wstring
        +askNewName(RenameRequest request) bool
        +updateProgressBar(int percentage) void
        +setCurrentPath(wstring path) void
        +showDirectoryError(DirectoryResult result) void
        +showCopyStarted() void
        +showStartFailed() void
        +showCancelling() void
        +showCopyFinished(size_t failed, size_t skipped) void
        +showCopyFailed() void
        -windowProcedure(HWND, UINT, WPARAM, LPARAM)$ LRESULT
        -handleMessage(UINT, WPARAM, LPARAM) LRESULT
        -createControls() void
        -onBrowse(HWND targetEdit) void
        -onAccept() void
        -restoreControls() void
        -setControlsEnabled(bool enabled) void
        -setStatus(wstring text) void
        -showError(wstring text) void
    }

    class Decision {
        -HINSTANCE instance_
        -Controller& controller_
        -RenameRequest request_
        -HWND parent_
        -HWND window_
        -HWND textBox_
        -HWND acceptButton_
        -bool renamed_
        -bool running_
        +Decision(HINSTANCE instance, Controller& controller, RenameRequest request)
        +~Decision()
        +show(HWND parent) bool
        +selfDestroy() void
        -registerClass(HINSTANCE instance)$ void
        -windowProcedure(HWND, UINT, WPARAM, LPARAM)$ LRESULT
        -handleMessage(UINT, WPARAM, LPARAM) LRESULT
        -createControls() void
        -onAccept() void
    }

    class RenameRequest {
        <<struct>>
        +size_t entryIndex
        +bool isDirectory
        +wstring currentName
        +size_t pathLength
        +size_t maxNameLength
    }

    class Copier {
        +MAX_FILE_PATH_LENGTH$ size_t = 259
        +MAX_DIRECTORY_PATH_LENGTH$ size_t = 247
        +MAX_NAME_LENGTH$ size_t = 255
        -bool sizeApproved_
        -size_t tempSize_
        -wstring sourceRoot_
        -wstring destinationRoot_
        -vector~CopyEntry~ entries_
        +Copier()
        +setDirectories(wstring sourceRoot, wstring destinationRoot) void
        +checkDirectories() DirectoryResult
        +load(atomic_bool* cancellation = nullptr) bool
        +check(size_t index) bool
        +copy(size_t index, atomic_bool* cancellation = nullptr) bool
        +rename(size_t index, wstring newName) RenameResult
        +skip(size_t index) void
        +getEntryCount() size_t
        +getEntry(size_t index) CopyEntry
        +getDestinationName(size_t index) wstring
        +getFullDestinationPath(size_t index) wstring
        +getMaxNameLength(size_t index) size_t
        +getSourceRoot() wstring
        +getDestinationRoot() wstring
        +isSizeApproved() bool
        +getTempSize() size_t
        -getPathLengthLimit(size_t index) size_t
        -scanDirectory(wstring relativePath, atomic_bool* cancellation) bool
        -getFullSourcePath(wstring relativePath) wstring
        -normalizePath(wstring path)$ wstring
        -isValidName(wstring name)$ bool
        -startsWith(wstring text, wstring prefix)$ bool
    }

    class CopyEntry {
        <<struct>>
        +wstring sourcePath
        +wstring destinationPath
        +bool isDirectory
        +bool skipped
    }

    class RenameResult {
        <<enumeration>>
        Ok
        Empty
        InvalidName
        TooLong
        AlreadyExists
        DestinationUnavailable
    }

    class DirectoryResult {
        <<enumeration>>
        Ok
        Empty
        SourceNotFound
        SameDirectory
        DestinationInsideSource
    }

    class Common {
        <<utility>>
        +ControlId enum
        +WM_APP_PROGRESS$ UINT
        +WM_APP_RENAME_REQUEST$ UINT
        +WM_APP_COPY_FINISHED$ UINT
        +WM_APP_COPY_FAILED$ UINT
        +WM_APP_CURRENT_ENTRY$ UINT
        +equalsIgnoreCase(wstring first, wstring second) bool
        +joinPath(wstring base, wstring part) wstring
        +dispatchWindowMessage~Owner~(HWND, UINT, WPARAM, LPARAM, windowMember, handler) LRESULT
        +getUiFont() HFONT
        +applyUiFont(HWND window) void
        +getWindowString(HWND window) wstring
    }

    Controller "1" *-- "1" Copier : model
    Controller "1" *-- "1" Window : view
    Window ..> Controller : passes user actions
    Window "1" *-- "0..1" Decision : composed of
    Decision ..> Controller : calls rename()
    Decision ..> RenameRequest : shows
    Controller ..> RenameRequest : creates
    Copier "1" *-- "*" CopyEntry : entries_
    Copier ..> RenameResult : returns
    Copier ..> DirectoryResult : returns
    Decision ..> RenameResult : reads
    Window ..> DirectoryResult : reads
    Window ..> Common : uses
    Decision ..> Common : uses
    Controller ..> Common : uses
    Copier ..> Common : uses

    note for Decision "Appears only when Copier::check() returns false (sizeApproved_ == false)"
```

## Notes

The code follows the Model-View-Controller pattern:

| Layer | Classes | Job |
|---|---|---|
| Model | `Copier`, `CopyEntry`, `RenameResult`, `DirectoryResult` | File system rules and data: directory checks, scanning, path limits, renames and copies. It does not know about windows. |
| View | `Window`, `Decision`, `RenameRequest` | Controls, user-visible texts and messages. They pass every user action to the `Controller` and never call the `Copier`. |
| Controller | `Controller` | Reads the input from the `Window`, calls the `Copier`, runs the copy thread, handles cancellation and tells the views what to show. |

- **Controller** owns the `Copier` and the `Window`. `main.cpp` creates only the `Controller`.
- **Window** creates one `Decision` only while a name is too long (`unique_ptr`, 0..1).
- **Decision** receives a `RenameRequest` with the data to show, and calls `Controller::rename()` when the user presses *Accept*, then calls `selfDestroy()`.
- **Copier** stores the scanned files and directories as `CopyEntry` items, in pre-order (a directory always comes before its content).
- `Copier::checkDirectories()` rejects empty directories, a missing source, and a destination equal to or inside the source before the copy starts.
- A failed or cancelled scan discards partial entries and stops the operation before files are copied. Existing destination paths must be directories, and existing files are never overwritten.
- `Controller` shares an atomic cancellation flag with `Copier`. Scanning checks it between entries, and `CopyFileExW` checks it through its progress callback.
- **Common** is not a class: it groups the control IDs, the messages between the copy thread and the main window, and the UI helper functions (`include/Common.hpp`).
- The copy runs on a separate thread (`Controller::runCopy`). It sends the `WM_APP_*` messages to the `Window`, which passes them to the `Controller` on the main thread. It asks for a new name with `SendMessage(WM_APP_RENAME_REQUEST)`, so it waits while the `Decision` window is open.
