#include "Controller.hpp"

#include "Common.hpp"
#include "Decision.hpp"

namespace fullcopy {

    namespace {

        // Removes the spaces and quotes around a path typed or pasted by the user.
        std::wstring trim(const std::wstring &text) {
            const wchar_t *const SPACES = L" \t\r\n\"";
            const std::size_t first = text.find_first_not_of(SPACES);
            if (first == std::wstring::npos) {
                return std::wstring();
            }
            const std::size_t last = text.find_last_not_of(SPACES);
            return text.substr(first, last - first + 1);
        }

    } // namespace

    Controller::Controller(HINSTANCE instance)
        : window_(instance, *this),
        workerThread_(nullptr),
        cancelRequested_(false),
        closePending_(false) {
    }

    Controller::~Controller() {
        if (workerThread_ != nullptr) {
            cancelRequested_.store(true);
            finishWorker();
        }
    }

    bool Controller::create(int showCommand) {
        return window_.create(showCommand);
    }

    int Controller::run() {
        return window_.run();
    }

    void Controller::onStart() {
        if (workerThread_ != nullptr) {
            return;
        }

        copier_.setDirectories(trim(window_.getSourceDirectory()), trim(window_.getDestinationDirectory()));

        const DirectoryResult result = copier_.checkDirectories();
        if (result != DirectoryResult::Ok) {
            window_.showDirectoryError(result);
            return;
        }

        cancelRequested_.store(false);
        closePending_ = false;
        window_.showCopyStarted();

        workerThread_ = CreateThread(nullptr, 0, copyThread, this, 0, nullptr);
        if (workerThread_ == nullptr) {
            window_.showStartFailed();
        }
    }

    void Controller::onClose() {
        if (workerThread_ != nullptr) {
            // The window is closed when the copy thread confirms it has stopped.
            cancelRequested_.store(true);
            closePending_ = true;
            window_.showCancelling();
            return;
        }

        window_.close();
    }

    void Controller::onProgress(int percentage) {
        window_.updateProgressBar(percentage);
    }

    // Runs on the main thread while the copy thread waits for the answer.
    bool Controller::onRenameRequest(std::size_t entryIndex) {
        if (closePending_) {
            return false;
        }

        RenameRequest request;
        request.entryIndex = entryIndex;
        request.isDirectory = copier_.getEntry(entryIndex).isDirectory;
        request.currentName = copier_.getDestinationName(entryIndex);
        request.pathLength = copier_.getTempSize();
        request.maxNameLength = copier_.getMaxNameLength(entryIndex);

        return window_.askNewName(request);
    }

    // Shows the full source path of the entry the copy thread is working on.
    void Controller::onCurrentEntry(std::size_t entryIndex) {
        if (entryIndex >= copier_.getEntryCount()) {
            return;
        }

        window_.setCurrentPath(joinPath(copier_.getSourceRoot(), copier_.getEntry(entryIndex).sourcePath));
    }

    void Controller::onCopyFinished(std::size_t failed, std::size_t skipped) {
        if (!finishCopy()) {
            return;
        }
        window_.showCopyFinished(failed, skipped);
    }

    void Controller::onCopyFailed() {
        if (!finishCopy()) {
            return;
        }
        window_.showCopyFailed();
    }

    RenameResult Controller::rename(std::size_t entryIndex, const std::wstring &newName) {
        return copier_.rename(entryIndex, newName);
    }

    bool Controller::finishCopy() {
        finishWorker();
        if (closePending_) {
            window_.close();
            return false;
        }
        return true;
    }

    void Controller::finishWorker() {
        if (workerThread_ != nullptr) {
            WaitForSingleObject(workerThread_, INFINITE);
            CloseHandle(workerThread_);
            workerThread_ = nullptr;
        }
    }

    DWORD WINAPI Controller::copyThread(LPVOID parameter) {
        auto *self = static_cast<Controller*>(parameter);
        try {
            self->runCopy();
            return 0;
        } catch (...) {
            PostMessageW(self->window_.getHandle(), WM_APP_COPY_FAILED, 0, 0);
            return 1;
        }
    }

    void Controller::runCopy() {
        const HWND window = window_.getHandle();

        if (!copier_.load(&cancelRequested_)) {
            PostMessageW(window, WM_APP_COPY_FAILED, 0, 0);
            return;
        }

        const std::size_t count = copier_.getEntryCount();
        std::size_t failed = 0;
        std::size_t skipped = 0;
        int lastPercentage = -1;

        for (std::size_t i = 0; i < count && !cancelRequested_.load(); ++i) {
            PostMessageW(window, WM_APP_CURRENT_ENTRY, static_cast<WPARAM>(i), 0);

            // Ask for a new name until the path fits. SendMessage blocks this
            // thread until the user answers in the Decision window.
            while (!copier_.check(i)) {
                if (copier_.getMaxNameLength(i) == 0 || cancelRequested_.load() || SendMessageW(window, WM_APP_RENAME_REQUEST, static_cast<WPARAM>(i), 0) == FALSE) {
                    copier_.skip(i);
                    ++skipped;
                    break;
                }
            }

            if (cancelRequested_.load()) {
                break;
            }

            if (!copier_.copy(i, &cancelRequested_)) {
                ++failed;
            }

            const int percentage = static_cast<int>((i + 1) * 100 / count);
            if (percentage != lastPercentage) {
                PostMessageW(window, WM_APP_PROGRESS, static_cast<WPARAM>(percentage), 0);
                lastPercentage = percentage;
            }
        }

        if (count == 0) {
            PostMessageW(window, WM_APP_PROGRESS, 100, 0);
        }

        PostMessageW(window, WM_APP_COPY_FINISHED, static_cast<WPARAM>(failed), static_cast<LPARAM>(skipped));
    }

} // namespace fullcopy
