#pragma once

#include <d2d1.h>

#if defined(__has_include)
#if __has_include("client_theme.hpp")
#include "client_theme.hpp"
#define OGG_HAS_CLIENT_THEME 1
#endif
#endif

namespace ogg::ui {

enum class ShellTheme {
    Dark,
    Light,
};

// Progress bar fill and primary buttons share theme primary (from appsettings via sync_settings).
#if defined(OGG_HAS_CLIENT_THEME)
inline D2D1_COLOR_F primary_color() { return theme::primary_color(); }
inline D2D1_COLOR_F secondary_color() { return theme::secondary_color(); }
#else
inline D2D1_COLOR_F primary_color() {
    return D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f);
}

inline D2D1_COLOR_F secondary_color() {
    return D2D1::ColorF(0.55f, 0.65f, 0.76f, 1.f);
}
#endif

inline D2D1_COLOR_F danger_color() {
    return D2D1::ColorF(0.82f, 0.18f, 0.18f, 1.f);
}

inline D2D1_COLOR_F background_color(ShellTheme theme = ShellTheme::Dark) {
    return theme == ShellTheme::Light
        ? D2D1::ColorF(1.f, 1.f, 1.f, 1.f)
        : D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
}

inline D2D1_COLOR_F text_color(ShellTheme theme = ShellTheme::Dark) {
    return theme == ShellTheme::Light
        ? D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.f)
        : D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.f);
}

inline D2D1_COLOR_F progress_track_color(ShellTheme theme = ShellTheme::Dark) {
    return theme == ShellTheme::Light
        ? D2D1::ColorF(0.86f, 0.86f, 0.88f, 1.f)
        : D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.f);
}

inline D2D1_COLOR_F button_on_primary_text_color() {
    return D2D1::ColorF(0.08f, 0.10f, 0.14f, 1.f);
}

inline D2D1_COLOR_F button_on_secondary_text_color() {
    return D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.f);
}

inline D2D1_COLOR_F button_on_danger_text_color() {
    return D2D1::ColorF(0.98f, 0.98f, 0.98f, 1.f);
}

inline D2D1_COLOR_F chrome_muted_color() {
    return D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.f);
}

inline D2D1_COLOR_F chrome_hover_grey_color() {
    return D2D1::ColorF(0.82f, 0.82f, 0.82f, 1.f);
}

inline COLORREF background_colorref(ShellTheme theme = ShellTheme::Dark) {
    return theme == ShellTheme::Light ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

// Client title-bar chrome uses fixed app colors (not DWM accent) for consistent branding.
// Minimize hover optionally follows the system button-face color when available.
inline COLORREF chrome_title_bar_colorref(ShellTheme theme = ShellTheme::Dark) {
    return background_colorref(theme);
}

inline COLORREF chrome_close_hover_colorref() {
    return RGB(209, 46, 46);
}

inline COLORREF chrome_minimize_hover_colorref(ShellTheme theme = ShellTheme::Dark) {
    (void)theme;
    return RGB(210, 210, 210);
}

inline COLORREF chrome_button_muted_colorref(ShellTheme theme = ShellTheme::Dark) {
    return theme == ShellTheme::Light ? RGB(120, 120, 120) : RGB(180, 180, 180);
}

} // namespace ogg::ui
