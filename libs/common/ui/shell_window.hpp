#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "colors.hpp"
#include "widgets.hpp"
#include "window.hpp"

namespace ogg::ui {

enum class ShellUserAction {
    None,
    Button0,
    Button1,
    Close,
};

constexpr UINT kMsgSetStatus = WM_APP + 10;
constexpr UINT kMsgSetProgress = WM_APP + 11;
constexpr UINT kMsgShowButtons = WM_APP + 12;
constexpr UINT kMsgSetEllipsis = WM_APP + 13;

class ShellWindow {
public:
    ShellWindow() = default;
    ~ShellWindow();

    bool create(
        const wchar_t* window_class,
        const wchar_t* title,
        const WindowSizePolicy& size_policy = client_window_policy(),
        ShellTheme theme = ShellTheme::Dark,
        bool defer_fade = false
    );

    void fade_in_now();
    void set_layered_alpha(BYTE alpha);
    void show();

    HWND hwnd() const { return hwnd_; }

    void pump_messages();
    ShellUserAction wait_for_action();

    void set_status(const std::wstring& text);
    void set_status_utf8(const std::string& text);
    void set_progress(int percent);
    void set_ellipsis_dots(int count);
    void set_show_buttons(bool show);

    void post_status(const std::wstring& text);
    void post_status_utf8(const std::string& text);
    void post_progress(int percent);
    void post_ellipsis_dots(int count);
    void post_show_buttons(bool show);

    void close();

    ShellView& view() { return view_; }
    const ShellView& view() const { return view_; }

    void set_action_buttons(const ShellButton* buttons, int count);
    void ensure_client_chrome_overlay();
    void set_status_font(const wchar_t* family, float size_pt);

private:
    static bool register_chrome_overlay_class();
    static LRESULT CALLBACK chrome_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void layout_chrome_overlay();
    void paint_client_chrome(HDC hdc, const RECT& rc);
    void update_chrome_hover(POINT pt);
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void invalidate();
    void on_paint();
    ShellUserAction user_action_ = ShellUserAction::None;

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    wchar_t window_class_[64]{};
    RenderContext render_;
    ShellView view_{};
    ShellTheme theme_ = ShellTheme::Dark;
    bool defer_fade_ = false;
    HWND chrome_overlay_ = nullptr;
    bool hover_chrome_close_ = false;
    bool hover_chrome_minimize_ = false;
};

} // namespace ogg::ui

#endif
