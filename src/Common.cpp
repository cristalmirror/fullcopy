#include "Common.hpp"

namespace fullcopy {

    namespace {

        BOOL CALLBACK setFontCallback(HWND child, LPARAM lParam) {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
            return TRUE;
        }

    } // namespace

    HFONT getUiFont() {
        struct FontOwner
        {
            HFONT handle = nullptr;
            bool owned = false;

            FontOwner() {
                NONCLIENTMETRICSW metrics = {};
                metrics.cbSize = sizeof(metrics);

                if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
                    handle = CreateFontIndirectW(&metrics.lfMessageFont);
                    owned = handle != nullptr;
                }

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
            FontOwner& operator=(const FontOwner&) = delete;
        };

        static FontOwner font;
        return font.handle;
    }

    void applyUiFont(HWND window) {
        HFONT font = getUiFont();
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        EnumChildWindows(window, setFontCallback, reinterpret_cast<LPARAM>(font));
    }

    std::wstring getWindowString(HWND window) {
        const int length = GetWindowTextLengthW(window);
        if (length <= 0) {
            return std::wstring();
        }

        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(window, &text[0], length + 1);
        text.resize(static_cast<std::size_t>(copied));
        return text;
    }

} // namespace fullcopy
