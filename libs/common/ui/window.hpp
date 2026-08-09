#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ogg::ui {

constexpr float kCornerRadius = 10.f;
constexpr int kFadeInMs = 750;

struct WindowSizePolicy {
    float width_ratio = 0.6f;
    float height_ratio = 0.6f;
    float margin_left_ratio = 0.2f;
    float margin_top_ratio = 0.2f;
};

inline WindowSizePolicy client_window_policy() {
    WindowSizePolicy policy;
    policy.width_ratio = 0.6f;
    policy.height_ratio = 0.6f;
    policy.margin_left_ratio = 0.2f;
    policy.margin_top_ratio = 0.2f;
    return policy;
}

inline WindowSizePolicy launcher_window_policy() {
    WindowSizePolicy policy;
    policy.width_ratio = 0.4f;
    policy.height_ratio = 0.2f;
    policy.margin_left_ratio = 0.3f;
    policy.margin_top_ratio = 0.4f;
    return policy;
}

inline WindowSizePolicy admin_window_policy() {
    WindowSizePolicy policy;
    policy.width_ratio = 1.f;
    policy.height_ratio = 1.f;
    policy.margin_left_ratio = 0.f;
    policy.margin_top_ratio = 0.f;
    return policy;
}

struct WindowPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

RECT monitor_rect_at_cursor();
RECT monitor_work_area_at_cursor();
WindowPlacement placement_on_monitor(RECT monitor, const WindowSizePolicy& policy = client_window_policy());
WindowPlacement placement_on_work_area(RECT work_area, const WindowSizePolicy& policy = client_window_policy());
void fade_in(HWND hwnd, int duration_ms = kFadeInMs);
void apply_rounded(HWND hwnd, int width, int height, float corner_radius = kCornerRadius);

} // namespace ogg::ui

#endif
