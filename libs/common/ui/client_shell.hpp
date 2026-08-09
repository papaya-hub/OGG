#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "colors.hpp"
#include "widgets.hpp"

namespace ogg::ui::client_shell {

// Reusable OGG client window frame:
// - Left login panel (XML UI) at fixed width
// - Right hero/content panel
// - Transparent top-right minimize/close chrome (glyph offset -4px)
// - Invisible drag band (40px) or full-window drag when client_drag_full_window is set
struct LayoutSpec {
    static constexpr float kLoginPanelWidth = kClientLoginPanelWidth;
    static constexpr float kDragBandHeight = kClientDragBandHeight;
    static constexpr float kChromeHeight = kClientChromeHeight;
    static constexpr float kChromeBtnYOffset = kClientChromeBtnYOffset;
    static constexpr float kChromeButtonWidth = kTitleCloseBtnW;
};

struct DragOptions {
    bool full_window = false;
};

struct PanelLayout {
    int login_x = 0;
    int login_y = 0;
    int login_w = 0;
    int login_h = 0;
    int hero_x = 0;
    int hero_y = 0;
    int hero_w = 0;
    int hero_h = 0;
    int chrome_x = 0;
    int chrome_y = 0;
    int chrome_w = 0;
    int chrome_h = 0;
};

PanelLayout measure_panels(int shell_width, int shell_height);

// True when the shell should return HTCAPTION (excludes chrome buttons; band-limited unless full_window).
bool allows_window_drag(POINT pt_client, float shell_width, DragOptions options = {});

class ChromeOverlay {
public:
    using ActionHandler = void (*)(void* user_data);

    ChromeOverlay() = default;

    bool ensure(HWND parent, void* user_data, ActionHandler on_close, ActionHandler on_minimize);
    void destroy();
    void layout(int shell_width);
    void set_theme(ShellTheme theme) { theme_ = theme; }

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void paint(HDC hdc, const RECT& rc);
    void present_layered();
    void update_hover(POINT pt);

    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    void* user_data_ = nullptr;
    ActionHandler on_close_ = nullptr;
    ActionHandler on_minimize_ = nullptr;
    ShellTheme theme_ = ShellTheme::Light;
    bool hover_minimize_ = false;
    bool hover_close_ = false;
    bool mouse_tracking_ = false;
};

} // namespace ogg::ui::client_shell

#endif
