#ifndef FULLCOPY_WINDOW_HPP
#define FULLCOPY_WINDOW_HPP

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Decision.hpp"
#include "Warnings.hpp"

namespace fullcopy {
    class Controller;
    enum class DirectoryResult;

    // Main window (view). It shows what the Controller asks for and passes every user action
    // to it. It is composed of one Decision only while a name is too long.
    class Window {
    public:
        Window(HINSTANCE instance, Controller &controller);

        Window(const Window&) = delete;
        Window &operator=(const Window&) = delete;

        bool create(int showCommand);

        // Runs the message loop until the window is closed. Returns the exit code.
        int run();

        // Destroys the window, which ends the message loop.
        void close();

        // Window that receives the messages of the copy thread.
        HWND getHandle() const;

        // Text written by the user in the directory boxes.
        std::wstring getSourceDirectory() const;
        std::wstring getDestinationDirectory() const;

        // Opens the Decision window and waits for it. Returns true if the entry was renamed.
        bool askNewName(const RenameRequest &request);

        // Opens the Warnings window with a list of paths and waits until the user accepts it.
        void showWarning(WarningKind kind, const std::vector<std::wstring> &paths);

        void updateProgressBar(int percentage);
        void setCurrentPath(const std::wstring &path);

        // States of the copy.
        void showDirectoryError(DirectoryResult result);
        void showCopyStarted();
        void showStartFailed();
        void showCancelling();
        void showCopyFinished(std::size_t failed, std::size_t skipped, std::size_t recursions,
                              const std::vector<std::wstring> &inaccessible);
        void showCopyFailed();

    private:
        static const wchar_t *CLASS_NAME;

        static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void createControls();
        void onBrowse(HWND targetEdit);
        void onAccept();
        void restoreControls();
        void setControlsEnabled(bool enabled);
        void setStatus(const std::wstring &text);
        void showError(const std::wstring &text);

        HINSTANCE instance_;
        Controller &controller_;
        HWND window_;

        int loadPercentage_;
        HWND sourceDirectory_;
        HWND destinationDirectory_;
        HWND acceptButton_;
        HWND browseButton_;            // Browses the source directory.
        HWND browseDestinationButton_; // Browses the destination directory.
        HWND startButton_;
        HWND progressBar_;
        HWND pathLabel_; // Path being copied, under the progress bar.
        HWND statusLabel_;

        std::unique_ptr<Decision> decision_;
    };
} // namespace fullcopy

#endif // FULLCOPY_WINDOW_HPP
