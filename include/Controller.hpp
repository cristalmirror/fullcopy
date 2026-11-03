#ifndef FULLCOPY_CONTROLLER_HPP
#define FULLCOPY_CONTROLLER_HPP

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "Copier.hpp"
#include "Window.hpp"

namespace fullcopy {
    // Controller: receives the user actions from the views (Window and Decision), runs the copy
    // with the Copier on a separate thread, and tells the Window what to show.
    class Controller {
    public:
        explicit Controller(HINSTANCE instance);
        ~Controller();

        Controller(const Controller&) = delete;
        Controller &operator=(const Controller&) = delete;

        // Creates and shows the main window.
        bool create(int showCommand);

        // Runs the application until the main window is closed. Returns the exit code.
        int run();

        // Actions from the Window (main thread).
        void onStart();
        void onClose();

        // Messages from the copy thread, delivered by the Window on the main thread.
        void onProgress(int percentage);
        bool onRenameRequest(std::size_t entryIndex);
        void onRecursionFound(const std::wstring &relativePath);
        void onCurrentEntry(std::size_t entryIndex);
        void pollWorker();

        // Action from the Decision window (main thread, while the copy thread waits).
        RenameResult rename(std::size_t entryIndex, const std::wstring &newName);

    private:
        // Copy thread: walks every entry, asks for a new name when needed and copies it.
        static DWORD WINAPI copyThread(LPVOID parameter);
        struct WorkerResult {
            bool succeeded = false;
            std::size_t failed = 0;
            std::size_t skipped = 0;
            std::size_t recursions = 0;             // Recursive references ignored during the scan.
            std::vector<std::wstring> inaccessible; // Full source paths that could not be read.
        };
        WorkerResult runCopy();

        // Waits for the copy thread to end. Returns false if the window was closed instead,
        // because the user asked to close it during the copy.
        bool finishCopy();
        void finishWorker();

        Copier copier_;
        Window window_;

        WorkerResult workerResult_;
        HANDLE workerThread_;
        std::atomic_bool cancelRequested_;
        bool closePending_;
    };
} // namespace fullcopy

#endif // FULLCOPY_CONTROLLER_HPP
