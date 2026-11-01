#ifndef FULLCOPY_DECISION_HPP
#define FULLCOPY_DECISION_HPP

#include <windows.h>

#include <cstddef>
#include <string>

namespace fullcopy {
    class Controller;

    // Data of the entry whose name is too long, prepared by the Controller for the Decision window.
    struct RenameRequest {
        std::size_t entryIndex;
        bool isDirectory;
        std::wstring currentName;
        std::size_t pathLength;    // Length of the full destination path.
        std::size_t maxNameLength; // Longest name that fits.
    };

    // Window that appears only when the Copier does not approve the size of a path.
    // It asks the user for a new name and passes it to Controller::rename().
    class Decision {
    public:
        Decision(HINSTANCE instance, Controller &controller, const RenameRequest &request);
        ~Decision();

        Decision(const Decision&) = delete;
        Decision &operator=(const Decision&) = delete;

        // Shows the window as modal over the parent and waits until it is closed.
        // Returns true if the entry was renamed, false if the user closed the window.
        bool show(HWND parent);

        // Closes and destroys the window.
        void selfDestroy();

    private:
        static const wchar_t *CLASS_NAME;

        static void registerClass(HINSTANCE instance);
        static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void createControls();
        void onAccept();

        HINSTANCE instance_;
        Controller &controller_;
        RenameRequest request_;
        HWND parent_;
        HWND window_;
        HWND textBox_;
        HWND acceptButton_;
        bool renamed_;
        bool running_;
    };
} // namespace fullcopy

#endif // FULLCOPY_DECISION_HPP
