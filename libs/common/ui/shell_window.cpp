#if defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>

#include "shell_window.hpp"
#include "colors.hpp"
#include "window.hpp"
#include "http_client.hpp"
#include "webview_host.hpp"
#include "hero_image.hpp"

namespace ogg::ui {

ShellWindow::~ShellWindow() {
    close();
}

bool ShellWindow::create(
    const wchar_t* window_class,
    const wchar_t* title,
    const WindowSizePolicy& size_policy,
    ShellTheme theme,
    bool defer_fade
) {
    if (!render_.init()) return false;
    render_.set_theme(theme);
    theme_ = theme;
    defer_fade_ = defer_fade;

    const RECT monitor = monitor_rect_at_cursor();
    const WindowPlacement place = placement_on_monitor(monitor, size_policy);
    width_ = place.width;
    height_ = place.height;

    wcsncpy_s(window_class_, window_class, _TRUNCATE);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(instance, window_class_, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ShellWindow::window_proc;
        wc.hInstance = instance;
        wc.lpszClassName = window_class_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(background_colorref(theme));
        if (!RegisterClassExW(&wc)) return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        window_class_,
        title,
        WS_POPUP,
        place.x,
        place.y,
        width_,
        height_,
        nullptr,
        nullptr,
        instance,
        this
    );
    if (!hwnd_) return false;

    apply_rounded(hwnd_, width_, height_);
    if (defer_fade_) {
        set_layered_alpha(0);
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    } else {
        fade_in(hwnd_, kFadeInMs);
    }
    SetForegroundWindow(hwnd_);
    invalidate();
    return true;
}

void ShellWindow::fade_in_now() {
    if (hwnd_) fade_in(hwnd_, kFadeInMs);
}

void ShellWindow::set_layered_alpha(BYTE alpha) {
    if (!hwnd_) return;
    SetWindowLongW(hwnd_, GWL_EXSTYLE, GetWindowLongW(hwnd_, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
}

void ShellWindow::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

void ShellWindow::close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    render_.release_target();
}

void ShellWindow::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ShellWindow::set_status(const std::wstring& text) {
    view_.status_text = text;
    view_.status_ellipsis = 0;
    invalidate();
}

void ShellWindow::set_status_utf8(const std::string& text) {
    set_status(ogg::http_client::to_wide(text));
}

void ShellWindow::set_progress(int percent) {
    view_.progress = percent;
    invalidate();
}

void ShellWindow::set_ellipsis_dots(int count) {
    view_.status_ellipsis = count < 0 ? 0 : (count > 3 ? 3 : count);
    invalidate();
}

void ShellWindow::set_show_buttons(bool show) {
    view_.show_buttons = show;
    invalidate();
}

void ShellWindow::post_status(const std::wstring& text) {
    if (!hwnd_) return;
    auto* copy = new std::wstring(text);
    if (!PostMessageW(hwnd_, kMsgSetStatus, 0, reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

void ShellWindow::post_status_utf8(const std::string& text) {
    post_status(ogg::http_client::to_wide(text));
}

void ShellWindow::post_progress(int percent) {
    if (hwnd_) PostMessageW(hwnd_, kMsgSetProgress, static_cast<WPARAM>(percent), 0);
}

void ShellWindow::post_ellipsis_dots(int count) {
    if (hwnd_) PostMessageW(hwnd_, kMsgSetEllipsis, static_cast<WPARAM>(count), 0);
}

void ShellWindow::post_show_buttons(bool show) {
    if (hwnd_) PostMessageW(hwnd_, kMsgShowButtons, show ? 1 : 0, 0);
}

void ShellWindow::set_action_buttons(const ShellButton* buttons, int count) {
    view_.button_count = count > 2 ? 2 : count;
    for (int i = 0; i < view_.button_count; ++i) {
        view_.buttons[i] = buttons[i];
    }
}

void ShellWindow::set_status_font(const wchar_t* family, float size_pt) {
    if (render_.set_status_font(family, size_pt)) {
        invalidate();
    }
}

void ShellWindow::pump_messages() {
    if (!hwnd_) return;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

ShellUserAction ShellWindow::wait_for_action() {
    if (!hwnd_) return ShellUserAction::Close;

    user_action_ = ShellUserAction::None;
    MSG msg{};
    while (hwnd_) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) {
            return ShellUserAction::Close;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        if (user_action_ != ShellUserAction::None) {
            return user_action_;
        }
    }
    return ShellUserAction::Close;
}

void ShellWindow::on_paint() {
    if (!hwnd_) return;
    if (!render_.target()) {
        render_.attach(hwnd_);
    }
    if (!render_.target()) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);

    render_.target()->BeginDraw();
    paint_shell(render_.target(), render_, view_, width, height);
    render_.target()->EndDraw();
}

namespace {

constexpr wchar_t kChromeOverlayClass[] = L"OGG.Client.ChromeOverlay";
constexpr wchar_t kHeroOverlayClass[] = L"OGG.Client.HeroOverlay";
constexpr wchar_t kVersionOverlayClass[] = L"OGG.Client.VersionOverlay";

int client_chrome_height() {
    return static_cast<int>(kClientChromeHeight);
}

} // namespace

bool ShellWindow::register_version_overlay_class() {
    WNDCLASSEXW wc{};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), kVersionOverlayClass, &wc)) {
        return true;
    }
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = version_overlay_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kVersionOverlayClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassExW(&wc) != 0;
}

bool ShellWindow::register_hero_overlay_class() {
    WNDCLASSEXW wc{};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), kHeroOverlayClass, &wc)) {
        return true;
    }
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hero_overlay_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kHeroOverlayClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassExW(&wc) != 0;
}

