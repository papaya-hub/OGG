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
#include "xml_ui.hpp"
#include "client_shell.hpp"

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
    bool ensure_client_login_panel(const char* xml);
    void ensure_client_chrome_overlay();
    void layout_client_shell();
    void ensure_client_version_overlay(const std::wstring& version_text);
    void bring_client_overlays_to_front();
    void set_status_font(const wchar_t* family, float size_pt);

private:
    static bool register_hero_overlay_class();
    static bool register_version_overlay_class();
    static LRESULT CALLBACK hero_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK version_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static void client_chrome_close(void* user_data);
    static void client_chrome_minimize(void* user_data);
    void layout_hero_overlay();
    void layout_login_panel();
    void layout_version_overlay();
    void paint_hero_panel(HDC hdc, const RECT& rc);
    void paint_client_version(HDC hdc, const RECT& rc);
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
    HWND hero_overlay_ = nullptr;
    client_shell::ChromeOverlay client_chrome_{};
    XmlUiHost login_ui_{};
    HWND version_overlay_ = nullptr;
    std::wstring version_overlay_text_;
};

} // namespace ogg::ui

#endif
