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

    // The Window gets a reference to this Controller, to pass it the user actions.
    Controller::Controller(HINSTANCE instance)
        : window_(instance, *this),
        workerThread_(nullptr),
        cancelRequested_(false),
        closePending_(false) {
    }

    // If the program ends during a copy, cancel it and wait for the thread before the Copier
    // is destroyed.
    Controller::~Controller() {
        if (workerThread_ != nullptr) {
            cancelRequested_.store(true);
            finishWorker();
        }
    }

    // Creates and shows the main window.
    bool Controller::create(int showCommand) {
        return window_.create(showCommand);
    }

    // Runs the message loop of the main window until it is closed.
    int Controller::run() {
        return window_.run();
    }

    // Start button: reads the directories, checks them with the Copier and starts the copy thread.
    void Controller::onStart() {
        // Only one copy at a time.
        if (workerThread_ != nullptr) {
            return;
        }

        // Give the typed directories to the model and let it check them.
        copier_.setDirectories(trim(window_.getSourceDirectory()), trim(window_.getDestinationDirectory()));

        const DirectoryResult result = copier_.checkDirectories();
        if (result != DirectoryResult::Ok) {
            window_.showDirectoryError(result);
            return;
        }

        // Establish the fallback before launching a worker. A failed PostMessage
        // must not leave the controls disabled or prevent a pending close.
        if (SetTimer(window_.getHandle(), WORKER_POLL_TIMER, 100, nullptr) == 0) {
            window_.showStartFailed();
            return;
        }
        workerResult_ = {};

        // Clear the state of a previous copy and update the window.
        cancelRequested_.store(false);
        closePending_ = false;
        window_.showCopyStarted();

        // The copy runs on its own thread, so the window keeps responding.
        workerThread_ = CreateThread(nullptr, 0, copyThread, this, 0, nullptr);
        if (workerThread_ == nullptr) {
            KillTimer(window_.getHandle(), WORKER_POLL_TIMER);
            window_.showStartFailed();
        }
    }

    // X button or Accept button. During a copy, it only asks the thread to stop: the window is
    // closed later, in finishCopy(), when the thread has ended.
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

    // Shows the progress sent by the copy thread.
    void Controller::onProgress(int percentage) {
        window_.updateProgressBar(percentage);
    }

    // The copy thread asks for a new name for an entry whose path is too long. It runs on the
    // main thread while the copy thread waits. Returns true if the user renamed the entry.
    bool Controller::onRenameRequest(std::size_t entryIndex) {
        // The user is closing the program: do not open a new window.
        if (closePending_) {
            return false;
        }

        // Collect what the Decision window has to show.
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

    // Called by the notification and timer on the UI thread. Waiting for the
    // thread to be signalled publishes its result without racing its writes.
    void Controller::pollWorker() {
        if (workerThread_ == nullptr ||
            WaitForSingleObject(workerThread_, 0) != WAIT_OBJECT_0) {
            return;
        }
        const WorkerResult result = workerResult_;
        if (!finishCopy()) {
            return;
        }
        if (result.succeeded) {
            window_.showCopyFinished(result.failed, result.skipped);
        } else {
            window_.showCopyFailed();
        }
    }

    // Accept button of the Decision window: applies the new name in the model and returns the result.
    RenameResult Controller::rename(std::size_t entryIndex, const std::wstring &newName) {
        return copier_.rename(entryIndex, newName);
    }

    // Waits for the copy thread. If the user asked to close the window during the copy, closes it
    // now and returns false.
    bool Controller::finishCopy() {
        finishWorker();
        if (closePending_) {
            window_.close();
            return false;
        }
        return true;
    }

    // Waits until the copy thread ends and releases its handle.
    void Controller::finishWorker() {
        if (workerThread_ != nullptr) {
            WaitForSingleObject(workerThread_, INFINITE);
            KillTimer(window_.getHandle(), WORKER_POLL_TIMER);
            CloseHandle(workerThread_);
            workerThread_ = nullptr;
        }
    }

    // Entry point of the copy thread. CreateThread passes this Controller as the parameter.
    // No exception can leave a thread, so any of them is reported as a failed copy.
    DWORD WINAPI Controller::copyThread(LPVOID parameter) {
        auto *self = static_cast<Controller*>(parameter);
        try {
            self->workerResult_ = self->runCopy();
        } catch (...) {
            self->workerResult_ = {};
        }
        // The timer also observes thread termination if this notification fails
        // or reaches the UI before the thread has actually returned.
        PostMessageW(self->window_.getHandle(), WM_APP_WORKER_DONE, 0, 0);
        return self->workerResult_.succeeded ? 0 : 1;
    }

    // Body of the copy thread: scans the source, then checks and copies every entry in order.
    // It talks to the window only with messages, because controls belong to the main thread.
    Controller::WorkerResult Controller::runCopy() {
        // Every message goes to the main window, which passes it to this Controller
        // on the main thread.
        const HWND window = window_.getHandle();

        // 1. Scan the source directory.
        if (!copier_.load(&cancelRequested_)) {
            return {};
        }

        // 2. Copy every entry, counting the ones that fail and the ones that are skipped.
        const std::size_t count = copier_.getEntryCount();
        std::size_t failed = 0;
        std::size_t skipped = 0;
        int lastPercentage = -1;

        for (std::size_t i = 0; i < count && !cancelRequested_.load(); ++i) {
            // Show which entry is being copied.
            PostMessageW(window, WM_APP_CURRENT_ENTRY, static_cast<WPARAM>(i), 0);

            // Ask for a new name until the path fits. SendMessage blocks this
            // thread until the user answers in the Decision window.
            while (!copier_.check(i)) {
                // Skip the entry if no name can fit, if the user cancels, or if the user closes
                // the Decision window.
                if (copier_.getMaxNameLength(i) == 0 || cancelRequested_.load() || SendMessageW(window, WM_APP_RENAME_REQUEST, static_cast<WPARAM>(i), 0) == FALSE) {
                    copier_.skip(i);
                    ++skipped;
                    break;
                }
            }

            // Stop here if the user cancelled.
            if (cancelRequested_.load()) {
                break;
            }

            // Copy the entry. A skipped entry does nothing and counts as done.
            if (!copier_.copy(i, &cancelRequested_)) {
                ++failed;
            }

            // Send the progress only when the percentage changes, so the window is not flooded.
            const int percentage = static_cast<int>((i + 1) * 100 / count);
            if (percentage != lastPercentage) {
                PostMessageW(window, WM_APP_PROGRESS, static_cast<WPARAM>(percentage), 0);
                lastPercentage = percentage;
            }
        }

        // 3. An empty source has no entries: show 100% directly.
        if (count == 0) {
            PostMessageW(window, WM_APP_PROGRESS, 100, 0);
        }

        // Publish the result when the thread ends, including cancellation.
        return {true, failed, skipped};
    }

} // namespace fullcopy
