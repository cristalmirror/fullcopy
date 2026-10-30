#ifndef FULLCOPY_WINDOW_HPP
#define FULLCOPY_WINDOW_HPP

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

#include "Copier.hpp"
#include "Decision.hpp"

namespace fullcopy
{

// Main window. It is composed of one Copier and, only while a name is too long, one Decision.
class Window
{
public:
    explicit Window(HINSTANCE instance);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(int showCommand);

    // Runs the message loop until the window is closed. Returns the exit code.
    int run();

    void updateProgressBar(int percentage);

private:
    static const wchar_t* CLASS_NAME;

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createControls();
    void onBrowse(HWND targetEdit);
    void onStart();
    void onAccept();
    void onClose();
    LRESULT onRenameRequest(std::size_t entryIndex);
    void onCopyFinished(std::size_t failed, std::size_t skipped);
    void onCopyFailed();
    void finishWorker();
    void setControlsEnabled(bool enabled);
    void setStatus(const std::wstring& text);
    void showError(const std::wstring& text);

    // Copy thread: walks every entry, asks for a new name when needed and copies it.
    static DWORD WINAPI copyThread(LPVOID parameter);
    void runCopy();

    HINSTANCE instance_;
    HWND window_;

    int loadPercentage_;
    HWND sourceDirectory_;
    HWND destinationDirectory_;
    HWND acceptButton_;
    HWND browseButton_;            // Browses the source directory.
    HWND browseDestinationButton_; // Browses the destination directory.
    HWND startButton_;
    HWND progressBar_;
    HWND statusLabel_;

    Copier copier_;
    std::unique_ptr<Decision> decision_;

    HANDLE workerThread_;
    std::atomic_bool cancelRequested_;
    bool closePending_;
};

} // namespace fullcopy

#endif // FULLCOPY_WINDOW_HPP
