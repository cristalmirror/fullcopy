#ifndef FULLCOPY_COMMON_HPP
#define FULLCOPY_COMMON_HPP

#include <windows.h>

#include <string>

namespace fullcopy
{

// Control identifiers.
enum ControlId
{
    ID_SOURCE_EDIT = 101,
    ID_DESTINATION_EDIT,
    ID_BROWSE_SOURCE_BUTTON,
    ID_BROWSE_DESTINATION_BUTTON,
    ID_START_BUTTON,
    ID_ACCEPT_BUTTON,
    ID_PROGRESS_BAR,
    ID_STATUS_LABEL,
    ID_DECISION_LABEL,
    ID_DECISION_TEXT_BOX
};

// Messages sent by the copy thread to the main window.
const UINT WM_APP_PROGRESS       = WM_APP + 1; // wParam: percentage (0-100).
const UINT WM_APP_RENAME_REQUEST = WM_APP + 2; // wParam: entry index. Returns TRUE if renamed.
const UINT WM_APP_COPY_FINISHED  = WM_APP + 3; // wParam: failed entries, lParam: skipped entries.
const UINT WM_APP_COPY_FAILED    = WM_APP + 4; // The source directory could not be read.

// Returns the system font used for message boxes (shared by every window).
HFONT getUiFont();

// Applies the UI font to a window and all its children.
void applyUiFont(HWND window);

// Returns the text of a window (edit box, label, ...).
std::wstring getWindowString(HWND window);

} // namespace fullcopy

#endif // FULLCOPY_COMMON_HPP
