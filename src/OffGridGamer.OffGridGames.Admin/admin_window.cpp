#if defined(_WIN32)



#ifndef UNICODE

#define UNICODE

#endif

#ifndef _UNICODE

#define _UNICODE

#endif



#include "admin_window.hpp"



#include "server_monitor.hpp"
#include "ui/client_shell.hpp"
#include "ui/window.hpp"



#include <windowsx.h>

#include <vector>



namespace ogg::admin {



namespace {



constexpr wchar_t kAdminWindowClass[] = L"OGG.Admin.Window";

constexpr wchar_t kAdminNavClass[] = L"OGG.Admin.Nav";

constexpr wchar_t kAdminAssetsTabClass[] = L"OGG.Admin.AssetsTabs";

constexpr wchar_t kAdminSettingsTabClass[] = L"OGG.Admin.SettingsTabs";

constexpr wchar_t kAdminBadgeStripClass[] = L"OGG.Admin.BadgeStrip";



constexpr int kNavWidth = 88;

constexpr int kNavItemSize = 64;

constexpr int kNavItemGap = 12;

constexpr int kNavTopPad = 20;

constexpr int kTabBarHeight = 52;

constexpr int kTabPadTop = 16;

constexpr int kTabGap = 28;

constexpr UINT_PTR kTabUnderlineTimerId = 2;

constexpr UINT kTabAnimIntervalMs = 16;

constexpr float kTabAnimStep = 0.14f;

constexpr UINT_PTR kBadgeTimerId = 4;

constexpr UINT kBadgeTimerMs = 500;

constexpr int kBadgeStripHeight = 28;

constexpr int kChromeOverlayWidth = 64;

constexpr float kBadgeDotRadius = 4.f;

constexpr float kBadgeGap = 8.f;

constexpr float kBadgeDotTextGap = 5.f;

constexpr UINT kMsgServerMonitorChanged = WM_APP + 9;



int measure_badge_strip_width(const std::vector<std::wstring>& labels) {

    int width = 0;

    for (const std::wstring& label : labels) {

        width += static_cast<int>(label.size() * 6.4f + 24.f + kBadgeDotRadius * 2.f + kBadgeDotTextGap + kBadgeGap);

    }

    return width > 0 ? width : 120;

}



float tab_anim_ease(float t) {

    if (t <= 0.f) return 0.f;

    if (t >= 1.f) return 1.f;

    return t * t * (3.f - 2.f * t);

}



AdminWindow* admin_from_hwnd(HWND hwnd) {

    return reinterpret_cast<AdminWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

}



void draw_tab_label(
    HDC hdc,
    const RECT& text_rc,
    const wchar_t* label,
    bool active,
    COLORREF primary,
    COLORREF secondary
) {

    static HFONT tab_font = CreateFontW(

        -15,

        0,

        0,

        0,

        FW_SEMIBOLD,

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

    HFONT old_font = reinterpret_cast<HFONT>(SelectObject(hdc, tab_font));

    SetBkMode(hdc, TRANSPARENT);

    SetTextColor(hdc, active ? primary : secondary);

    DrawTextW(hdc, label, -1, const_cast<RECT*>(&text_rc), DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, old_font);

}



void draw_tab_underline(HDC hdc, int left, int width, int bottom, COLORREF primary) {

    if (width <= 0) return;

    RECT underline{};

    underline.left = left;

    underline.right = left + width;

    underline.top = bottom - 3;

    underline.bottom = bottom;

    HBRUSH brush = CreateSolidBrush(primary);

    FillRect(hdc, &underline, brush);

    DeleteObject(brush);

}



struct TabSlot {

    int block_left = 0;

    int block_width = 0;

    int text_left = 0;

    int text_right = 0;

};



TabSlot make_tab_slot(int block_left, int text_pad, int label_width) {

    TabSlot slot{};

    slot.block_left = block_left;

    slot.text_left = block_left + text_pad;

    slot.text_right = slot.text_left + label_width;

    slot.block_width = text_pad + label_width;

    return slot;

}



RECT tab_text_rect(const TabSlot& slot) {

    return RECT{ slot.text_left, kTabPadTop, slot.text_right, kTabBarHeight - 8 };

}

struct AssetsTabBarLayout {
    TabSlot general{};
    TabSlot login{};
    TabSlot images{};
    TabSlot videos{};
};

AssetsTabBarLayout layout_assets_tabs(int margin, int pad) {
    int x = margin;
    AssetsTabBarLayout out{};
    out.general = make_tab_slot(x, pad, 88);
    x += out.general.block_width + kTabGap;
    out.login = make_tab_slot(x, pad, 128);
    x += out.login.block_width + kTabGap;
    out.images = make_tab_slot(x, pad, 88);
    x += out.images.block_width + kTabGap;
    out.videos = make_tab_slot(x, pad, 88);
    return out;
}

TabSlot assets_tab_slot(const AssetsTabBarLayout& layout, AssetsTab tab) {
    switch (tab) {
    case AssetsTab::General: return layout.general;
    case AssetsTab::LoginImages: return layout.login;
    case AssetsTab::Images: return layout.images;
    case AssetsTab::Videos: return layout.videos;
    }
    return layout.general;
}

bool point_in_tab_slot(POINT pt, const TabSlot& slot) {
    return pt.x >= slot.block_left && pt.x < slot.block_left + slot.block_width;
}

} // namespace



bool AdminWindow::create() {

    const RECT work = ogg::ui::monitor_work_area_at_cursor();

    const ogg::ui::WindowPlacement place = ogg::ui::placement_on_work_area(work, ogg::ui::admin_window_policy());



    WNDCLASSEXW wc{};

    if (!GetClassInfoExW(GetModuleHandleW(nullptr), kAdminWindowClass, &wc)) {

        wc.cbSize = sizeof(wc);

        wc.lpfnWndProc = AdminWindow::window_proc;

        wc.hInstance = GetModuleHandleW(nullptr);

        wc.lpszClassName = kAdminWindowClass;

        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        wc.hbrBackground = CreateSolidBrush(RGB(244, 246, 248));

        if (!RegisterClassExW(&wc)) return false;

    }



    auto register_child = [](const wchar_t* class_name, WNDPROC proc, COLORREF bg) -> bool {

        WNDCLASSEXW child{};

        if (GetClassInfoExW(GetModuleHandleW(nullptr), class_name, &child)) return true;

        child.cbSize = sizeof(child);

        child.lpfnWndProc = proc;

        child.hInstance = GetModuleHandleW(nullptr);

        child.lpszClassName = class_name;

        child.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        child.hbrBackground = CreateSolidBrush(bg);

        return RegisterClassExW(&child) != 0;

    };



    if (!register_child(kAdminNavClass, AdminWindow::nav_proc, RGB(17, 24, 39))) return false;

    if (!register_child(kAdminAssetsTabClass, AdminWindow::assets_tab_proc, RGB(244, 246, 248))) return false;

    if (!register_child(kAdminSettingsTabClass, AdminWindow::settings_tab_proc, RGB(244, 246, 248))) return false;

    if (!register_child(kAdminBadgeStripClass, AdminWindow::badge_strip_proc, RGB(244, 246, 248))) return false;



    hwnd_ = CreateWindowExW(

        WS_EX_APPWINDOW,

        kAdminWindowClass,

        L"OGG Admin",

        WS_POPUP | WS_VISIBLE,

        place.x,

        place.y,

        place.width,

        place.height,

        nullptr,

        nullptr,

        GetModuleHandleW(nullptr),

        this

    );

    if (!hwnd_) return false;



    nav_hwnd_ = CreateWindowExW(0, kAdminNavClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, kNavWidth, place.height, hwnd_, nullptr, GetModuleHandleW(nullptr), this);

    assets_tab_hwnd_ = CreateWindowExW(0, kAdminAssetsTabClass, nullptr, WS_CHILD, 0, 0, 400, kTabBarHeight, hwnd_, nullptr, GetModuleHandleW(nullptr), this);

    settings_tab_hwnd_ = CreateWindowExW(0, kAdminSettingsTabClass, nullptr, WS_CHILD, 0, 0, 400, kTabBarHeight, hwnd_, nullptr, GetModuleHandleW(nullptr), this);



    if (!nav_hwnd_ || !assets_tab_hwnd_ || !settings_tab_hwnd_) return false;



    ensure_chrome_overlay();

    start_server_status_monitor();

    init_tab_underlines();

    layout_panels();

    return true;

}



void AdminWindow::show() {

    if (hwnd_) {

        ShowWindow(hwnd_, SW_SHOW);

        UpdateWindow(hwnd_);

    }

}



void AdminWindow::close() {

    stop_server_status_monitor();

    settings_docs_ui_.destroy();

    settings_typography_ui_.destroy();
    settings_appearance_ui_.destroy();

    settings_ai_ui_.destroy();

    assets_login_ui_.destroy();

    assets_images_ui_.destroy();

    assets_videos_ui_.destroy();

    assets_general_ui_.destroy();

    users_ui_.destroy();

    dashboard_ui_.destroy();

    if (settings_tab_hwnd_) { DestroyWindow(settings_tab_hwnd_); settings_tab_hwnd_ = nullptr; }

    if (assets_tab_hwnd_) { DestroyWindow(assets_tab_hwnd_); assets_tab_hwnd_ = nullptr; }

    if (nav_hwnd_) { DestroyWindow(nav_hwnd_); nav_hwnd_ = nullptr; }

    if (badge_strip_hwnd_) { DestroyWindow(badge_strip_hwnd_); badge_strip_hwnd_ = nullptr; }

    chrome_.destroy();

    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }

}



void AdminWindow::set_nav_handler(void* user_data, NavHandler handler) {

    nav_user_data_ = user_data;

    nav_handler_ = handler;

}



void AdminWindow::set_assets_tab_handler(void* user_data, TabHandler handler) {

    assets_tab_user_data_ = user_data;

    assets_tab_handler_ = handler;

}



void AdminWindow::set_settings_tab_handler(void* user_data, SettingsTabHandler handler) {

    settings_tab_user_data_ = user_data;

    settings_tab_handler_ = handler;

}



void AdminWindow::set_theme_colors(COLORREF primary, COLORREF secondary) {

    primary_color_ = primary;

    secondary_color_ = secondary;

    chrome_.set_theme(ogg::ui::ShellTheme::Light);

    if (chrome_.hwnd()) InvalidateRect(chrome_.hwnd(), nullptr, FALSE);

    if (assets_tab_hwnd_) InvalidateRect(assets_tab_hwnd_, nullptr, FALSE);

    if (settings_tab_hwnd_) InvalidateRect(settings_tab_hwnd_, nullptr, FALSE);

}



void AdminWindow::set_tab_layout(int margin_left, int text_pad_left) {

    tab_margin_left_ = margin_left < 0 ? 0 : margin_left;

    tab_pad_left_ = text_pad_left < 0 ? 0 : text_pad_left;

    if (assets_tab_hwnd_) InvalidateRect(assets_tab_hwnd_, nullptr, FALSE);

    if (settings_tab_hwnd_) InvalidateRect(settings_tab_hwnd_, nullptr, FALSE);

}



void AdminWindow::show_section(AdminSection section) {

    section_ = section;

    if (nav_hwnd_) InvalidateRect(nav_hwnd_, nullptr, FALSE);

    layout_content();

}



void AdminWindow::show_assets_tab(AssetsTab tab) {

    if (tab != assets_tab_) {

        start_assets_tab_underline_anim(tab);

    }

    assets_tab_ = tab;

    if (assets_tab_hwnd_) InvalidateRect(assets_tab_hwnd_, nullptr, FALSE);

    layout_content();

}



void AdminWindow::show_settings_tab(SettingsTab tab) {

    if (tab != settings_tab_) {

        start_settings_tab_underline_anim(tab);

    }

    settings_tab_ = tab;

    if (settings_tab_hwnd_) InvalidateRect(settings_tab_hwnd_, nullptr, FALSE);

    layout_content();

}



void AdminWindow::layout_nav() {

    if (!hwnd_ || !nav_hwnd_) return;

    RECT rc{};

    GetClientRect(hwnd_, &rc);

    SetWindowPos(nav_hwnd_, HWND_TOP, 0, 0, kNavWidth, rc.bottom, SWP_NOACTIVATE);

}



void AdminWindow::ensure_chrome_overlay() {

    if (!hwnd_) return;

    chrome_.set_theme(ogg::ui::ShellTheme::Light);

    if (!chrome_.hwnd()) {

        chrome_.ensure(hwnd_, this, chrome_close, chrome_minimize);

    }

}



void AdminWindow::layout_chrome() {

    if (!hwnd_ || !chrome_.hwnd()) return;

    RECT rc{};

    GetClientRect(hwnd_, &rc);

    chrome_.layout(rc.right);

    SetWindowPos(chrome_.hwnd(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    layout_server_badge_strip();

}



void AdminWindow::chrome_close(void* user_data) {

    auto* self = static_cast<AdminWindow*>(user_data);

    if (self) self->close();

}



void AdminWindow::chrome_minimize(void* user_data) {

    auto* self = static_cast<AdminWindow*>(user_data);

    if (self && self->hwnd_) ShowWindow(self->hwnd_, SW_MINIMIZE);

}



void AdminWindow::layout_panels() {

    layout_nav();

    layout_content();

    layout_chrome();

}



void AdminWindow::layout_content() {

    if (!hwnd_) return;

    RECT rc{};

    GetClientRect(hwnd_, &rc);

    const int content_x = kNavWidth;

    const int content_w = rc.right - content_x;

    const int content_h = rc.bottom;



    auto hide = [&](HWND hwnd) {

        if (hwnd) ShowWindow(hwnd, SW_HIDE);

    };

    hide(dashboard_ui_.hwnd());

    hide(users_ui_.hwnd());

    hide(assets_tab_hwnd_);

    hide(assets_general_ui_.hwnd());

    hide(assets_login_ui_.hwnd());

    hide(assets_images_ui_.hwnd());

    hide(assets_videos_ui_.hwnd());

    hide(settings_tab_hwnd_);

    hide(settings_ai_ui_.hwnd());

    hide(settings_appearance_ui_.hwnd());

    hide(settings_typography_ui_.hwnd());

    hide(settings_docs_ui_.hwnd());



    switch (section_) {

    case AdminSection::Dashboard:

        if (dashboard_ui_.hwnd()) {

            dashboard_ui_.layout(content_x, 0, content_w, content_h);

            ShowWindow(dashboard_ui_.hwnd(), SW_SHOW);

        }

        break;

    case AdminSection::Users:

        if (users_ui_.hwnd()) {

            users_ui_.layout(content_x, 0, content_w, content_h);

            ShowWindow(users_ui_.hwnd(), SW_SHOW);

        }

        break;

    case AdminSection::Assets:

        if (assets_tab_hwnd_) {

            SetWindowPos(assets_tab_hwnd_, HWND_TOP, content_x, 0, content_w, kTabBarHeight, SWP_NOACTIVATE);

            ShowWindow(assets_tab_hwnd_, SW_SHOW);

        }

        if (assets_tab_ == AssetsTab::General) {

            if (assets_general_ui_.hwnd()) {

                assets_general_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(assets_general_ui_.hwnd(), SW_SHOW);

            }

        } else if (assets_tab_ == AssetsTab::LoginImages) {

            if (assets_login_ui_.hwnd()) {

                assets_login_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(assets_login_ui_.hwnd(), SW_SHOW);

            }

        } else if (assets_tab_ == AssetsTab::Images) {

            if (assets_images_ui_.hwnd()) {

                assets_images_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(assets_images_ui_.hwnd(), SW_SHOW);

            }

        } else if (assets_tab_ == AssetsTab::Videos && assets_videos_ui_.hwnd()) {

            assets_videos_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

            ShowWindow(assets_videos_ui_.hwnd(), SW_SHOW);

        }

        break;

    case AdminSection::Settings:

        if (settings_tab_hwnd_) {

            SetWindowPos(settings_tab_hwnd_, HWND_TOP, content_x, 0, content_w, kTabBarHeight, SWP_NOACTIVATE);

            ShowWindow(settings_tab_hwnd_, SW_SHOW);

        }

        if (settings_tab_ == SettingsTab::AI) {

            if (settings_ai_ui_.hwnd()) {

                settings_ai_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(settings_ai_ui_.hwnd(), SW_SHOW);

            }

        } else if (settings_tab_ == SettingsTab::Appearance) {

            if (settings_appearance_ui_.hwnd()) {

                settings_appearance_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(settings_appearance_ui_.hwnd(), SW_SHOW);

            }

        } else if (settings_tab_ == SettingsTab::Typography) {

            if (settings_typography_ui_.hwnd()) {

                settings_typography_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

                ShowWindow(settings_typography_ui_.hwnd(), SW_SHOW);

            }

        } else if (settings_tab_ == SettingsTab::Docs && settings_docs_ui_.hwnd()) {

            settings_docs_ui_.layout(content_x, kTabBarHeight, content_w, content_h - kTabBarHeight);

            ShowWindow(settings_docs_ui_.hwnd(), SW_SHOW);

        }

        break;

    }

}



void AdminWindow::assets_tab_underline_metrics(AssetsTab tab, int& left, int& width) const {

    const int margin = tab_margin_left_;

    const int pad = tab_pad_left_;

    const AssetsTabBarLayout layout = layout_assets_tabs(margin, pad);

    const TabSlot slot = assets_tab_slot(layout, tab);

    const RECT text_rc = tab_text_rect(slot);

    left = slot.block_left;

    width = text_rc.right - slot.block_left;

}



void AdminWindow::settings_tab_underline_metrics(SettingsTab tab, int& left, int& width) const {

    const int margin = tab_margin_left_;

    const int pad = tab_pad_left_;

    int x = margin;

    const TabSlot ai = make_tab_slot(x, pad, 50);

    x += ai.block_width + kTabGap;

    const TabSlot appearance = make_tab_slot(x, pad, 120);

    x += appearance.block_width + kTabGap;

    const TabSlot typography = make_tab_slot(x, pad, 120);

    x += typography.block_width + kTabGap;

    const TabSlot docs = make_tab_slot(x, pad, 60);

    const TabSlot* slot = &ai;

    if (tab == SettingsTab::Appearance) slot = &appearance;

    else if (tab == SettingsTab::Typography) slot = &typography;

    else if (tab == SettingsTab::Docs) slot = &docs;

    const RECT text_rc = tab_text_rect(*slot);

    left = slot->block_left;

    width = text_rc.right - slot->block_left;

}



void AdminWindow::init_tab_underlines() {

    int left = 0;

    int width = 0;

    assets_tab_underline_metrics(assets_tab_, left, width);

    assets_tab_underline_.x = static_cast<float>(left);

    assets_tab_underline_.width = static_cast<float>(width);

    assets_tab_underline_.progress = 1.f;

    settings_tab_underline_metrics(settings_tab_, left, width);

    settings_tab_underline_.x = static_cast<float>(left);

    settings_tab_underline_.width = static_cast<float>(width);

    settings_tab_underline_.progress = 1.f;

}



void AdminWindow::start_assets_tab_underline_anim(AssetsTab tab) {

    int to_left = 0;

    int to_width = 0;

    assets_tab_underline_metrics(tab, to_left, to_width);

    TabUnderlineAnimState& anim = assets_tab_underline_;

    anim.from_x = anim.x;

    anim.from_width = anim.width;

    anim.to_x = static_cast<float>(to_left);

    anim.to_width = static_cast<float>(to_width);

    anim.progress = 0.f;

    if (assets_tab_hwnd_) {

        SetTimer(assets_tab_hwnd_, kTabUnderlineTimerId, kTabAnimIntervalMs, nullptr);

    }

}



void AdminWindow::start_settings_tab_underline_anim(SettingsTab tab) {

    int to_left = 0;

    int to_width = 0;

    settings_tab_underline_metrics(tab, to_left, to_width);

    TabUnderlineAnimState& anim = settings_tab_underline_;

    anim.from_x = anim.x;

    anim.from_width = anim.width;

    anim.to_x = static_cast<float>(to_left);

    anim.to_width = static_cast<float>(to_width);

    anim.progress = 0.f;

    if (settings_tab_hwnd_) {

        SetTimer(settings_tab_hwnd_, kTabUnderlineTimerId, kTabAnimIntervalMs, nullptr);

    }

}



void AdminWindow::tick_assets_tab_underline_anim() {

    TabUnderlineAnimState& anim = assets_tab_underline_;

    if (anim.progress >= 1.f) return;

    anim.progress += kTabAnimStep;

    if (anim.progress > 1.f) anim.progress = 1.f;

    const float t = tab_anim_ease(anim.progress);

    anim.x = anim.from_x + (anim.to_x - anim.from_x) * t;

    anim.width = anim.from_width + (anim.to_width - anim.from_width) * t;

}



void AdminWindow::tick_settings_tab_underline_anim() {

    TabUnderlineAnimState& anim = settings_tab_underline_;

    if (anim.progress >= 1.f) return;

    anim.progress += kTabAnimStep;

    if (anim.progress > 1.f) anim.progress = 1.f;

    const float t = tab_anim_ease(anim.progress);

    anim.x = anim.from_x + (anim.to_x - anim.from_x) * t;

    anim.width = anim.from_width + (anim.to_width - anim.from_width) * t;

}



void AdminWindow::paint_nav(HDC hdc, const RECT& rc) {

    HBRUSH bg = CreateSolidBrush(RGB(17, 24, 39));

    FillRect(hdc, &rc, bg);

    DeleteObject(bg);



    static HFONT icon_font = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    HFONT old_font = reinterpret_cast<HFONT>(SelectObject(hdc, icon_font));

    SetBkMode(hdc, TRANSPARENT);



    struct NavItem { AdminSection section; const wchar_t* glyph; };

    const NavItem items[] = {

        { AdminSection::Dashboard, L"\uE80F" },

        { AdminSection::Users, L"\uE77B" },

        { AdminSection::Assets, L"\uE91B" },

        { AdminSection::Settings, L"\uE713" },

    };



    int y = kNavTopPad;

    for (const NavItem& item : items) {

        RECT item_rc{ (rc.right - kNavItemSize) / 2, y, (rc.right + kNavItemSize) / 2, y + kNavItemSize };

        if (section_ == item.section) {

            HBRUSH fill = CreateSolidBrush(primary_color_);

            FillRect(hdc, &item_rc, fill);

            DeleteObject(fill);

            SetTextColor(hdc, RGB(255, 255, 255));

        } else {

            SetTextColor(hdc, RGB(156, 163, 175));

        }

        DrawTextW(hdc, item.glyph, 1, &item_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        y += kNavItemSize + kNavItemGap;

    }

    SelectObject(hdc, old_font);

}



void AdminWindow::paint_assets_tabs(HDC hdc, const RECT& rc) {

    HBRUSH bg = CreateSolidBrush(RGB(244, 246, 248));

    FillRect(hdc, &rc, bg);

    DeleteObject(bg);

    const int margin = tab_margin_left_;

    const int pad = tab_pad_left_;

    const AssetsTabBarLayout layout = layout_assets_tabs(margin, pad);

    draw_tab_label(hdc, tab_text_rect(layout.general), L"GENERAL", assets_tab_ == AssetsTab::General, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(layout.login), L"LOGIN IMAGES", assets_tab_ == AssetsTab::LoginImages, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(layout.images), L"IMAGES", assets_tab_ == AssetsTab::Images, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(layout.videos), L"VIDEOS", assets_tab_ == AssetsTab::Videos, primary_color_, secondary_color_);

    const int underline_left = static_cast<int>(assets_tab_underline_.x + 0.5f);

    const int underline_width = static_cast<int>(assets_tab_underline_.width + 0.5f);

    draw_tab_underline(hdc, underline_left, underline_width, kTabBarHeight - 8, primary_color_);

}



void AdminWindow::paint_settings_tabs(HDC hdc, const RECT& rc) {

    HBRUSH bg = CreateSolidBrush(RGB(244, 246, 248));

    FillRect(hdc, &rc, bg);

    DeleteObject(bg);



    const int margin = tab_margin_left_;

    const int pad = tab_pad_left_;

    int x = margin;

    const TabSlot ai = make_tab_slot(x, pad, 50);

    x += ai.block_width + kTabGap;

    const TabSlot appearance = make_tab_slot(x, pad, 120);

    x += appearance.block_width + kTabGap;

    const TabSlot typography = make_tab_slot(x, pad, 120);

    x += typography.block_width + kTabGap;

    const TabSlot docs = make_tab_slot(x, pad, 60);

    draw_tab_label(hdc, tab_text_rect(ai), L"AI", settings_tab_ == SettingsTab::AI, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(appearance), L"APPEARANCE", settings_tab_ == SettingsTab::Appearance, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(typography), L"TYPOGRAPHY", settings_tab_ == SettingsTab::Typography, primary_color_, secondary_color_);

    draw_tab_label(hdc, tab_text_rect(docs), L"DOCS", settings_tab_ == SettingsTab::Docs, primary_color_, secondary_color_);

    const int underline_left = static_cast<int>(settings_tab_underline_.x + 0.5f);

    const int underline_width = static_cast<int>(settings_tab_underline_.width + 0.5f);

    draw_tab_underline(hdc, underline_left, underline_width, kTabBarHeight - 8, primary_color_);

}



LRESULT CALLBACK AdminWindow::nav_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    AdminWindow* self = reinterpret_cast<AdminWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CREATE) {

        self = reinterpret_cast<AdminWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);



    switch (msg) {

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {

        PAINTSTRUCT ps{};

        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{}; GetClientRect(hwnd, &rc);

        self->paint_nav(hdc, rc);

        EndPaint(hwnd, &ps);

        return 0;

    }

    case WM_LBUTTONUP: {

        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        const int local_y = pt.y - kNavTopPad;

        if (local_y < 0) return 0;

        const int stride = kNavItemSize + kNavItemGap;

        const int index = local_y / stride;

        if (index < 0 || index > 3) return 0;

        if ((local_y % stride) > kNavItemSize) return 0;

        const AdminSection section = static_cast<AdminSection>(index);

        self->show_section(section);

        if (self->nav_handler_) self->nav_handler_(self->nav_user_data_, section);

        return 0;

    }

    default: return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

}



LRESULT CALLBACK AdminWindow::assets_tab_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    AdminWindow* self = reinterpret_cast<AdminWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CREATE) {

        self = reinterpret_cast<AdminWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);



    switch (msg) {

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {

        PAINTSTRUCT ps{};

        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{}; GetClientRect(hwnd, &rc);

        self->paint_assets_tabs(hdc, rc);

        EndPaint(hwnd, &ps);

        return 0;

    }

    case WM_LBUTTONUP: {

        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        const int margin = self->tab_margin_left_;

        const int pad = self->tab_pad_left_;

        const AssetsTabBarLayout layout = layout_assets_tabs(margin, pad);

        AssetsTab clicked = AssetsTab::General;

        if (point_in_tab_slot(pt, layout.general)) {

            clicked = AssetsTab::General;

        } else if (point_in_tab_slot(pt, layout.login)) {

            clicked = AssetsTab::LoginImages;

        } else if (point_in_tab_slot(pt, layout.images)) {

            clicked = AssetsTab::Images;

        } else if (point_in_tab_slot(pt, layout.videos)) {

            clicked = AssetsTab::Videos;

        } else {

            return 0;

        }

        self->show_assets_tab(clicked);

        if (self->assets_tab_handler_) self->assets_tab_handler_(self->assets_tab_user_data_, clicked);

        return 0;

    }

    case WM_TIMER:

        if (wparam == kTabUnderlineTimerId) {

            self->tick_assets_tab_underline_anim();

            if (self->assets_tab_underline_.progress >= 1.f) {

                KillTimer(hwnd, kTabUnderlineTimerId);

            }

            InvalidateRect(hwnd, nullptr, FALSE);

            return 0;

        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    default: return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

}



LRESULT CALLBACK AdminWindow::settings_tab_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    AdminWindow* self = reinterpret_cast<AdminWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CREATE) {

        self = reinterpret_cast<AdminWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);



    switch (msg) {

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {

        PAINTSTRUCT ps{};

        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{}; GetClientRect(hwnd, &rc);

        self->paint_settings_tabs(hdc, rc);

        EndPaint(hwnd, &ps);

        return 0;

    }

    case WM_LBUTTONUP: {

        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        const int margin = self->tab_margin_left_;

        const int pad = self->tab_pad_left_;

        int x = margin;

        const TabSlot ai = make_tab_slot(x, pad, 50);

        x += ai.block_width + kTabGap;

        const TabSlot appearance = make_tab_slot(x, pad, 120);

        x += appearance.block_width + kTabGap;

        const TabSlot typography = make_tab_slot(x, pad, 120);

        x += typography.block_width + kTabGap;

        const TabSlot docs = make_tab_slot(x, pad, 60);

        if (pt.x >= ai.block_left && pt.x < ai.block_left + ai.block_width) {

            self->show_settings_tab(SettingsTab::AI);

            if (self->settings_tab_handler_) self->settings_tab_handler_(self->settings_tab_user_data_, SettingsTab::AI);

        } else if (pt.x >= appearance.block_left && pt.x < appearance.block_left + appearance.block_width) {

            self->show_settings_tab(SettingsTab::Appearance);

            if (self->settings_tab_handler_) self->settings_tab_handler_(self->settings_tab_user_data_, SettingsTab::Appearance);

        } else if (pt.x >= typography.block_left && pt.x < typography.block_left + typography.block_width) {

            self->show_settings_tab(SettingsTab::Typography);

            if (self->settings_tab_handler_) self->settings_tab_handler_(self->settings_tab_user_data_, SettingsTab::Typography);

        } else if (pt.x >= docs.block_left && pt.x < docs.block_left + docs.block_width) {

            self->show_settings_tab(SettingsTab::Docs);

            if (self->settings_tab_handler_) self->settings_tab_handler_(self->settings_tab_user_data_, SettingsTab::Docs);

        }

        return 0;

    }

    case WM_TIMER:

        if (wparam == kTabUnderlineTimerId) {

            self->tick_settings_tab_underline_anim();

            if (self->settings_tab_underline_.progress >= 1.f) {

                KillTimer(hwnd, kTabUnderlineTimerId);

            }

            InvalidateRect(hwnd, nullptr, FALSE);

            return 0;

        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    default: return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

}



void AdminWindow::start_server_status_monitor() {

    stop_server_status_monitor();

    if (!hwnd_) return;

    server_monitor_.set_change_handler(this, AdminWindow::on_server_monitor_changed);

    server_monitor_.start(ogg::server_monitor::default_targets());

    server_monitor_active_ = true;

    ensure_server_badge_strip();

    refresh_server_badges();

    if (badge_strip_hwnd_) {

        SetTimer(badge_strip_hwnd_, kBadgeTimerId, kBadgeTimerMs, nullptr);

    }

    layout_server_badge_strip();

}



void AdminWindow::on_server_monitor_changed(void* user_data) {

    auto* self = static_cast<AdminWindow*>(user_data);

    if (!self || !self->hwnd_) return;

    PostMessageW(self->hwnd_, kMsgServerMonitorChanged, 0, 0);

}



void AdminWindow::stop_server_status_monitor() {

    if (badge_strip_hwnd_) KillTimer(badge_strip_hwnd_, kBadgeTimerId);

    server_monitor_.stop();

    server_monitor_active_ = false;

    server_badge_blink_bright_ = false;

    server_badge_labels_.clear();

    server_badge_up_.clear();

    server_badge_polling_.clear();

    server_badge_checking_.clear();

    server_badge_local_.clear();

}



void AdminWindow::ensure_server_badge_strip() {

    if (!hwnd_ || badge_strip_hwnd_) return;

    badge_strip_hwnd_ = CreateWindowExW(

        0,

        kAdminBadgeStripClass,

        nullptr,

        WS_CHILD | WS_VISIBLE,

        0,

        0,

        120,

        kBadgeStripHeight,

        hwnd_,

        nullptr,

        GetModuleHandleW(nullptr),

        this

    );

}



void AdminWindow::layout_server_badge_strip() {

    if (!hwnd_ || !badge_strip_hwnd_) return;

    RECT rc{};

    GetClientRect(hwnd_, &rc);

    const int strip_w = measure_badge_strip_width(server_badge_labels_);

    const int x = rc.right - kChromeOverlayWidth - strip_w - 6;

    SetWindowPos(

        badge_strip_hwnd_,

        HWND_TOP,

        x > 0 ? x : 0,

        0,

        strip_w,

        kBadgeStripHeight,

        SWP_NOACTIVATE

    );

}



void AdminWindow::refresh_server_badges() {

    if (!server_monitor_active_) return;

    const auto badges = server_monitor_.snapshot();

    server_badge_labels_.clear();

    server_badge_up_.clear();

    server_badge_polling_.clear();

    server_badge_checking_.clear();

    server_badge_local_.clear();

    server_badge_labels_.reserve(badges.size());

    server_badge_up_.reserve(badges.size());

    server_badge_polling_.reserve(badges.size());

    server_badge_checking_.reserve(badges.size());

    server_badge_local_.reserve(badges.size());

    for (const auto& badge : badges) {

        server_badge_labels_.push_back(badge.label);

        server_badge_up_.push_back(badge.up);

        server_badge_polling_.push_back(badge.polling);

        server_badge_checking_.push_back(badge.checking);

        server_badge_local_.push_back(badge.local_probe);

    }

    layout_server_badge_strip();

    if (badge_strip_hwnd_) InvalidateRect(badge_strip_hwnd_, nullptr, FALSE);

}



void AdminWindow::paint_server_badges(HDC hdc, const RECT& rc) {

    HBRUSH bg = CreateSolidBrush(RGB(244, 246, 248));

    FillRect(hdc, &rc, bg);

    DeleteObject(bg);

    if (server_badge_labels_.empty()) return;



    HFONT font = CreateFontW(

        -13,

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

    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));

    SetBkMode(hdc, TRANSPARENT);



    const COLORREF bright_green = RGB(89, 235, 107);

    const COLORREF dull_green = RGB(46, 140, 61);

    const COLORREF bright_orange = RGB(255, 184, 56);

    const COLORREF dull_orange = RGB(199, 122, 31);

    const COLORREF red = RGB(235, 71, 71);

    const COLORREF text_color = RGB(107, 114, 128);



    int cursor_x = rc.right - 6;

    for (int i = static_cast<int>(server_badge_labels_.size()) - 1; i >= 0; --i) {

        const std::wstring& label = server_badge_labels_[static_cast<size_t>(i)];

        const bool up = server_badge_up_[static_cast<size_t>(i)];

        const bool polling = server_badge_polling_[static_cast<size_t>(i)];

        const bool checking = server_badge_checking_[static_cast<size_t>(i)];

        const bool local = server_badge_local_[static_cast<size_t>(i)];

        const int text_w = static_cast<int>(label.size() * 6.4f + 24.f);

        cursor_x -= text_w + static_cast<int>(kBadgeDotRadius * 2.f + kBadgeDotTextGap + kBadgeGap);



        const int dot_x = cursor_x + static_cast<int>(kBadgeDotRadius);

        const int dot_y = 14;

        COLORREF dot_color = red;

        if (checking) {

            dot_color = server_badge_blink_bright_ ? bright_orange : dull_orange;

        } else if (up) {

            if (local) {

                dot_color = polling ? dull_green

                                    : (server_badge_blink_bright_ ? bright_green : dull_green);

            } else {

                dot_color = bright_green;

            }

        }

        HBRUSH dot_brush = CreateSolidBrush(dot_color);

        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, dot_brush));

        HPEN null_pen = static_cast<HPEN>(GetStockObject(NULL_PEN));

        HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, null_pen));

        Ellipse(

            hdc,

            dot_x - static_cast<int>(kBadgeDotRadius),

            dot_y - static_cast<int>(kBadgeDotRadius),

            dot_x + static_cast<int>(kBadgeDotRadius),

            dot_y + static_cast<int>(kBadgeDotRadius)

        );

        SelectObject(hdc, old_pen);

        SelectObject(hdc, old_brush);

        DeleteObject(dot_brush);



        SetTextColor(hdc, text_color);

        RECT text_rc{

            cursor_x + static_cast<int>(kBadgeDotRadius * 2.f + kBadgeDotTextGap),

            4,

            cursor_x + static_cast<int>(kBadgeDotRadius * 2.f + kBadgeDotTextGap) + text_w,

            24,

        };

        DrawTextW(hdc, label.c_str(), static_cast<int>(label.size()), &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    }



    SelectObject(hdc, old_font);

    DeleteObject(font);

}



