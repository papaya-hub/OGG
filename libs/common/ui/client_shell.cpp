#if defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "client_shell.hpp"

#include <cstring>
#include <cstdint>
#include <windowsx.h>

namespace ogg::ui::client_shell {

namespace {

constexpr wchar_t kChromeOverlayClass[] = L"OGG.ClientShell.ChromeOverlay";

ChromeOverlay* chrome_from_hwnd(HWND hwnd) {
    return reinterpret_cast<ChromeOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int chrome_height_px() {
    return static_cast<int>(LayoutSpec::kDragBandHeight);
}

void fix_bitmap_alpha(void* bits, int pixel_count) {
    auto* px = reinterpret_cast<uint32_t*>(bits);
    for (int i = 0; i < pixel_count; ++i) {
        const uint32_t b = px[i] & 0xFF;
        const uint32_t g = (px[i] >> 8) & 0xFF;
        const uint32_t r = (px[i] >> 16) & 0xFF;
        if (r > 8 || g > 8 || b > 8) {
            px[i] |= 0xFF000000;
        }
    }
}

void draw_chrome_glyph(HDC hdc, const wchar_t* text, RECT* rc, COLORREF fg) {
    SetTextColor(hdc, RGB(0, 0, 0));
    RECT shadow = *rc;
    OffsetRect(&shadow, 1, 1);
    DrawTextW(hdc, text, 1, &shadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, fg);
    DrawTextW(hdc, text, 1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

bool allows_window_drag(POINT pt_client, float shell_width, DragOptions options) {
    const float chrome_left = shell_width - (LayoutSpec::kChromeButtonWidth * 2.f);
    const float chrome_bottom = LayoutSpec::kChromeHeight + LayoutSpec::kChromeBtnYOffset;
    if (static_cast<float>(pt_client.y) < chrome_bottom &&
        static_cast<float>(pt_client.x) >= chrome_left) {
        return false;
    }
    if (!options.full_window &&
        static_cast<float>(pt_client.y) >= LayoutSpec::kDragBandHeight) {
        return false;
    }
    return true;
}

bool ChromeOverlay::ensure(HWND parent, void* user_data, ActionHandler on_close, ActionHandler on_minimize) {
    if (!parent) return false;
    if (hwnd_) return true;

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
        WS_EX_LAYERED,
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
    if (hwnd_) {
        present_layered();
    }
    return hwnd_ != nullptr;
}

void ChromeOverlay::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    parent_ = nullptr;
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
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChromeOverlay::paint(HDC hdc, const RECT& rc) {
    const float chrome_width = static_cast<float>(rc.right - rc.left);
    const D2D1_RECT_F min_rect = client_chrome_minimize_rect(chrome_width);
    const D2D1_RECT_F close_rect = client_chrome_close_rect(chrome_width);

    static HFONT chrome_font = CreateFontW(
        -18,
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
        ANTIALIASED_QUALITY,
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
    const COLORREF glyph_idle = RGB(255, 255, 255);
    const COLORREF glyph_on_grey = RGB(72, 72, 72);
    const COLORREF light = RGB(250, 250, 250);

    SetBkMode(hdc, TRANSPARENT);

    if (hover_minimize_) {
        HBRUSH brush = CreateSolidBrush(grey_hover);
        FillRect(hdc, &min_rc, brush);
        DeleteObject(brush);
        draw_chrome_glyph(hdc, L"\u2013", &min_rc, glyph_on_grey);
    } else {
        draw_chrome_glyph(hdc, L"\u2013", &min_rc, glyph_idle);
    }

    if (hover_close_) {
        HBRUSH brush = CreateSolidBrush(danger);
        FillRect(hdc, &close_rc, brush);
        DeleteObject(brush);
        draw_chrome_glyph(hdc, L"\u00D7", &close_rc, light);
    } else {
        draw_chrome_glyph(hdc, L"\u00D7", &close_rc, glyph_idle);
    }
    SelectObject(hdc, old_font);
}

void ChromeOverlay::present_layered() {
    if (!hwnd_) return;

    RECT client_rc{};
    GetClientRect(hwnd_, &client_rc);
    const int w = client_rc.right - client_rc.left;
    const int h = client_rc.bottom - client_rc.top;
    if (w <= 0 || h <= 0) return;

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        ReleaseDC(nullptr, screen_dc);
        if (dib) DeleteObject(dib);
        return;
    }
    std::memset(bits, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

    HDC mem_dc = CreateCompatibleDC(screen_dc);
    if (!mem_dc) {
        DeleteObject(dib);
        ReleaseDC(nullptr, screen_dc);
        return;
    }

    HGDIOBJ old_bmp = SelectObject(mem_dc, dib);
    paint(mem_dc, client_rc);
    fix_bitmap_alpha(bits, w * h);

    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    POINT pt_dst{ wr.left, wr.top };
    SIZE size{ w, h };
    POINT pt_src{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd_, screen_dc, &pt_dst, &size, mem_dc, &pt_src, 0, &blend, ULW_ALPHA);

    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);
    DeleteObject(dib);
    ReleaseDC(nullptr, screen_dc);
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
        BeginPaint(hwnd, &ps);
        chrome->present_layered();
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
