#if defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "client_shell.hpp"

#include <windowsx.h>

namespace ogg::ui::client_shell {

namespace {

constexpr wchar_t kChromeOverlayClass[] = L"OGG.ClientShell.ChromeOverlay";

ChromeOverlay* chrome_from_hwnd(HWND hwnd) {
    return reinterpret_cast<ChromeOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int chrome_height_px() {
    return static_cast<int>(LayoutSpec::kChromeHeight);
}

} // namespace

PanelLayout measure_panels(int shell_width, int shell_height) {
    PanelLayout layout{};
    if (shell_width <= 0 || shell_height <= 0) return layout;

    const int login_w = static_cast<int>(LayoutSpec::kLoginPanelWidth);
    layout.login_x = 0;
    layout.login_y = 0;
    layout.login_w = login_w < shell_width ? login_w : shell_width;
    layout.login_h = shell_height;

    layout.hero_x = layout.login_w;
    layout.hero_y = 0;
    layout.hero_w = shell_width - layout.login_w;
    layout.hero_h = shell_height;

    layout.chrome_w = static_cast<int>(LayoutSpec::kChromeButtonWidth * 2.f);
    layout.chrome_h = chrome_height_px();
    layout.chrome_x = shell_width - layout.chrome_w;
    layout.chrome_y = 0;
    return layout;
}

bool allows_window_drag(POINT pt_client, float shell_width) {
    const float chrome_left = shell_width - (LayoutSpec::kChromeButtonWidth * 2.f);
    const float chrome_bottom = LayoutSpec::kChromeHeight + LayoutSpec::kChromeBtnYOffset;
    if (static_cast<float>(pt_client.y) < chrome_bottom &&
        static_cast<float>(pt_client.x) >= chrome_left) {
        return false;
    }
    return true;
}

bool ChromeOverlay::ensure(HWND parent, void* user_data, ActionHandler on_close, ActionHandler on_minimize) {
    if (!parent) return false;
    destroy();

    parent_ = parent;
    user_data_ = user_data;
    on_close_ = on_close;
    on_minimize_ = on_minimize;

    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(GetModuleHandleW(nullptr), kChromeOverlayClass, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ChromeOverlay::window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kChromeOverlayClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kChromeOverlayClass,
        nullptr,
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        static_cast<int>(LayoutSpec::kChromeButtonWidth * 2.f),
        chrome_height_px(),
        parent_,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );
    return hwnd_ != nullptr;
}

void ChromeOverlay::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    parent_ = nullptr;
    repair_target_ = nullptr;
    user_data_ = nullptr;
    on_close_ = nullptr;
    on_minimize_ = nullptr;
    hover_minimize_ = false;
    hover_close_ = false;
    mouse_tracking_ = false;
}

void ChromeOverlay::layout(int shell_width) {
    if (!hwnd_ || shell_width <= 0) return;
    const PanelLayout panels = measure_panels(shell_width, chrome_height_px());
    SetWindowPos(
        hwnd_,
        HWND_TOP,
        panels.chrome_x,
        panels.chrome_y,
        panels.chrome_w,
        panels.chrome_h,
        SWP_NOACTIVATE
    );
}

void ChromeOverlay::invalidate_repair_region() {
    if (!repair_target_) return;
    RECT hero_rc{};
    GetClientRect(repair_target_, &hero_rc);
    const int chrome_h = static_cast<int>(LayoutSpec::kChromeHeight + LayoutSpec::kChromeBtnYOffset);
    RECT repair{
        hero_rc.right - static_cast<LONG>(LayoutSpec::kChromeButtonWidth * 2.f),
        0,
        hero_rc.right,
        chrome_h > 0 ? chrome_h : static_cast<LONG>(LayoutSpec::kChromeHeight),
    };
    InvalidateRect(repair_target_, &repair, FALSE);
}

void ChromeOverlay::paint(HDC hdc, const RECT& rc) {
    const float chrome_width = static_cast<float>(rc.right - rc.left);
    const D2D1_RECT_F min_rect = client_chrome_minimize_rect(chrome_width);
    const D2D1_RECT_F close_rect = client_chrome_close_rect(chrome_width);

    static HFONT chrome_font = CreateFontW(
        -28,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
    HFONT old_font = reinterpret_cast<HFONT>(SelectObject(hdc, chrome_font));

    RECT min_rc{
        static_cast<LONG>(min_rect.left),
        static_cast<LONG>(min_rect.top),
        static_cast<LONG>(min_rect.right),
        static_cast<LONG>(min_rect.bottom),
    };
    RECT close_rc{
        static_cast<LONG>(close_rect.left),
        static_cast<LONG>(close_rect.top),
        static_cast<LONG>(close_rect.right),
        static_cast<LONG>(close_rect.bottom),
    };

    const COLORREF danger = chrome_close_hover_colorref();
    COLORREF grey_hover = chrome_minimize_hover_colorref(theme_);
    const COLORREF sys_btnface = GetSysColor(COLOR_BTNFACE);
    if (sys_btnface != 0) grey_hover = sys_btnface;
    const COLORREF muted = chrome_button_muted_colorref(theme_);
    const COLORREF light = RGB(250, 250, 250);
    const COLORREF on_grey = RGB(72, 72, 72);

    SetBkMode(hdc, TRANSPARENT);

    if (hover_minimize_) {
        HBRUSH brush = CreateSolidBrush(grey_hover);
        FillRect(hdc, &min_rc, brush);
        DeleteObject(brush);
        SetTextColor(hdc, on_grey);
    } else {
        SetTextColor(hdc, muted);
    }
    DrawTextW(hdc, L"\u2013", 1, &min_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (hover_close_) {
        HBRUSH brush = CreateSolidBrush(danger);
        FillRect(hdc, &close_rc, brush);
        DeleteObject(brush);
        SetTextColor(hdc, light);
    } else {
        SetTextColor(hdc, muted);
    }
    DrawTextW(hdc, L"\u00D7", 1, &close_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old_font);
}

void ChromeOverlay::update_hover(POINT pt) {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float chrome_width = static_cast<float>(rc.right - rc.left);
    const D2D1_RECT_F min_rect = client_chrome_minimize_rect(chrome_width);
    const D2D1_RECT_F close_rect = client_chrome_close_rect(chrome_width);

    const bool hover_min = point_in_rect(pt, min_rect);
    const bool hover_close = point_in_rect(pt, close_rect);

    if (hover_min != hover_minimize_ || hover_close != hover_close_) {
        hover_minimize_ = hover_min;
        hover_close_ = hover_close;
        InvalidateRect(hwnd_, nullptr, FALSE);
        invalidate_repair_region();
    }
}

LRESULT CALLBACK ChromeOverlay::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ChromeOverlay* chrome = chrome_from_hwnd(hwnd);
    if (msg == WM_CREATE) {
        chrome = reinterpret_cast<ChromeOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(chrome));
    }
    if (!chrome) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt_screen{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt_screen);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float chrome_width = static_cast<float>(rc.right - rc.left);
        const D2D1_RECT_F min_rect = client_chrome_minimize_rect(chrome_width);
        const D2D1_RECT_F close_rect = client_chrome_close_rect(chrome_width);
        if (point_in_rect(pt_screen, min_rect) || point_in_rect(pt_screen, close_rect)) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rc{};
        GetClientRect(hwnd, &client_rc);
        chrome->paint(hdc, client_rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        chrome->update_hover(pt);
        if (!chrome->mouse_tracking_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            chrome->mouse_tracking_ = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        chrome->mouse_tracking_ = false;
        chrome->hover_minimize_ = false;
        chrome->hover_close_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        chrome->invalidate_repair_region();
        return 0;

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float chrome_width = static_cast<float>(rc.right - rc.left);
        const D2D1_RECT_F min_rect = client_chrome_minimize_rect(chrome_width);
        const D2D1_RECT_F close_rect = client_chrome_close_rect(chrome_width);
        if (point_in_rect(pt, close_rect)) {
            if (chrome->on_close_) chrome->on_close_(chrome->user_data_);
            return 0;
        }
        if (point_in_rect(pt, min_rect)) {
            if (chrome->on_minimize_) chrome->on_minimize_(chrome->user_data_);
            return 0;
        }
        return 0;
    }

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace ogg::ui::client_shell

#endif
