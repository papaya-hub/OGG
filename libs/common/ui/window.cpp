#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include "window.hpp"

#pragma comment(lib, "dwmapi.lib")

namespace ogg::ui {

RECT monitor_rect_at_cursor() {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return info.rcMonitor;
}

WindowPlacement placement_on_monitor(RECT monitor, const WindowSizePolicy& policy) {
    const int screen_w = monitor.right - monitor.left;
    const int screen_h = monitor.bottom - monitor.top;
    WindowPlacement place;
    place.width = static_cast<int>(screen_w * policy.width_ratio);
    place.height = static_cast<int>(screen_h * policy.height_ratio);
    place.x = monitor.left + static_cast<int>(screen_w * policy.margin_left_ratio);
    place.y = monitor.top + static_cast<int>(screen_h * policy.margin_top_ratio);
    return place;
}

void fade_in(HWND hwnd, int duration_ms) {
    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    const DWORD start = GetTickCount();
    for (;;) {
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= static_cast<DWORD>(duration_ms)) break;
        const BYTE alpha = static_cast<BYTE>((elapsed * 255) / static_cast<DWORD>(duration_ms));
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

void apply_rounded(HWND hwnd, int width, int height, float corner_radius) {
    constexpr int kDwmWindowCornerPreference = 2;
    int preference = kDwmWindowCornerPreference;
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    HRGN region = CreateRoundRectRgn(
        0,
        0,
        width + 1,
        height + 1,
        static_cast<int>(corner_radius * 2.f),
        static_cast<int>(corner_radius * 2.f)
    );
    SetWindowRgn(hwnd, region, TRUE);
}

} // namespace ogg::ui

#endif
