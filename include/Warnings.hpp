#ifndef FULLCOPY_WARNINGS_HPP
#define FULLCOPY_WARNINGS_HPP

#include <windows.h>

#include <string>
#include <vector>

namespace fullcopy {
    // Selects the title and the text of the Warnings window.
    enum class WarningKind {
        InaccessibleReference, // Files or directories that could not be read, listed after the copy.
        Recursion              // A reference to a directory that contains it, found during the scan.
    };

    // Modal window that tells the user which files or directories are not copied and why.
    // It shows a fixed text, the list of paths and an Accept button.
    class Warnings {
    public:
        Warnings(HINSTANCE instance, WarningKind kind, const std::vector<std::wstring> &paths);
        ~Warnings();

        Warnings(const Warnings&) = delete;
        Warnings &operator=(const Warnings&) = delete;

        // Shows the window as modal over the parent and waits until the user accepts it.
        // If the window can not be created, shows the same text in a message box instead.
        void show(HWND parent);

    private:
        static const wchar_t *CLASS_NAME;

        static bool registerClass(HINSTANCE instance);
        static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        bool createControls();
        void showMessageBox() const;
        void selfDestroy();
        const wchar_t *getTitle() const;
        const wchar_t *getText() const;
        std::wstring getPathList() const;

        HINSTANCE instance_;
        WarningKind kind_;
        std::vector<std::wstring> paths_;
        HWND parent_;
        HWND window_;
        HWND acceptButton_;
        bool running_;
    };
} // namespace fullcopy

#endif // FULLCOPY_WARNINGS_HPP