bool ShellWindow::register_chrome_overlay_class() {
    WNDCLASSEXW wc{};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), kChromeOverlayClass, &wc)) {
        return true;
    }
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = chrome_overlay_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kChromeOverlayClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassExW(&wc) != 0;
}

void ShellWindow::ensure_client_chrome_overlay() {
    if (!hwnd_ || !view_.minimal_chrome) return;
    if (!register_chrome_overlay_class()) return;
    if (!register_hero_overlay_class()) return;

    if (!hero_overlay_) {
        hero_overlay_ = CreateWindowExW(
            0,
            kHeroOverlayClass,
            nullptr,
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            100,
            100,
            hwnd_,
            nullptr,
            GetModuleHandleW(nullptr),
            this
        );
    }

    if (!chrome_overlay_) {
        chrome_overlay_ = CreateWindowExW(
            0,
            kChromeOverlayClass,
            nullptr,
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            static_cast<int>(kTitleCloseBtnW * 2.f),
            client_chrome_height(),
            hwnd_,
            nullptr,
            GetModuleHandleW(nullptr),
            this
        );
    }

    layout_client_shell();
}

void ShellWindow::ensure_client_version_overlay(const std::wstring& version_text) {
    if (!hwnd_ || !view_.minimal_chrome || version_overlay_ || version_text.empty()) return;
    if (!register_version_overlay_class()) return;

    version_overlay_text_ = version_text;
    version_overlay_ = CreateWindowExW(
        WS_EX_TRANSPARENT,
        kVersionOverlayClass,
        nullptr,
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        static_cast<int>(kVersionOverlayMinWidth),
        static_cast<int>(kVersionOverlayHeight),
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );
    layout_version_overlay();
}