int AdminWindow::hit_server_badge_index(POINT pt) const {

    if (server_badge_labels_.empty()) return -1;

    if (pt.y < 0 || pt.y >= kBadgeStripHeight) return -1;



    int cursor_x = 0;

    RECT rc{};

    if (badge_strip_hwnd_) {

        GetClientRect(badge_strip_hwnd_, &rc);

        cursor_x = rc.right - 6;

    }



    for (int i = static_cast<int>(server_badge_labels_.size()) - 1; i >= 0; --i) {

        const std::wstring& label = server_badge_labels_[static_cast<size_t>(i)];

        const int text_w = static_cast<int>(label.size() * 6.4f + 24.f);

        const int badge_w = text_w + static_cast<int>(kBadgeDotRadius * 2.f + kBadgeDotTextGap + kBadgeGap);

        cursor_x -= badge_w;

        if (pt.x >= cursor_x && pt.x < cursor_x + badge_w - static_cast<int>(kBadgeGap)) {

            return i;

        }

    }

    return -1;

}



LRESULT CALLBACK AdminWindow::badge_strip_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    AdminWindow* self = reinterpret_cast<AdminWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CREATE) {

        self = reinterpret_cast<AdminWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);



    switch (msg) {

    case WM_ERASEBKGND:

        return 1;

    case WM_LBUTTONUP: {

        const POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        const int badge_index = self->hit_server_badge_index(pt);

        if (badge_index >= 0 &&

            badge_index < static_cast<int>(self->server_badge_local_.size()) &&

            !self->server_badge_local_[static_cast<size_t>(badge_index)]) {

            self->server_monitor_.recheck(static_cast<std::size_t>(badge_index));

            return 0;

        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

    case WM_PAINT: {

        PAINTSTRUCT ps{};

        HDC hdc = BeginPaint(hwnd, &ps);

        self->paint_server_badges(hdc, ps.rcPaint);

        EndPaint(hwnd, &ps);

        return 0;

    }

    case WM_TIMER:

        if (wparam == kBadgeTimerId) {

            self->server_badge_blink_bright_ = !self->server_badge_blink_bright_;

            InvalidateRect(hwnd, nullptr, FALSE);

            return 0;

        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    default:

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

}



LRESULT CALLBACK AdminWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

    AdminWindow* self = admin_from_hwnd(hwnd);

    if (msg == WM_CREATE) {

        self = reinterpret_cast<AdminWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

    }

    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);



    switch (msg) {

    case WM_NCHITTEST: {

        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        ScreenToClient(hwnd, &pt);

        RECT rc{};

        GetClientRect(hwnd, &rc);

        const float width = static_cast<float>(rc.right - rc.left);

        if (ogg::ui::client_shell::allows_window_drag(pt, width)) {

            return HTCAPTION;

        }

        return HTCLIENT;

    }

    case WM_SIZE:

        self->layout_panels();

        return 0;

    case kMsgServerMonitorChanged:

        self->refresh_server_badges();

        return 0;

    case WM_DESTROY:

        self->hwnd_ = nullptr;

        PostQuitMessage(0);

        return 0;

    default:

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    }

}



} // namespace ogg::admin



#endif


