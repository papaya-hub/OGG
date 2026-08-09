#pragma once



#if defined(_WIN32)



#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#include <windows.h>



#include <functional>

#include <string>



#include "ui/client_shell.hpp"
#include "ui/xml_ui.hpp"
#include "server_monitor.hpp"



namespace ogg::admin {



struct TabUnderlineAnimState {

    float x = 0.f;

    float width = 0.f;

    float from_x = 0.f;

    float from_width = 0.f;

    float to_x = 0.f;

    float to_width = 0.f;

    float progress = 1.f;

};



enum class AdminSection {

    Dashboard,

    Users,

    Assets,

    Settings,

};



enum class AssetsTab {

    General,

    LoginImages,

    Images,

    Videos,

};



enum class SettingsTab {

    AI,

    Appearance,

    Typography,

    Docs,

};



class AdminWindow {

public:

    using NavHandler = void (*)(void* user_data, AdminSection section);

    using TabHandler = void (*)(void* user_data, AssetsTab tab);

    using SettingsTabHandler = void (*)(void* user_data, SettingsTab tab);



    bool create();

    void show();

    void close();

    HWND hwnd() const { return hwnd_; }



    void set_nav_handler(void* user_data, NavHandler handler);

    void set_assets_tab_handler(void* user_data, TabHandler handler);

    void set_settings_tab_handler(void* user_data, SettingsTabHandler handler);



    void set_theme_colors(COLORREF primary, COLORREF secondary);

    void set_tab_layout(int margin_left, int text_pad_left);

    void start_server_status_monitor();
    void stop_server_status_monitor();



    void show_section(AdminSection section);

    void show_assets_tab(AssetsTab tab);

    void show_settings_tab(SettingsTab tab);

    void layout_content();



    ogg::ui::XmlUiHost& dashboard_ui() { return dashboard_ui_; }

    ogg::ui::XmlUiHost& users_ui() { return users_ui_; }

    ogg::ui::XmlUiHost& assets_general_ui() { return assets_general_ui_; }

    ogg::ui::XmlUiHost& assets_login_ui() { return assets_login_ui_; }
    ogg::ui::XmlUiHost& assets_images_ui() { return assets_images_ui_; }
    ogg::ui::XmlUiHost& assets_videos_ui() { return assets_videos_ui_; }

    ogg::ui::XmlUiHost& settings_ai_ui() { return settings_ai_ui_; }

    ogg::ui::XmlUiHost& settings_appearance_ui() { return settings_appearance_ui_; }

    ogg::ui::XmlUiHost& settings_typography_ui() { return settings_typography_ui_; }

    ogg::ui::XmlUiHost& settings_docs_ui() { return settings_docs_ui_; }



private:

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    static LRESULT CALLBACK nav_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    static LRESULT CALLBACK assets_tab_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    static LRESULT CALLBACK settings_tab_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    static LRESULT CALLBACK badge_strip_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);



    void paint_nav(HDC hdc, const RECT& rc);

    void paint_assets_tabs(HDC hdc, const RECT& rc);

    void paint_settings_tabs(HDC hdc, const RECT& rc);

    void layout_nav();

    void layout_panels();

    void ensure_chrome_overlay();

    void layout_chrome();

    static void chrome_close(void* user_data);

    static void chrome_minimize(void* user_data);

    void init_tab_underlines();

    void assets_tab_underline_metrics(AssetsTab tab, int& left, int& width) const;

    void settings_tab_underline_metrics(SettingsTab tab, int& left, int& width) const;

    void start_assets_tab_underline_anim(AssetsTab tab);

    void start_settings_tab_underline_anim(SettingsTab tab);

    void tick_assets_tab_underline_anim();

    void tick_settings_tab_underline_anim();

    void ensure_server_badge_strip();

    void layout_server_badge_strip();

    void refresh_server_badges();

    void paint_server_badges(HDC hdc, const RECT& rc);

    int hit_server_badge_index(POINT pt) const;

    static void on_server_monitor_changed(void* user_data);



    HWND hwnd_ = nullptr;

    HWND badge_strip_hwnd_ = nullptr;

    HWND nav_hwnd_ = nullptr;

    HWND assets_tab_hwnd_ = nullptr;

    HWND settings_tab_hwnd_ = nullptr;



    void* nav_user_data_ = nullptr;

    NavHandler nav_handler_ = nullptr;

    void* assets_tab_user_data_ = nullptr;

    TabHandler assets_tab_handler_ = nullptr;

    void* settings_tab_user_data_ = nullptr;

    SettingsTabHandler settings_tab_handler_ = nullptr;



    AdminSection section_ = AdminSection::Dashboard;

    AssetsTab assets_tab_ = AssetsTab::General;

    SettingsTab settings_tab_ = SettingsTab::AI;



    COLORREF primary_color_ = RGB(89, 191, 255);

    COLORREF secondary_color_ = RGB(75, 85, 99);

    int tab_margin_left_ = 24;

    int tab_pad_left_ = 8;



    ogg::ui::XmlUiHost dashboard_ui_{};

    ogg::ui::XmlUiHost users_ui_{};

    ogg::ui::XmlUiHost assets_general_ui_{};

    ogg::ui::XmlUiHost assets_login_ui_{};
    ogg::ui::XmlUiHost assets_images_ui_{};
    ogg::ui::XmlUiHost assets_videos_ui_{};

    ogg::ui::XmlUiHost settings_ai_ui_{};

    ogg::ui::XmlUiHost settings_appearance_ui_{};

    ogg::ui::XmlUiHost settings_typography_ui_{};

    ogg::ui::XmlUiHost settings_docs_ui_{};

    ogg::ui::client_shell::ChromeOverlay chrome_{};

    TabUnderlineAnimState assets_tab_underline_{};

    TabUnderlineAnimState settings_tab_underline_{};

    ogg::server_monitor::Monitor server_monitor_;
    bool server_monitor_active_ = false;
    bool server_badge_blink_bright_ = false;
    std::vector<std::wstring> server_badge_labels_;
    std::vector<bool> server_badge_up_;
    std::vector<bool> server_badge_polling_;
    std::vector<bool> server_badge_checking_;
    std::vector<bool> server_badge_local_;

};



} // namespace ogg::admin



#endif


