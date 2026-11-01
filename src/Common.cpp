#include "Common.hpp"

namespace fullcopy {

    namespace {

        // Called by EnumChildWindows for every child window: gives it the font passed in lParam.
        BOOL CALLBACK setFontCallback(HWND child, LPARAM lParam) {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
            return TRUE;
        }

    } // namespace

    // Returns the font of the message boxes. It is created on the first call and deleted when
    // the program ends.
    HFONT getUiFont() {
        // Owns the font: creates it in the constructor and deletes it in the destructor.
        struct FontOwner
        {
            HFONT handle = nullptr;
            bool owned = false;

            FontOwner() {
                NONCLIENTMETRICSW metrics = {};
                metrics.cbSize = sizeof(metrics);

                // Ask Windows for the message box font and create a copy of it.
                if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
                    handle = CreateFontIndirectW(&metrics.lfMessageFont);
                    owned = handle != nullptr;
                }

                // If that failed, use the stock GUI font. It belongs to Windows, so it is never deleted.
                if (handle == nullptr) {
                    handle = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                }
            }

            ~FontOwner() {
                if (owned) {
                    DeleteObject(handle);
                }
            }

            FontOwner(const FontOwner&) = delete;
            FontOwner &operator=(const FontOwner&) = delete;
        };

        // A static local object is created on the first call and destroyed when the program ends.
        static FontOwner font;
        return font.handle;
    }

    // Sets the UI font on a window and on all its child controls.
    void applyUiFont(HWND window) {
        HFONT font = getUiFont();
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        EnumChildWindows(window, setFontCallback, reinterpret_cast<LPARAM>(font));
    }

    // Returns the text of a window (edit box, label, ...), or an empty string if it has none.
    std::wstring getWindowString(HWND window) {
        const int length = GetWindowTextLengthW(window);
        if (length <= 0) {
            return std::wstring();
        }

        // Reserve room for the text and the final null character, then drop the unused part.
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(window, &text[0], length + 1);
        text.resize(static_cast<std::size_t>(copied));
        return text;
    }

    // Joins two path parts with a single backslash.
    std::wstring joinPath(const std::wstring &base, const std::wstring &part) {
        if (part.empty()) {
            return base;
        }
        if (!base.empty() && base.back() == L'\\') {
            return base + part;
        }
        return base + L'\\' + part;
    }

    // Compares two strings ignoring case, with the same rules Windows uses for file names.
    bool equalsIgnoreCase(const std::wstring &first, const std::wstring &second) {
        return CompareStringOrdinal(first.c_str(),
                                    static_cast<int>(first.size()),
                                    second.c_str(),
                                    static_cast<int>(second.size()),
                                    TRUE) == CSTR_EQUAL;
    }

} // namespace fullcopy