void ShellWindow::bring_client_overlays_to_front() {
    if (hero_overlay_) {
        SetWindowPos(hero_overlay_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (version_overlay_) {
        SetWindowPos(version_overlay_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (chrome_overlay_) {
        SetWindowPos(chrome_overlay_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void ShellWindow::layout_client_shell() {
    if (!hwnd_ || !view_.minimal_chrome) return;

    layout_hero_overlay();
    layout_chrome_overlay();
    layout_version_overlay();

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int login_w = static_cast<int>(kClientLoginPanelWidth);
    if (login_w > 0 && rc.bottom > 0) {
        set_embedded_webview_bounds(hwnd_, 0, 0, login_w, rc.bottom);
    }
}

void ShellWindow::layout_hero_overlay() {
    if (!hwnd_ || !hero_overlay_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int login_w = static_cast<int>(kClientLoginPanelWidth);
    const int hero_w = rc.right - login_w;
    if (hero_w <= 0) return;
    SetWindowPos(
        hero_overlay_,
        HWND_TOP,
        login_w,
        0,
        hero_w,
        rc.bottom,
        SWP_NOACTIVATE
    );
}

void ShellWindow::layout_chrome_overlay() {
    if (!hwnd_ || !chrome_overlay_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int overlay_w = static_cast<int>(kTitleCloseBtnW * 2.f);
    SetWindowPos(
        chrome_overlay_,
        HWND_TOP,
        rc.right - overlay_w,
        0,
        overlay_w,
        client_chrome_height(),
        SWP_NOACTIVATE
    );
}

void ShellWindow::layout_version_overlay() {
    if (!hwnd_ || !version_overlay_ || version_overlay_text_.empty()) return;

    HDC hdc = GetDC(hwnd_);
    int overlay_w = static_cast<int>(kVersionOverlayMinWidth);
    if (hdc) {
        static HFONT measure_font = CreateFontW(
            -12,
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
        HFONT old_font = reinterpret_cast<HFONT>(SelectObject(hdc, measure_font));
        SIZE text_size{};
        if (GetTextExtentPoint32W(
                hdc,
                version_overlay_text_.c_str(),
                static_cast<int>(version_overlay_text_.size()),
                &text_size)) {
            overlay_w = text_size.cx + static_cast<int>(kVersionOverlayMargin);
        }
        SelectObject(hdc, old_font);
        ReleaseDC(hwnd_, hdc);
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int overlay_h = static_cast<int>(kVersionOverlayHeight);
    SetWindowPos(
        version_overlay_,
        HWND_TOP,
        static_cast<int>(kVersionOverlayMargin),
        rc.bottom - overlay_h - static_cast<int>(kVersionOverlayMargin),
        overlay_w,
        overlay_h,
        SWP_NOACTIVATE
    );
}

void ShellWindow::paint_client_version(HDC hdc, const RECT& rc) {
    if (version_overlay_text_.empty()) return;

    static HFONT version_font = CreateFontW(
        -12,
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
    HFONT old_font = reinterpret_cast<HFONT>(SelectObject(hdc, version_font));
    SetBkMode(hdc, TRANSPARENT);
    const COLORREF muted = theme_ == ShellTheme::Light ? RGB(120, 120, 120) : RGB(160, 160, 160);
    SetTextColor(hdc, muted);
    DrawTextW(
        hdc,
        version_overlay_text_.c_str(),
        static_cast<int>(version_overlay_text_.size()),
        const_cast<RECT*>(&rc),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
    );
    SelectObject(hdc, old_font);
}

void ShellWindow::paint_hero_panel(HDC hdc, const RECT& rc) {
    paint_embedded_hero_art(hdc, rc);
}

void ShellWindow::paint_client_chrome(HDC hdc, const RECT& rc) {
    (void)rc;

    const D2D1_RECT_F min_rect = client_chrome_minimize_rect();
    const D2D1_RECT_F close_rect = client_chrome_close_rect();

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

    const COLORREF danger = RGB(209, 46, 46);
    const COLORREF grey_hover = RGB(210, 210, 210);
    const COLORREF muted = RGB(210, 210, 210);
    const COLORREF light = RGB(250, 250, 250);
    const COLORREF on_grey = RGB(72, 72, 72);

    SetBkMode(hdc, TRANSPARENT);

    if (hover_chrome_minimize_) {
        HBRUSH brush = CreateSolidBrush(grey_hover);
        FillRect(hdc, &min_rc, brush);
        DeleteObject(brush);
        SetTextColor(hdc, on_grey);
    } else {
        SetTextColor(hdc, muted);
    }
    DrawTextW(hdc, L"\u2013", 1, &min_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (hover_chrome_close_) {
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

void ShellWindow::update_chrome_hover(POINT pt) {
    const D2D1_RECT_F min_rect = client_chrome_minimize_rect();
    const D2D1_RECT_F close_rect = client_chrome_close_rect();

    const bool hover_min = point_in_rect(pt, min_rect);
    const bool hover_close = point_in_rect(pt, close_rect);

    if (hover_min != hover_chrome_minimize_ || hover_close != hover_chrome_close_) {
        hover_chrome_minimize_ = hover_min;
        hover_chrome_close_ = hover_close;
        if (chrome_overlay_) InvalidateRect(chrome_overlay_, nullptr, FALSE);
    }
}

LRESULT CALLBACK ShellWindow::hero_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ShellWindow* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<ShellWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_NCHITTEST:
        return HTCAPTION;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rc{};
        GetClientRect(hwnd, &client_rc);
        self->paint_hero_panel(hdc, client_rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK ShellWindow::version_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ShellWindow* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<ShellWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        self->paint_client_version(hdc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK ShellWindow::chrome_overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ShellWindow* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<ShellWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt_screen{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt_screen);
        const D2D1_RECT_F min_rect = client_chrome_minimize_rect();
        const D2D1_RECT_F close_rect = client_chrome_close_rect();
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
        self->paint_client_chrome(hdc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        self->update_chrome_hover(pt);
        if (!self->chrome_mouse_tracking_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            self->chrome_mouse_tracking_ = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        self->chrome_mouse_tracking_ = false;
        self->hover_chrome_minimize_ = false;
        self->hover_chrome_close_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const D2D1_RECT_F min_rect = client_chrome_minimize_rect();
        const D2D1_RECT_F close_rect = client_chrome_close_rect();
        if (point_in_rect(pt, close_rect)) {
            if (self->hwnd_) DestroyWindow(self->hwnd_);
            return 0;
        }
        if (point_in_rect(pt, min_rect)) {
            if (self->hwnd_) ShowWindow(self->hwnd_, SW_MINIMIZE);
            return 0;
        }
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK ShellWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ShellWindow* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<ShellWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case kMsgSetStatus: {
        auto* text = reinterpret_cast<std::wstring*>(lparam);
        if (text) {
            self->view_.status_text = *text;
            self->view_.status_ellipsis = 0;
            delete text;
            self->invalidate();
        }
        return 0;
    }

    case kMsgSetEllipsis:
        self->view_.status_ellipsis = static_cast<int>(wparam);
        if (self->view_.status_ellipsis < 0) self->view_.status_ellipsis = 0;
        if (self->view_.status_ellipsis > 3) self->view_.status_ellipsis = 3;
        self->invalidate();
        return 0;

    case kMsgSetProgress:
        self->view_.progress = static_cast<int>(wparam);
        self->invalidate();
        return 0;

    case kMsgShowButtons:
        self->view_.show_buttons = wparam != 0;
        self->invalidate();
        return 0;

    case WM_NCHITTEST: {
        POINT pt_screen{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt_screen);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float width = static_cast<float>(rc.right - rc.left);

        if (self->view_.minimal_chrome) {
            const int login_w = static_cast<int>(kClientLoginPanelWidth);
            if (pt_screen.x >= login_w) {
                const float chrome_top = kClientChromeHeight;
                const float chrome_left = width - (kTitleCloseBtnW * 2.f);
                if (static_cast<float>(pt_screen.y) < chrome_top &&
                    static_cast<float>(pt_screen.x) >= chrome_left) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        if (static_cast<float>(pt_screen.y) < kTitleBarHeight + kChromeBtnYOffset) {
            const D2D1_RECT_F close_rect = title_close_rect(width);
            if (!point_in_rect(pt_screen, close_rect)) {
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        if (!self->view_.minimal_chrome) {
            POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float width = static_cast<float>(rc.right - rc.left);
            const bool hover_close = point_in_rect(pt, title_close_rect(width));
            if (hover_close != self->view_.hover_title_close) {
                self->view_.hover_title_close = hover_close;
                self->invalidate();
            }
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (!self->view_.minimal_chrome && self->view_.hover_title_close) {
            self->view_.hover_title_close = false;
            self->invalidate();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float width = static_cast<float>(rc.right - rc.left);
        const float height = static_cast<float>(rc.bottom - rc.top);

        if (point_in_rect(pt, title_close_rect(width))) {
            self->user_action_ = ShellUserAction::Close;
            DestroyWindow(hwnd);
            return 0;
        }

        if (self->view_.show_buttons && self->view_.button_count > 0) {
            for (int i = 0; i < self->view_.button_count; ++i) {
                if (point_in_rect(pt, action_button_rect(width, height, i, self->view_.button_count))) {
                    self->user_action_ = i == 0 ? ShellUserAction::Button0 : ShellUserAction::Button1;
                    return 0;
                }
            }
        }
        return 0;
    }

    case WM_SIZE:
        self->render_.release_target();
        if (self->view_.minimal_chrome) {
            self->layout_client_shell();
        } else {
            layout_embedded_webview(hwnd);
        }
        self->invalidate();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        self->on_paint();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        self->chrome_overlay_ = nullptr;
        self->hero_overlay_ = nullptr;
        self->version_overlay_ = nullptr;
        self->hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace ogg::ui

#endif
