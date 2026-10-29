# FullCopy - Class Diagram

Class diagram of the current code (`include/` and `src/`). Everything lives in the `fullcopy` namespace.

```mermaid
classDiagram
    direction TB

    class Window {
        -HINSTANCE instance_
        -HWND window_
        -int loadPercentage_
        -HWND sourceDirectory_
        -HWND destinationDirectory_
        -HWND acceptButton_
        -HWND browseButton_
        -HWND browseDestinationButton_
        -HWND startButton_
        -HWND progressBar_
        -HWND statusLabel_
        -Copier copier_
        -unique_ptr~Decision~ decision_
        -HANDLE workerThread_
        -volatile LONG cancelRequested_
        -bool closePending_
        +Window(HINSTANCE instance)
        +~Window()
        +create(int showCommand) bool
        +run() int
        +updateProgressBar(int percentage) void
        -windowProcedure(HWND, UINT, WPARAM, LPARAM)$ LRESULT
        -handleMessage(UINT, WPARAM, LPARAM) LRESULT
        -createControls() void
        -onBrowse(HWND targetEdit) void
        -onStart() void
        -onAccept() void
        -onClose() void
        -onRenameRequest(size_t entryIndex) LRESULT
        -onCopyFinished(size_t failed, size_t skipped) void
        -onCopyFailed() void
        -finishWorker() void
        -setControlsEnabled(bool enabled) void
        -setStatus(wstring text) void
        -showError(wstring text) void
        -copyThread(LPVOID parameter)$ DWORD
        -runCopy() void
    }

    class Decision {
        -HINSTANCE instance_
        -Copier& copier_
        -size_t entryIndex_
        -HWND parent_
        -HWND window_
        -HWND textBox_
        -HWND acceptButton_
        -bool renamed_
        -bool running_
        +Decision(HINSTANCE instance, Copier& copier, size_t entryIndex)
        +~Decision()
        +show(HWND parent) bool
        +selfDestroy() void
        -registerClass(HINSTANCE instance)$ void
        -windowProcedure(HWND, UINT, WPARAM, LPARAM)$ LRESULT
        -handleMessage(UINT, WPARAM, LPARAM) LRESULT
        -createControls() void
        -onAccept() void
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
        +load() bool
        +check(size_t index) bool
        +copy(size_t index) bool
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
        -scanDirectory(wstring relativePath) void
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
    }

    class Common {
        <<utility>>
        +ControlId enum
        +WM_APP_PROGRESS$ UINT
        +WM_APP_RENAME_REQUEST$ UINT
        +WM_APP_COPY_FINISHED$ UINT
        +WM_APP_COPY_FAILED$ UINT
        +getUiFont() HFONT
        +applyUiFont(HWND window) void
        +getWindowString(HWND window) wstring
    }

    Window "1" *-- "1" Copier : composed of
    Window "1" *-- "0..1" Decision : composed of
    Decision ..> Copier : uses rename()
    Copier "1" *-- "*" CopyEntry : entries_
    Copier ..> RenameResult : returns
    Decision ..> RenameResult : reads
    Window ..> Common : uses
    Decision ..> Common : uses

    note for Decision "Appears only when Copier::check() returns false (sizeApproved_ == false)"
```

## Notes

- **Window** owns one `Copier` and creates one `Decision` only while a name is too long (`unique_ptr`, 0..1).
- **Decision** keeps a reference to the `Copier` and calls `rename()` when the user presses *Accept*, then calls `selfDestroy()`.
- **Copier** stores the scanned files and directories as `CopyEntry` items, in pre-order (a directory always comes before its content).
- **Common** is not a class: it groups the control IDs, the messages between the copy thread and the main window, and the UI helper functions (`include/Common.hpp`).
- The copy runs on a separate thread (`Window::runCopy`). It asks for a new name with `SendMessage(WM_APP_RENAME_REQUEST)`, so it waits while the `Decision` window is open.
