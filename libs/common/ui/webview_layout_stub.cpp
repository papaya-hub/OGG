#if defined(_WIN32)

#include "webview_host.hpp"

namespace ogg::ui {

void set_embedded_webview_bounds(HWND parent, int x, int y, int width, int height) {
    (void)parent;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void layout_embedded_webview(HWND parent) {
    (void)parent;
}

} // namespace ogg::ui

#endif
