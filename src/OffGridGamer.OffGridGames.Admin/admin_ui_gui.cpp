#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include <fstream>
#include <string>
#include <vector>

#include "admin_ui.hpp"
#include "admin_window.hpp"
#include "ai_image.hpp"
#include "ai_video.hpp"
#include "app_settings.hpp"
#include "generated_media_assets.hpp"
#include "http_client.hpp"
#include "login_image_assets.hpp"
#include "version.hpp"

namespace ogg::admin {

namespace {

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

COLORREF hex_to_colorref(const std::string& hex) {
    int r = 89, g = 191, b = 255;
    if (hex.size() >= 7 && hex[0] == '#') {
        auto hv = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        r = (hv(hex[1]) << 4) | hv(hex[2]);
        g = (hv(hex[3]) << 4) | hv(hex[4]);
        b = (hv(hex[5]) << 4) | hv(hex[6]);
    }
    return RGB(r, g, b);
}

std::string read_file_utf8(const std::wstring& path) {
    const std::string path_utf8 = wide_to_utf8(path);
    if (path_utf8.empty()) return {};
    std::ifstream file(path_utf8, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::string out(static_cast<size_t>(size), '\0');
    if (!file.read(out.data(), size)) return {};
    return out;
}

std::wstring admin_xml_path(const wchar_t* filename) {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return {};
    wchar_t exe_dir[MAX_PATH]{};
    wcsncpy_s(exe_dir, exe_path, _TRUNCATE);
    for (int i = static_cast<int>(wcslen(exe_dir)) - 1; i >= 0; --i) {
        if (exe_dir[i] == L'\\' || exe_dir[i] == L'/') {
            exe_dir[i + 1] = L'\0';
            break;
        }
    }
    return std::wstring(exe_dir) + filename;
}

std::string escape_xml(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

int parse_int(const std::wstring& text, int fallback) {
    if (text.empty()) return fallback;
    int value = 0;
    bool negative = false;
    size_t i = 0;
    if (text[0] == L'-') {
        negative = true;
        i = 1;
    }
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') return fallback;
        value = value * 10 + (text[i] - L'0');
    }
    return negative ? -value : value;
}

int clamp_font_size(int value, int fallback) {
    if (value < 8 || value > 72) return fallback;
    return value;
}

int clamp_control_width(int value, int fallback) {
    if (value < 80 || value > 480) return fallback;
    return value;
}

int clamp_small_button_width(int value, int fallback) {
    if (value < 48 || value > 240) return fallback;
    return value;
}

struct AdminApp {
    struct GeminiModelCache {
        std::string key;
        std::vector<std::string> models;
        bool valid = false;
    };

    AdminWindow window{};
    ogg::settings::AppSettings settings{};
    std::string selected_image;
    std::string selected_asset_image;
    std::string selected_video;
    GeminiModelCache gemini_cache{};
    GeminiModelCache gemini_video_cache{};
    GeminiModelCache openai_video_cache{};
};

AdminApp* g_app = nullptr;

constexpr UINT kMsgReloadLoginGallery = WM_APP + 1;
constexpr UINT kMsgAiSaveComplete = WM_APP + 2;
constexpr UINT kMsgImageGenerateComplete = WM_APP + 3;
constexpr UINT kMsgReloadAssetImagesGallery = WM_APP + 4;
constexpr UINT kMsgReloadVideosGallery = WM_APP + 5;
constexpr UINT kMsgAssetImageGenerateComplete = WM_APP + 6;
constexpr UINT kMsgVideoGenerateComplete = WM_APP + 7;
constexpr UINT kMsgRefreshDashboardVersion = WM_APP + 8;
constexpr DWORD kVersionRefreshDelayMs = 5000;

struct AiSaveWork {
    HWND notify_hwnd = nullptr;
    ogg::settings::AppSettings settings;
    std::string message;
    bool saved = false;
};

std::string ai_provider_label(const std::string& provider) {
    if (provider == "gemini") return "Gemini";
    return "OpenAI";
}

DWORD WINAPI ai_save_worker(LPVOID param) {
    auto* work = static_cast<AiSaveWork*>(param);
    if (!work) return 0;

    work->message = "Image provider: " + ai_provider_label(work->settings.ai_provider) + "\n";
    work->message += "OpenAI model: " + work->settings.openai_image_model + "\n";
    work->message += "OpenAI quality: " + work->settings.openai_image_quality + "\n";
    work->message += "OpenAI size: " + work->settings.openai_image_size + "\n";
    work->message += "Gemini model: " + work->settings.gemini_image_model + "\n";
    work->message += "OpenAI video model: " + work->settings.openai_video_model + "\n";
    work->message += "Gemini video model: " + work->settings.gemini_video_model + "\n";
    if (!work->settings.ai_system_prompt.empty()) {
        work->message += "System prompt: " + work->settings.ai_system_prompt + "\n";
    }
    work->message += "\nUse Validate OpenAI / Validate Gemini to test keys.\n";

    work->saved = ogg::settings::save(work->settings);
    if (work->saved) {
        work->message += "\nSettings saved.";
    } else {
        work->message += "\nFailed to write settings.";
    }

    if (work->notify_hwnd) {
        PostMessageW(work->notify_hwnd, kMsgAiSaveComplete, 0, reinterpret_cast<LPARAM>(work));
    } else {
        delete work;
    }
    return 0;
}

void refresh_ai_key_status(AdminApp& app) {
    auto& ui = app.window.settings_ai_ui();
    ui.set_text(
        L"openai_key_status",
        app.settings.openai_api_key.empty() ? L"" : L"Configured"
    );
    ui.set_text(
        L"gemini_key_status",
        app.settings.gemini_api_key.empty() ? L"" : L"Configured"
    );
}

void refresh_selected_login_image_label(AdminApp& app) {
    std::string selected;
    ogg::assets::read_selected_image(selected);
    if (!selected.empty()) {
        app.selected_image = selected;
    } else if (!app.selected_image.empty()) {
        selected = app.selected_image;
    }
    const std::wstring label = selected.empty()
        ? L"Selected login image: (none)"
        : (L"Selected login image: " + utf8_to_wide(selected));
    auto& ai_ui = app.window.settings_ai_ui();
    if (ai_ui.hwnd()) {
        ai_ui.set_text(L"selected_login_image", label);
    }
    auto& login_ui = app.window.assets_login_ui();
    if (login_ui.hwnd()) {
        login_ui.set_text(L"selected_login_image", label);
    }
}

void refresh_gemini_model_select(AdminApp& app, bool force_refresh = false);
void refresh_gemini_video_model_select(AdminApp& app, bool force_refresh = false);
void refresh_openai_video_model_select(AdminApp& app, bool force_refresh = false);
void update_api_call_display(AdminApp& app);
void populate_dashboard_builds(AdminApp& app);

std::string trim_version_line(std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    return text.substr(start);
}

std::string read_repo_version_txt(const AdminApp& app) {
    const std::string root = ogg::settings::resolve_source_dev_dir(app.settings);
    if (root.empty()) return {};
    const std::string path_utf8 = ogg::assets::join_path(
        ogg::assets::join_path(root, "src"),
        "version.txt");
    return trim_version_line(read_file_utf8(utf8_to_wide(path_utf8)));
}

void refresh_dashboard_version_label(AdminApp& app) {
    auto& ui = app.window.dashboard_ui();
    if (!ui.hwnd()) return;
    std::string version = read_repo_version_txt(app);
    if (version.empty()) version = ogg::VERSION;
    ui.set_text(L"build_version", L"Version: " + utf8_to_wide(version));
}

DWORD WINAPI version_refresh_worker(LPVOID param) {
    const HWND hwnd = static_cast<HWND>(param);
    Sleep(kVersionRefreshDelayMs);
    if (hwnd) PostMessageW(hwnd, kMsgRefreshDashboardVersion, 0, 0);
    return 0;
}

void schedule_dashboard_version_refresh(HWND hwnd) {
    if (!hwnd) return;
    HANDLE thread = CreateThread(nullptr, 0, version_refresh_worker, hwnd, 0, nullptr);
    if (thread) CloseHandle(thread);
}
bool launch_make_target(AdminApp& app, const wchar_t* target);
void on_settings_tab(AdminApp& app, SettingsTab tab);

std::wstring format_api_call_stats() {
    const int openai = ogg::http_client::api_openai_calls();
    const int gemini = ogg::http_client::api_gemini_calls();
    return L"API calls — OpenAI: " + std::to_wstring(openai) + L", Gemini: " + std::to_wstring(gemini);
}

void update_api_call_display(AdminApp& app) {
    const std::wstring stats = format_api_call_stats();
    auto& ai_ui = app.window.settings_ai_ui();
    if (ai_ui.hwnd()) {
        ai_ui.set_text(L"api_call_stats", stats);
    }
    auto& login_ui = app.window.assets_login_ui();
    if (login_ui.hwnd()) {
        login_ui.set_text(L"api_call_stats", stats);
    }
    auto& images_ui = app.window.assets_images_ui();
    if (images_ui.hwnd()) {
        images_ui.set_text(L"api_call_stats", stats);
    }
    auto& videos_ui = app.window.assets_videos_ui();
    if (videos_ui.hwnd()) {
        videos_ui.set_text(L"api_call_stats", stats);
    }
}

void complete_ai_save(AdminApp& app, AiSaveWork* work) {
    if (!work) return;
    auto& ui = app.window.settings_ai_ui();
    ui.set_busy(false);
    ui.set_button_enabled(L"btn_save_ai", true);
    ui.set_text(L"ai_save_status", L"");

    if (work->saved) {
        const std::string prev_gemini_key = app.settings.gemini_api_key;
        const std::string prev_openai_key = app.settings.openai_api_key;
        app.settings = work->settings;
        if (app.settings.gemini_api_key != prev_gemini_key) {
            app.gemini_cache.valid = false;
            app.gemini_video_cache.valid = false;
        }
        if (app.settings.openai_api_key != prev_openai_key) {
            app.openai_video_cache.valid = false;
        }
        refresh_gemini_model_select(app, app.settings.gemini_api_key != prev_gemini_key);
        refresh_gemini_video_model_select(app, app.settings.gemini_api_key != prev_gemini_key);
        refresh_openai_video_model_select(app, app.settings.openai_api_key != prev_openai_key);
        ui.set_input_text(L"openai_api_key", L"");
        ui.set_input_text(L"gemini_api_key", L"");
        refresh_ai_key_status(app);
    }

    const UINT icon = work->saved ? MB_ICONINFORMATION : MB_ICONERROR;
    MessageBoxW(app.window.hwnd(), utf8_to_wide(work->message).c_str(), L"AI Settings", icon);
    update_api_call_display(app);
    delete work;
}

struct ImageGenerateWork {
    HWND notify_hwnd = nullptr;
    ogg::settings::AppSettings settings;
    std::string prompt;
    bool ok = false;
    std::string message;
    std::string filename;
};

void request_reload_login_gallery(AdminApp& app);

DWORD WINAPI image_generate_worker(LPVOID param) {
    auto* work = static_cast<ImageGenerateWork*>(param);
    if (!work) return 0;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_owned = (com_hr == S_OK);

    const auto generated = ogg::ai_image::generate_login_hero(work->settings, work->prompt);
    if (!generated.ok) {
        work->message = generated.message;
    } else if (!ogg::ai_image::save_generated_jpeg(generated.jpeg_bytes, work->filename)) {
        work->message = "Failed to save generated image.";
    } else {
        work->ok = true;
        work->message = generated.message;
    }

    if (com_owned) CoUninitialize();

    if (work->notify_hwnd) {
        PostMessageW(work->notify_hwnd, kMsgImageGenerateComplete, 0, reinterpret_cast<LPARAM>(work));
    } else {
        delete work;
    }
    return 0;
}

void complete_image_generate(AdminApp& app, ImageGenerateWork* work) {
    if (!work) return;

    if (!work->ok) {
        auto& ui = app.window.assets_login_ui();
        if (ui.hwnd()) {
            ui.set_busy(false);
            ui.set_button_enabled(L"btn_generate_image", true);
        }
        MessageBoxW(app.window.hwnd(), utf8_to_wide(work->message).c_str(), L"OGG Admin", MB_ICONERROR);
        update_api_call_display(app);
        delete work;
        return;
    }

    app.selected_image = work->filename;
    app.settings.last_image_prompt = work->prompt;
    delete work;
    request_reload_login_gallery(app);
}

void request_reload_login_gallery(AdminApp& app) {
    PostMessageW(app.window.hwnd(), kMsgReloadLoginGallery, 0, 0);
}

void request_reload_asset_images_gallery(AdminApp& app) {
    PostMessageW(app.window.hwnd(), kMsgReloadAssetImagesGallery, 0, 0);
}

void request_reload_videos_gallery(AdminApp& app) {
    PostMessageW(app.window.hwnd(), kMsgReloadVideosGallery, 0, 0);
}

struct AssetImageGenerateWork {
    HWND notify_hwnd = nullptr;
    ogg::settings::AppSettings settings;
    std::string prompt;
    bool ok = false;
    std::string message;
    std::string filename;
};

DWORD WINAPI asset_image_generate_worker(LPVOID param) {
    auto* work = static_cast<AssetImageGenerateWork*>(param);
    if (!work) return 0;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_owned = (com_hr == S_OK);

    const auto generated = ogg::ai_image::generate_asset_image(work->settings, work->prompt);
    if (!generated.ok) {
        work->message = generated.message;
    } else if (!ogg::ai_image::save_generated_asset_image(generated.jpeg_bytes, work->filename)) {
        work->message = "Failed to save generated image.";
    } else {
        work->ok = true;
        work->message = generated.message;
    }

    if (com_owned) CoUninitialize();

    if (work->notify_hwnd) {
        PostMessageW(work->notify_hwnd, kMsgAssetImageGenerateComplete, 0, reinterpret_cast<LPARAM>(work));
    } else {
        delete work;
    }
    return 0;
}

void complete_asset_image_generate(AdminApp& app, AssetImageGenerateWork* work) {
    if (!work) return;

    if (!work->ok) {
        auto& ui = app.window.assets_images_ui();
        if (ui.hwnd()) {
            ui.set_busy(false);
            ui.set_button_enabled(L"btn_generate_asset_image", true);
        }
        MessageBoxW(app.window.hwnd(), utf8_to_wide(work->message).c_str(), L"OGG Admin", MB_ICONERROR);
        update_api_call_display(app);
        delete work;
        return;
    }

    app.selected_asset_image = work->filename;
    app.settings.last_asset_image_prompt = work->prompt;
    delete work;
    request_reload_asset_images_gallery(app);
}

struct VideoGenerateWork {
    HWND notify_hwnd = nullptr;
    ogg::settings::AppSettings settings;
    std::string prompt;
    bool ok = false;
    std::string message;
    std::string filename;
};

DWORD WINAPI video_generate_worker(LPVOID param) {
    auto* work = static_cast<VideoGenerateWork*>(param);
    if (!work) return 0;

    const auto generated = ogg::ai_video::generate_asset_video(work->settings, work->prompt);
    if (!generated.ok) {
        work->message = generated.message;
    } else if (!ogg::ai_video::save_generated_video(generated.mp4_bytes, work->filename)) {
        work->message = "Failed to save generated video.";
    } else {
        work->ok = true;
        work->message = generated.message;
    }

    if (work->notify_hwnd) {
        PostMessageW(work->notify_hwnd, kMsgVideoGenerateComplete, 0, reinterpret_cast<LPARAM>(work));
    } else {
        delete work;
    }
    return 0;
}

void complete_video_generate(AdminApp& app, VideoGenerateWork* work) {
    if (!work) return;

    if (!work->ok) {
        auto& ui = app.window.assets_videos_ui();
        if (ui.hwnd()) {
            ui.set_busy(false);
            ui.set_button_enabled(L"btn_generate_video", true);
        }
        MessageBoxW(app.window.hwnd(), utf8_to_wide(work->message).c_str(), L"OGG Admin", MB_ICONERROR);
        update_api_call_display(app);
        delete work;
        return;
    }

    app.selected_video = work->filename;
    app.settings.last_video_prompt = work->prompt;
    delete work;
    request_reload_videos_gallery(app);
}

void apply_theme(AdminApp& app) {
    app.window.set_theme_colors(
        hex_to_colorref(app.settings.primary_color),
        hex_to_colorref(app.settings.secondary_color)
    );
}

void apply_tab_layout(AdminApp& app) {
    const int margin = app.settings.tab_margin_left >= 0 ? app.settings.tab_margin_left : 24;
    const int pad = app.settings.tab_pad_left >= 0 ? app.settings.tab_pad_left : 8;
    app.window.set_tab_layout(margin, pad);
}

ogg::ui::InputInsets insets_from_settings(const ogg::settings::AppSettings& settings) {
    ogg::ui::InputInsets insets{};
    insets.left = settings.input_inset_left;
    insets.right = settings.input_inset_right;
    insets.top = settings.input_inset_top;
    insets.bottom = settings.input_inset_bottom;
    return insets;
}

void apply_input_insets(AdminApp& app) {
    const auto insets = insets_from_settings(app.settings);
    app.window.dashboard_ui().set_input_insets(insets);
    app.window.users_ui().set_input_insets(insets);
    app.window.assets_general_ui().set_input_insets(insets);
    app.window.assets_login_ui().set_input_insets(insets);
    app.window.assets_images_ui().set_input_insets(insets);
    app.window.assets_videos_ui().set_input_insets(insets);
    app.window.settings_ai_ui().set_input_insets(insets);
    app.window.settings_appearance_ui().set_input_insets(insets);
    app.window.settings_typography_ui().set_input_insets(insets);
    app.window.settings_docs_ui().set_input_insets(insets);
}

void apply_control_width(AdminApp& app) {
    const float width = static_cast<float>(app.settings.ui_control_width > 0 ? app.settings.ui_control_width : 320);
    app.window.dashboard_ui().set_control_width(width);
    app.window.users_ui().set_control_width(width);
    app.window.assets_general_ui().set_control_width(width);
    app.window.assets_login_ui().set_control_width(width);
    app.window.assets_images_ui().set_control_width(width);
    app.window.assets_videos_ui().set_control_width(width);
    app.window.settings_ai_ui().set_control_width(width);
    app.window.settings_appearance_ui().set_control_width(width);
    app.window.settings_typography_ui().set_control_width(width);
    app.window.settings_docs_ui().set_control_width(width);
}

void apply_label_control_gap(AdminApp& app) {
    const float gap = static_cast<float>(app.settings.label_control_gap >= 0 ? app.settings.label_control_gap : 6);
    app.window.dashboard_ui().set_label_control_gap(gap);
    app.window.users_ui().set_label_control_gap(gap);
    app.window.assets_general_ui().set_label_control_gap(gap);
    app.window.assets_login_ui().set_label_control_gap(gap);
    app.window.assets_images_ui().set_label_control_gap(gap);
    app.window.assets_videos_ui().set_label_control_gap(gap);
    app.window.settings_ai_ui().set_label_control_gap(gap);
    app.window.settings_appearance_ui().set_label_control_gap(gap);
    app.window.settings_typography_ui().set_label_control_gap(gap);
    app.window.settings_docs_ui().set_label_control_gap(gap);
}

void apply_scroll_wheel_step(AdminApp& app) {
    const float step = static_cast<float>(app.settings.ui_scroll_step > 0 ? app.settings.ui_scroll_step : 25);
    app.window.dashboard_ui().set_scroll_wheel_step(step);
    app.window.users_ui().set_scroll_wheel_step(step);
    app.window.assets_general_ui().set_scroll_wheel_step(step);
    app.window.assets_login_ui().set_scroll_wheel_step(step);
    app.window.assets_images_ui().set_scroll_wheel_step(step);
    app.window.assets_videos_ui().set_scroll_wheel_step(step);
    app.window.settings_ai_ui().set_scroll_wheel_step(step);
    app.window.settings_appearance_ui().set_scroll_wheel_step(step);
    app.window.settings_typography_ui().set_scroll_wheel_step(step);
    app.window.settings_docs_ui().set_scroll_wheel_step(step);
}

void apply_button_widths(AdminApp& app) {
    const float button_w = static_cast<float>(app.settings.ui_button_width > 0 ? app.settings.ui_button_width : 140);
    const float small_w = static_cast<float>(app.settings.ui_small_button_width > 0 ? app.settings.ui_small_button_width : 80);
    app.window.dashboard_ui().set_button_width(button_w);
    app.window.users_ui().set_button_width(button_w);
    app.window.assets_general_ui().set_button_width(button_w);
    app.window.assets_login_ui().set_button_width(button_w);
    app.window.assets_images_ui().set_button_width(button_w);
    app.window.assets_videos_ui().set_button_width(button_w);
    app.window.settings_ai_ui().set_button_width(button_w);
    app.window.settings_appearance_ui().set_button_width(button_w);
    app.window.settings_typography_ui().set_button_width(button_w);
    app.window.settings_docs_ui().set_button_width(button_w);
    app.window.dashboard_ui().set_small_button_width(small_w);
    app.window.users_ui().set_small_button_width(small_w);
    app.window.assets_general_ui().set_small_button_width(small_w);
    app.window.assets_login_ui().set_small_button_width(small_w);
    app.window.assets_images_ui().set_small_button_width(small_w);
    app.window.assets_videos_ui().set_small_button_width(small_w);
    app.window.settings_ai_ui().set_small_button_width(small_w);
    app.window.settings_appearance_ui().set_small_button_width(small_w);
    app.window.settings_typography_ui().set_small_button_width(small_w);
    app.window.settings_docs_ui().set_small_button_width(small_w);
}

ogg::ui::UiTypography typography_from_settings(const ogg::settings::AppSettings& settings) {
    ogg::ui::UiTypography typography{};
    typography.font_family = utf8_to_wide(
        settings.ui_font_family.empty() ? "Segoe UI" : settings.ui_font_family
    );
    typography.label_font_size = static_cast<float>(settings.label_font_size > 0 ? settings.label_font_size : 12);
    typography.input_font_size = static_cast<float>(settings.input_font_size > 0 ? settings.input_font_size : 14);
    typography.button_font_size = static_cast<float>(settings.button_font_size > 0 ? settings.button_font_size : 13);
    return typography;
}

void apply_typography(AdminApp& app) {
    const auto typography = typography_from_settings(app.settings);
    app.window.dashboard_ui().set_typography(typography);
    app.window.users_ui().set_typography(typography);
    app.window.assets_general_ui().set_typography(typography);
    app.window.assets_login_ui().set_typography(typography);
    app.window.assets_images_ui().set_typography(typography);
    app.window.assets_videos_ui().set_typography(typography);
    app.window.settings_ai_ui().set_typography(typography);
    app.window.settings_appearance_ui().set_typography(typography);
    app.window.settings_typography_ui().set_typography(typography);
    app.window.settings_docs_ui().set_typography(typography);
}

void relayout_appearance_preview(AdminApp& app) {
    app.window.settings_appearance_ui().set_input_insets(insets_from_settings(app.settings));
    app.window.settings_appearance_ui().refresh_input_insets();
}

void refresh_provider_radio(AdminApp& app) {
    auto& ui = app.window.settings_ai_ui();
    const std::wstring provider = app.settings.ai_provider == "gemini" ? L"gemini" : L"openai";
    ui.set_radio_value(L"ai_provider", provider);
}

void apply_gemini_models_to_select(AdminApp& app, const std::vector<std::string>& models) {
    auto& ui = app.window.settings_ai_ui();
    std::vector<std::wstring> options;
    for (const auto& model : models) {
        options.push_back(utf8_to_wide(model));
    }
    if (options.empty()) {
        options.push_back(L"gemini-2.5-flash-image");
    }

    std::wstring selected = utf8_to_wide(
        app.settings.gemini_image_model.empty() ? "gemini-2.5-flash-image" : app.settings.gemini_image_model
    );
    bool found = false;
    for (const auto& option : options) {
        if (_wcsicmp(option.c_str(), selected.c_str()) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        selected = options.front();
        app.settings.gemini_image_model = wide_to_utf8(selected);
    }
    ui.set_select_options(L"gemini_image_model", options, selected);
}

void refresh_gemini_model_select(AdminApp& app, bool force_refresh) {
    const std::string& key = app.settings.gemini_api_key;
    if (key.empty()) {
        apply_gemini_models_to_select(app, {});
        return;
    }

    if (!force_refresh && app.gemini_cache.valid && app.gemini_cache.key == key) {
        apply_gemini_models_to_select(app, app.gemini_cache.models);
        return;
    }

    const auto models = ogg::ai_image::list_gemini_image_models(key);
    if (models.empty()) {
        app.gemini_cache.valid = false;
        apply_gemini_models_to_select(app, {});
        return;
    }

    app.gemini_cache.key = key;
    app.gemini_cache.models = models;
    app.gemini_cache.valid = true;
    apply_gemini_models_to_select(app, models);
}

void apply_gemini_video_models_to_select(AdminApp& app, const std::vector<std::string>& models) {
    auto& ui = app.window.settings_ai_ui();
    std::vector<std::wstring> options;
    for (const auto& model : models) {
        options.push_back(utf8_to_wide(model));
    }
    if (options.empty()) {
        options.push_back(L"veo-3.1-fast-generate-preview");
    }

    std::wstring selected = utf8_to_wide(
        app.settings.gemini_video_model.empty() ? "veo-3.1-fast-generate-preview" : app.settings.gemini_video_model
    );
    bool found = false;
    for (const auto& option : options) {
        if (_wcsicmp(option.c_str(), selected.c_str()) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        selected = options.front();
        app.settings.gemini_video_model = wide_to_utf8(selected);
    }
    ui.set_select_options(L"gemini_video_model", options, selected);
}

void apply_openai_video_models_to_select(AdminApp& app, const std::vector<std::string>& models) {
    auto& ui = app.window.settings_ai_ui();
    std::vector<std::wstring> options;
    for (const auto& model : models) {
        options.push_back(utf8_to_wide(model));
    }
    if (options.empty()) {
        options.push_back(L"sora-2");
        options.push_back(L"sora-2-pro");
    }

    std::wstring selected = utf8_to_wide(
        app.settings.openai_video_model.empty() ? "sora-2" : app.settings.openai_video_model
    );
    bool found = false;
    for (const auto& option : options) {
        if (_wcsicmp(option.c_str(), selected.c_str()) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        selected = options.front();
        app.settings.openai_video_model = wide_to_utf8(selected);
    }
    ui.set_select_options(L"openai_video_model", options, selected);
}

void refresh_gemini_video_model_select(AdminApp& app, bool force_refresh) {
    const std::string& key = app.settings.gemini_api_key;
    if (key.empty()) {
        apply_gemini_video_models_to_select(app, {});
        return;
    }

    if (!force_refresh && app.gemini_video_cache.valid && app.gemini_video_cache.key == key) {
        apply_gemini_video_models_to_select(app, app.gemini_video_cache.models);
        return;
    }

    const auto models = ogg::ai_video::list_gemini_video_models(key);
    if (models.empty()) {
        app.gemini_video_cache.valid = false;
        apply_gemini_video_models_to_select(app, {});
        return;
    }

    app.gemini_video_cache.key = key;
    app.gemini_video_cache.models = models;
    app.gemini_video_cache.valid = true;
    apply_gemini_video_models_to_select(app, models);
}

void refresh_openai_video_model_select(AdminApp& app, bool force_refresh) {
    const std::string& key = app.settings.openai_api_key;
    if (key.empty()) {
        apply_openai_video_models_to_select(app, {});
        return;
    }

    if (!force_refresh && app.openai_video_cache.valid && app.openai_video_cache.key == key) {
        apply_openai_video_models_to_select(app, app.openai_video_cache.models);
        return;
    }

    const auto models = ogg::ai_video::list_openai_video_models(key);
    if (models.empty()) {
        app.openai_video_cache.valid = false;
        apply_openai_video_models_to_select(app, {});
        return;
    }

    app.openai_video_cache.key = key;
    app.openai_video_cache.models = models;
    app.openai_video_cache.valid = true;
    apply_openai_video_models_to_select(app, models);
}

void populate_settings_ai(AdminApp& app) {
    auto& ui = app.window.settings_ai_ui();
    ui.set_input_text(L"openai_api_key", L"");
    ui.set_input_text(L"gemini_api_key", L"");
    ui.set_select_value(L"openai_image_model", utf8_to_wide(
        app.settings.openai_image_model.empty() ? "gpt-image-2" : app.settings.openai_image_model
    ));
    ui.set_select_value(L"openai_image_quality", utf8_to_wide(
        app.settings.openai_image_quality.empty() ? "medium" : app.settings.openai_image_quality
    ));
    ui.set_select_value(L"openai_image_size", utf8_to_wide(
        app.settings.openai_image_size.empty() ? "1536x1024" : app.settings.openai_image_size
    ));
    refresh_gemini_model_select(app, false);
    refresh_gemini_video_model_select(app, false);
    refresh_openai_video_model_select(app, false);
    ui.set_select_value(L"openai_video_size", utf8_to_wide(
        app.settings.openai_video_size.empty() ? "1280x720" : app.settings.openai_video_size
    ));
    ui.set_select_value(L"openai_video_seconds", utf8_to_wide(
        app.settings.openai_video_seconds.empty() ? "8" : app.settings.openai_video_seconds
    ));
    ui.set_input_text(L"ai_system_prompt", utf8_to_wide(app.settings.ai_system_prompt));
    refresh_provider_radio(app);
    refresh_ai_key_status(app);
    refresh_selected_login_image_label(app);
    update_api_call_display(app);
}

void populate_settings_appearance(AdminApp& app) {
    auto& ui = app.window.settings_appearance_ui();
    ui.set_input_text(L"primary_color", utf8_to_wide(app.settings.primary_color));
    ui.set_input_text(L"secondary_color", utf8_to_wide(app.settings.secondary_color));
    const std::string source_dir = ogg::settings::resolve_source_dev_dir(app.settings);
    ui.set_input_text(L"source_dev_dir", utf8_to_wide(
        app.settings.source_dev_dir.empty() ? source_dir : app.settings.source_dev_dir
    ));
    ui.set_range_value(L"ui_control_width", app.settings.ui_control_width);
    ui.set_range_value(L"ui_button_width", app.settings.ui_button_width);
    ui.set_range_value(L"ui_small_button_width", app.settings.ui_small_button_width);
    ui.set_range_value(L"tab_margin_left", app.settings.tab_margin_left >= 0 ? app.settings.tab_margin_left : 24);
    ui.set_range_value(L"tab_pad_left", app.settings.tab_pad_left >= 0 ? app.settings.tab_pad_left : 8);
    ui.set_range_value(L"label_control_gap", app.settings.label_control_gap >= 0 ? app.settings.label_control_gap : 6);
    ui.set_range_value(L"ui_scroll_step", app.settings.ui_scroll_step > 0 ? app.settings.ui_scroll_step : 25);
    ui.set_range_value(L"inset_left", app.settings.input_inset_left);
    ui.set_range_value(L"inset_right", app.settings.input_inset_right);
    ui.set_range_value(L"inset_top", app.settings.input_inset_top);
    ui.set_range_value(L"inset_bottom", app.settings.input_inset_bottom);
    apply_input_insets(app);
    apply_control_width(app);
    apply_button_widths(app);
    apply_tab_layout(app);
    apply_label_control_gap(app);
    apply_scroll_wheel_step(app);
}

void populate_settings_typography(AdminApp& app) {
    auto& ui = app.window.settings_typography_ui();
    ui.set_select_value(L"ui_font", utf8_to_wide(
        app.settings.ui_font_family.empty() ? "Segoe UI" : app.settings.ui_font_family
    ));
    ui.set_range_value(L"label_font_size", app.settings.label_font_size);
    ui.set_range_value(L"input_font_size", app.settings.input_font_size);
    ui.set_range_value(L"button_font_size", app.settings.button_font_size);
    apply_typography(app);
}

bool read_settings_ai_from_ui(AdminApp& app) {
    auto& ui = app.window.settings_ai_ui();
    const std::string openai_key = http_client::trim(wide_to_utf8(ui.get_input_text(L"openai_api_key")));
    const std::string gemini_key = http_client::trim(wide_to_utf8(ui.get_input_text(L"gemini_api_key")));
    const std::string prev_openai_key = app.settings.openai_api_key;
    const std::string prev_gemini_key = app.settings.gemini_api_key;
    if (!openai_key.empty() && openai_key != prev_openai_key) {
        app.openai_video_cache.valid = false;
    }
    if (!openai_key.empty()) app.settings.openai_api_key = openai_key;
    if (!gemini_key.empty()) {
        if (gemini_key != prev_gemini_key) {
            app.gemini_cache.valid = false;
            app.gemini_video_cache.valid = false;
        }
        app.settings.gemini_api_key = gemini_key;
    }
    const std::string openai_model = wide_to_utf8(ui.get_select_value(L"openai_image_model"));
    const std::string openai_quality = wide_to_utf8(ui.get_select_value(L"openai_image_quality"));
    const std::string openai_size = wide_to_utf8(ui.get_select_value(L"openai_image_size"));
    const std::string gemini_model = wide_to_utf8(ui.get_select_value(L"gemini_image_model"));
    const std::string openai_video_model = wide_to_utf8(ui.get_select_value(L"openai_video_model"));
    const std::string openai_video_size = wide_to_utf8(ui.get_select_value(L"openai_video_size"));
    const std::string openai_video_seconds = wide_to_utf8(ui.get_select_value(L"openai_video_seconds"));
    const std::string gemini_video_model = wide_to_utf8(ui.get_select_value(L"gemini_video_model"));
    if (!openai_model.empty()) app.settings.openai_image_model = openai_model;
    if (!openai_quality.empty()) app.settings.openai_image_quality = openai_quality;
    if (!openai_size.empty()) app.settings.openai_image_size = openai_size;
    if (!gemini_model.empty()) app.settings.gemini_image_model = gemini_model;
    if (!openai_video_model.empty()) app.settings.openai_video_model = openai_video_model;
    if (!openai_video_size.empty()) app.settings.openai_video_size = openai_video_size;
    if (!openai_video_seconds.empty()) app.settings.openai_video_seconds = openai_video_seconds;
    if (!gemini_video_model.empty()) app.settings.gemini_video_model = gemini_video_model;
    app.settings.ai_system_prompt = wide_to_utf8(ui.get_input_text(L"ai_system_prompt"));
    const std::wstring provider = ui.get_radio_value(L"ai_provider");
    if (!provider.empty()) app.settings.ai_provider = wide_to_utf8(provider);
    if (app.settings.ai_provider.empty()) app.settings.ai_provider = "openai";
    if (app.settings.openai_image_model.empty()) app.settings.openai_image_model = "gpt-image-2";
    if (app.settings.openai_image_quality.empty()) app.settings.openai_image_quality = "medium";
    if (app.settings.openai_image_size.empty()) app.settings.openai_image_size = "1536x1024";
    if (app.settings.gemini_image_model.empty()) app.settings.gemini_image_model = "gemini-2.5-flash-image";
    if (app.settings.openai_video_model.empty()) app.settings.openai_video_model = "sora-2";
    if (app.settings.openai_video_size.empty()) app.settings.openai_video_size = "1280x720";
    if (app.settings.openai_video_seconds.empty()) app.settings.openai_video_seconds = "8";
    if (app.settings.gemini_video_model.empty()) app.settings.gemini_video_model = "veo-3.1-fast-generate-preview";
    return true;
}

bool read_settings_appearance_from_ui(AdminApp& app) {
    auto& ui = app.window.settings_appearance_ui();
    app.settings.primary_color = wide_to_utf8(ui.get_input_text(L"primary_color"));
    app.settings.secondary_color = wide_to_utf8(ui.get_input_text(L"secondary_color"));
    app.settings.ui_control_width = clamp_control_width(
        ui.get_range_value(L"ui_control_width"),
        app.settings.ui_control_width > 0 ? app.settings.ui_control_width : 320
    );
    app.settings.ui_button_width = clamp_control_width(
        ui.get_range_value(L"ui_button_width"),
        app.settings.ui_button_width > 0 ? app.settings.ui_button_width : 140
    );
    app.settings.ui_small_button_width = clamp_small_button_width(
        ui.get_range_value(L"ui_small_button_width"),
        app.settings.ui_small_button_width > 0 ? app.settings.ui_small_button_width : 80
    );
    app.settings.tab_margin_left = ui.get_range_value(L"tab_margin_left");
    if (app.settings.tab_margin_left < 0) app.settings.tab_margin_left = 0;
    if (app.settings.tab_margin_left > 64) app.settings.tab_margin_left = 64;
    app.settings.tab_pad_left = ui.get_range_value(L"tab_pad_left");
    if (app.settings.tab_pad_left < 0) app.settings.tab_pad_left = 0;
    if (app.settings.tab_pad_left > 32) app.settings.tab_pad_left = 32;
    app.settings.label_control_gap = ui.get_range_value(L"label_control_gap");
    if (app.settings.label_control_gap < 0) app.settings.label_control_gap = 0;
    if (app.settings.label_control_gap > 32) app.settings.label_control_gap = 32;
    app.settings.ui_scroll_step = ui.get_range_value(L"ui_scroll_step");
    if (app.settings.ui_scroll_step < 1) app.settings.ui_scroll_step = 1;
    if (app.settings.ui_scroll_step > 32) app.settings.ui_scroll_step = 32;
    app.settings.input_inset_left = ui.get_range_value(L"inset_left");
    app.settings.input_inset_right = ui.get_range_value(L"inset_right");
    app.settings.input_inset_top = ui.get_range_value(L"inset_top");
    app.settings.input_inset_bottom = ui.get_range_value(L"inset_bottom");
    app.settings.source_dev_dir = http_client::trim(wide_to_utf8(ui.get_input_text(L"source_dev_dir")));
    if (app.settings.primary_color.empty()) app.settings.primary_color = "#59bfff";
    if (app.settings.secondary_color.empty()) app.settings.secondary_color = "#4b5563";
    if (app.settings.ui_control_width < 80) app.settings.ui_control_width = 320;
    return true;
}

bool read_settings_typography_from_ui(AdminApp& app) {
    auto& ui = app.window.settings_typography_ui();
    const std::wstring font = ui.get_select_value(L"ui_font");
    if (!font.empty()) app.settings.ui_font_family = wide_to_utf8(font);
    app.settings.label_font_size = clamp_font_size(
        ui.get_range_value(L"label_font_size"),
        app.settings.label_font_size > 0 ? app.settings.label_font_size : 12
    );
    app.settings.input_font_size = clamp_font_size(
        ui.get_range_value(L"input_font_size"),
        app.settings.input_font_size > 0 ? app.settings.input_font_size : 14
    );
    app.settings.button_font_size = clamp_font_size(
        ui.get_range_value(L"button_font_size"),
        app.settings.button_font_size > 0 ? app.settings.button_font_size : 13
    );
    if (app.settings.ui_font_family.empty()) app.settings.ui_font_family = "Segoe UI";
    return true;
}

std::string build_login_gallery_xml(const AdminApp& app) {
    (void)app;
    std::string selected;
    ogg::assets::read_selected_image(selected);

    std::string xml;
    xml.append(R"(<Layout width="960">
  <Class rule="page" padding="24,24" bg_color="#f4f6f8" />
  <Class rule="title" color="#111827" font_size="22" font_weight="600" margin="0,0,8,0" />
  <Class rule="body" color="#4b5563" font_size="14" margin="0,0,12,0" />
  <Class rule="field-label" color="#111827" font_size="12" font_weight="600" margin="0,0,0,0" />
  <Class rule="input" height="36" padding="0,12" border_color="#c8c8c8" bg_color="#ffffff" font_size="14" focus_border_color="#59bfff" radius="6" />
  <Class rule="textarea" border_color="#c8c8c8" bg_color="#ffffff" font_size="14" focus_border_color="#59bfff" radius="6" />
  <Class rule="btn-primary" height="38" width="220" margin="0,0,16,0" bg_color="#59bfff" hover_bg_color="#4aaef0" color="#0d1117" hover_color="#0d1117" font_size="14" font_weight="600" radius="6" />
  <Class rule="thumb" width="220" height="140" margin="0,0,16,16" radius="8" />
  <Div class="page">
    <Text class="title">Login Images</Text>
    <Text class="body">Gallery: %LOCALAPPDATA%\OffGridGames\login_images — selection syncs to hero_art.jpg on make client.</Text>
    <Text id="api_call_stats" class="body" />
    <Text id="selected_login_image" class="body" />
    <Label class="field-label">Image prompt (reuses last prompt)</Label>
    <TextArea id="image_prompt" class="textarea" rows="4" placeholder="Misty mountains at dawn..." />
    <Button id="btn_generate_image" class="btn-primary">Generate image with AI</Button>
    <Gallery columns="3">)");

    const auto images = ogg::assets::list_login_images();
    if (images.empty()) {
        xml.append(R"(
      <Text class="body">No images yet. Enter a prompt above and click Generate image with AI.</Text>)");
    }
    for (const auto& entry : images) {
        const bool is_selected = !selected.empty() && entry.filename == selected;
        xml.append("\n      <Image id=\"");
        xml.append(escape_xml(entry.filename));
        xml.append("\" class=\"thumb\" src=\"");
        xml.append(escape_xml(entry.absolute_path_utf8));
        xml.append("\" selected=\"");
        xml.append(is_selected ? "true" : "false");
        xml.append("\" />");
    }

    xml.append(R"(
    </Gallery>
  </Div>
</Layout>)");
    return xml;
}

std::string build_asset_images_gallery_xml(const AdminApp& app) {
  const std::string& selected = app.selected_asset_image;

    std::string xml;
    xml.append(R"(<Layout width="960">
  <Class rule="page" padding="24,24" bg_color="#f4f6f8" />
  <Class rule="title" color="#111827" font_size="22" font_weight="600" margin="0,0,8,0" />
  <Class rule="body" color="#4b5563" font_size="14" margin="0,0,12,0" />
  <Class rule="field-label" color="#111827" font_size="12" font_weight="600" margin="0,0,0,0" />
  <Class rule="textarea" border_color="#c8c8c8" bg_color="#ffffff" font_size="14" focus_border_color="#59bfff" radius="6" />
  <Class rule="btn-primary" height="38" width="220" margin="0,0,16,0" bg_color="#59bfff" hover_bg_color="#4aaef0" color="#0d1117" hover_color="#0d1117" font_size="14" font_weight="600" radius="6" />
  <Class rule="thumb" width="220" height="140" margin="0,0,16,16" radius="8" />
  <Div class="page">
    <Text class="title">Images</Text>
    <Text class="body">Gallery: %LOCALAPPDATA%\OffGridGames\generated_images — full-resolution AI images (not login hero crop).</Text>
    <Text id="api_call_stats" class="body" />
    <Text id="selected_asset_image" class="body" />
    <Label class="field-label">Image prompt</Label>
    <TextArea id="asset_image_prompt" class="textarea" rows="4" placeholder="Cinematic cityscape at dusk..." />
    <Button id="btn_generate_asset_image" class="btn-primary">Generate image with AI</Button>
    <Gallery columns="3">)");

    const auto images = ogg::assets::list_generated_images();
    if (images.empty()) {
        xml.append(R"(
      <Text class="body">No images yet. Enter a prompt and click Generate image with AI.</Text>)");
    }
    for (const auto& entry : images) {
        const bool is_selected = !selected.empty() && entry.filename == selected;
        xml.append("\n      <Image id=\"");
        xml.append(escape_xml(entry.filename));
        xml.append("\" class=\"thumb\" src=\"");
        xml.append(escape_xml(entry.absolute_path_utf8));
        xml.append("\" selected=\"");
        xml.append(is_selected ? "true" : "false");
        xml.append("\" />");
    }

    xml.append(R"(
    </Gallery>
  </Div>
</Layout>)");
    return xml;
}

std::string build_asset_videos_gallery_xml(const AdminApp& app) {
    const std::string& selected = app.selected_video;

    std::string xml;
    xml.append(R"(<Layout width="960">
  <Class rule="page" padding="24,24" bg_color="#f4f6f8" />
  <Class rule="title" color="#111827" font_size="22" font_weight="600" margin="0,0,8,0" />
  <Class rule="body" color="#4b5563" font_size="14" margin="0,0,12,0" />
  <Class rule="field-label" color="#111827" font_size="12" font_weight="600" margin="0,0,0,0" />
  <Class rule="textarea" border_color="#c8c8c8" bg_color="#ffffff" font_size="14" focus_border_color="#59bfff" radius="6" />
  <Class rule="btn-primary" height="38" width="220" margin="0,0,16,0" bg_color="#59bfff" hover_bg_color="#4aaef0" color="#0d1117" hover_color="#0d1117" font_size="14" font_weight="600" radius="6" />
  <Class rule="thumb" width="220" height="140" margin="0,0,16,16" radius="8" />
  <Div class="page">
    <Text class="title">Videos</Text>
    <Text class="body">Gallery: %LOCALAPPDATA%\OffGridGames\generated_videos — OpenAI Sora or Gemini Veo (async jobs, may take minutes).</Text>
    <Text id="api_call_stats" class="body" />
    <Text id="selected_video" class="body" />
    <Label class="field-label">Video prompt</Label>
    <TextArea id="video_prompt" class="textarea" rows="4" placeholder="Slow dolly shot through a misty forest..." />
    <Button id="btn_generate_video" class="btn-primary">Generate video with AI</Button>
    <Gallery columns="3">)");

    const auto videos = ogg::assets::list_generated_videos();
    if (videos.empty()) {
        xml.append(R"(
      <Text class="body">No videos yet. Enter a prompt and click Generate video with AI.</Text>)");
    }
    for (const auto& entry : videos) {
        const bool is_selected = !selected.empty() && entry.filename == selected;
        xml.append("\n      <Image id=\"");
        xml.append(escape_xml(entry.filename));
        xml.append("\" class=\"thumb\" text=\"");
        xml.append(escape_xml(entry.filename));
        xml.append("\" selected=\"");
        xml.append(is_selected ? "true" : "false");
        xml.append("\" />");
    }

    xml.append(R"(
    </Gallery>
  </Div>
</Layout>)");
    return xml;
}

void reload_login_gallery(AdminApp& app);

void on_login_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app || id != L"btn_generate_image") return;
    AdminApp& app = *g_app;
    auto& ui = app.window.assets_login_ui();
    if (ui.is_busy()) return;

    ogg::settings::load(app.settings);

    const std::string prompt = wide_to_utf8(ui.get_input_text(L"image_prompt"));
    if (prompt.empty()) {
        MessageBoxW(app.window.hwnd(), L"Enter an image prompt.", L"OGG Admin", MB_ICONINFORMATION);
        return;
    }

    app.settings.last_image_prompt = prompt;
    ogg::assets::write_last_image_prompt(prompt);

    auto* work = new ImageGenerateWork{};
    work->notify_hwnd = app.window.hwnd();
    work->settings = app.settings;
    work->prompt = prompt;

    ui.set_busy(true, L"btn_generate_image");
    ui.set_button_enabled(L"btn_generate_image", false);

    HANDLE thread = CreateThread(nullptr, 0, image_generate_worker, work, 0, nullptr);
    if (!thread) {
        ui.set_busy(false);
        ui.set_button_enabled(L"btn_generate_image", true);
        delete work;
        MessageBoxW(app.window.hwnd(), L"Could not start image generation.", L"OGG Admin", MB_ICONERROR);
    } else {
        CloseHandle(thread);
    }
}

void on_login_image(void* /*ctx*/, const std::wstring& id) {
    if (!g_app) return;
    AdminApp& app = *g_app;
    const std::string filename = wide_to_utf8(id);
    ogg::assets::write_selected_image(filename);
    ogg::assets::sync_hero_art_from_selection();
    app.selected_image = filename;
    app.window.assets_login_ui().set_image_selected(id);
    refresh_selected_login_image_label(app);
}

void on_asset_images_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app || id != L"btn_generate_asset_image") return;
    AdminApp& app = *g_app;
    auto& ui = app.window.assets_images_ui();
    if (ui.is_busy()) return;

    ogg::settings::load(app.settings);
    read_settings_ai_from_ui(app);

    const std::string prompt = wide_to_utf8(ui.get_input_text(L"asset_image_prompt"));
    if (prompt.empty()) {
        MessageBoxW(app.window.hwnd(), L"Enter an image prompt.", L"OGG Admin", MB_ICONINFORMATION);
        return;
    }

    app.settings.last_asset_image_prompt = prompt;
    ogg::assets::write_last_asset_image_prompt(prompt);

    auto* work = new AssetImageGenerateWork{};
    work->notify_hwnd = app.window.hwnd();
    work->settings = app.settings;
    work->prompt = prompt;

    ui.set_busy(true, L"btn_generate_asset_image");
    ui.set_button_enabled(L"btn_generate_asset_image", false);

    HANDLE thread = CreateThread(nullptr, 0, asset_image_generate_worker, work, 0, nullptr);
    if (!thread) {
        ui.set_busy(false);
        ui.set_button_enabled(L"btn_generate_asset_image", true);
        delete work;
        MessageBoxW(app.window.hwnd(), L"Could not start image generation.", L"OGG Admin", MB_ICONERROR);
    } else {
        CloseHandle(thread);
    }
}

void on_asset_images_image(void* /*ctx*/, const std::wstring& id) {
    if (!g_app) return;
    AdminApp& app = *g_app;
    app.selected_asset_image = wide_to_utf8(id);
    app.window.assets_images_ui().set_image_selected(id);
    auto& ui = app.window.assets_images_ui();
    if (ui.hwnd()) {
        const std::wstring label = L"Selected: " + id;
        ui.set_text(L"selected_asset_image", label);
    }
}

void on_asset_videos_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app || id != L"btn_generate_video") return;
    AdminApp& app = *g_app;
    auto& ui = app.window.assets_videos_ui();
    if (ui.is_busy()) return;

    ogg::settings::load(app.settings);
    read_settings_ai_from_ui(app);

    const std::string prompt = wide_to_utf8(ui.get_input_text(L"video_prompt"));
    if (prompt.empty()) {
        MessageBoxW(app.window.hwnd(), L"Enter a video prompt.", L"OGG Admin", MB_ICONINFORMATION);
        return;
    }

    app.settings.last_video_prompt = prompt;
    ogg::assets::write_last_video_prompt(prompt);

    auto* work = new VideoGenerateWork{};
    work->notify_hwnd = app.window.hwnd();
    work->settings = app.settings;
    work->prompt = prompt;

    ui.set_busy(true, L"btn_generate_video");
    ui.set_button_enabled(L"btn_generate_video", false);

    HANDLE thread = CreateThread(nullptr, 0, video_generate_worker, work, 0, nullptr);
    if (!thread) {
        ui.set_busy(false);
        ui.set_button_enabled(L"btn_generate_video", true);
        delete work;
        MessageBoxW(app.window.hwnd(), L"Could not start video generation.", L"OGG Admin", MB_ICONERROR);
    } else {
        CloseHandle(thread);
    }
}

void on_asset_videos_image(void* /*ctx*/, const std::wstring& id) {
    if (!g_app) return;
    AdminApp& app = *g_app;
    app.selected_video = wide_to_utf8(id);
    app.window.assets_videos_ui().set_image_selected(id);
    auto& ui = app.window.assets_videos_ui();
    if (ui.hwnd()) {
        const std::wstring label = L"Selected: " + id;
        ui.set_text(L"selected_video", label);
    }
}

void on_settings_ai_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app) return;
    AdminApp& app = *g_app;
    read_settings_ai_from_ui(app);
    if (id == L"btn_go_login_images") {
        reload_login_gallery(app);
        return;
    }
    if (id == L"btn_test_openai") {
        const auto result = ogg::settings::validate_openai_key(app.settings.openai_api_key);
        MessageBoxW(app.window.hwnd(), utf8_to_wide(result.message).c_str(), L"OpenAI", result.ok ? MB_ICONINFORMATION : MB_ICONERROR);
        update_api_call_display(app);
        return;
    }
    if (id == L"btn_test_gemini") {
        if (app.settings.gemini_api_key.empty()) {
            MessageBoxW(app.window.hwnd(), L"Gemini API key is empty.", L"Gemini", MB_ICONERROR);
            return;
        }
        app.gemini_cache.valid = false;
        refresh_gemini_model_select(app, true);
        const bool ok = app.gemini_cache.valid;
        MessageBoxW(
            app.window.hwnd(),
            ok ? L"Gemini key is valid. Image models refreshed." : L"Could not list Gemini image models.",
            L"Gemini",
            ok ? MB_ICONINFORMATION : MB_ICONERROR
        );
        update_api_call_display(app);
        return;
    }
    if (id == L"btn_refresh_gemini_models") {
        if (app.settings.gemini_api_key.empty()) {
            MessageBoxW(app.window.hwnd(), L"Enter a Gemini API key first.", L"Gemini models", MB_ICONINFORMATION);
            return;
        }
        app.gemini_cache.valid = false;
        refresh_gemini_model_select(app, true);
        update_api_call_display(app);
        return;
    }
    if (id == L"btn_refresh_gemini_video_models") {
        if (app.settings.gemini_api_key.empty()) {
            MessageBoxW(app.window.hwnd(), L"Enter a Gemini API key first.", L"Gemini video models", MB_ICONINFORMATION);
            return;
        }
        app.gemini_video_cache.valid = false;
        refresh_gemini_video_model_select(app, true);
        update_api_call_display(app);
        return;
    }
    if (id == L"btn_refresh_openai_video_models") {
        if (app.settings.openai_api_key.empty()) {
            MessageBoxW(app.window.hwnd(), L"Enter an OpenAI API key first.", L"OpenAI video models", MB_ICONINFORMATION);
            return;
        }
        refresh_openai_video_model_select(app, true);
        update_api_call_display(app);
        return;
    }
    if (id == L"btn_save_ai") {
        auto& ui = app.window.settings_ai_ui();
        if (ui.is_busy()) return;

        auto* work = new AiSaveWork{};
        work->notify_hwnd = app.window.hwnd();
        work->settings = app.settings;

        ui.set_busy(true, L"btn_save_ai");
        ui.set_button_enabled(L"btn_save_ai", false);
        ui.set_text(L"ai_save_status", L"Saving...");

        HANDLE thread = CreateThread(nullptr, 0, ai_save_worker, work, 0, nullptr);
        if (!thread) {
            ui.set_busy(false);
            ui.set_button_enabled(L"btn_save_ai", true);
            ui.set_text(L"ai_save_status", L"");
            delete work;
            MessageBoxW(app.window.hwnd(), L"Could not start save.", L"AI Settings", MB_ICONERROR);
        } else {
            CloseHandle(thread);
        }
    }
}

void on_ai_provider_radio(void* ctx, const std::wstring& id, const std::wstring& value) {
    if (!g_app || id != L"ai_provider" || value.empty()) return;
    AdminApp& app = *static_cast<AdminApp*>(ctx);
    app.settings.ai_provider = wide_to_utf8(value);
}

void on_appearance_range(void* ctx, const std::wstring& id, int value) {
    AdminApp& app = *static_cast<AdminApp*>(ctx);
    if (id == L"inset_left") app.settings.input_inset_left = value;
    else if (id == L"inset_right") app.settings.input_inset_right = value;
    else if (id == L"inset_top") app.settings.input_inset_top = value;
    else if (id == L"inset_bottom") app.settings.input_inset_bottom = value;
    else if (id == L"ui_control_width") {
        app.settings.ui_control_width = value;
        apply_control_width(app);
        app.window.layout_content();
        return;
    }
    else if (id == L"ui_button_width" || id == L"ui_small_button_width") {
        if (id == L"ui_button_width") app.settings.ui_button_width = value;
        else app.settings.ui_small_button_width = value;
        apply_button_widths(app);
        app.window.layout_content();
        return;
    }
    else if (id == L"tab_margin_left") {
        app.settings.tab_margin_left = value < 0 ? 0 : (value > 64 ? 64 : value);
        apply_tab_layout(app);
        return;
    }
    else if (id == L"tab_pad_left") {
        app.settings.tab_pad_left = value < 0 ? 0 : (value > 32 ? 32 : value);
        apply_tab_layout(app);
        return;
    }
    else if (id == L"label_control_gap") {
        app.settings.label_control_gap = value < 0 ? 0 : (value > 32 ? 32 : value);
        apply_label_control_gap(app);
        app.window.layout_content();
        return;
    }
    else if (id == L"ui_scroll_step") {
        app.settings.ui_scroll_step = value < 1 ? 1 : (value > 32 ? 32 : value);
        apply_scroll_wheel_step(app);
        return;
    }
    relayout_appearance_preview(app);
}

void on_typography_select(void* ctx, const std::wstring& id, const std::wstring& value) {
    if (!g_app || id != L"ui_font" || value.empty()) return;
    AdminApp& app = *static_cast<AdminApp*>(ctx);
    app.settings.ui_font_family = wide_to_utf8(value);
    apply_typography(app);
}

void on_typography_range(void* ctx, const std::wstring& id, int value) {
    AdminApp& app = *static_cast<AdminApp*>(ctx);
    if (id == L"label_font_size") app.settings.label_font_size = value;
    else if (id == L"input_font_size") app.settings.input_font_size = value;
    else if (id == L"button_font_size") app.settings.button_font_size = value;
    apply_typography(app);
}

void on_settings_appearance_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app || id != L"btn_save_appearance") return;
    AdminApp& app = *g_app;
    read_settings_appearance_from_ui(app);
    if (!ogg::settings::save(app.settings)) {
        MessageBoxW(app.window.hwnd(), L"Failed to save appearance.", L"OGG Admin", MB_ICONERROR);
        return;
    }
    apply_theme(app);
    apply_input_insets(app);
    apply_control_width(app);
    apply_button_widths(app);
    apply_tab_layout(app);
    apply_label_control_gap(app);
    apply_scroll_wheel_step(app);
    populate_dashboard_builds(app);
    MessageBoxW(app.window.hwnd(), L"Appearance saved to %LOCALAPPDATA%\\OffGridGames\\appsettings.json. Run make client to embed theme.", L"OGG Admin", MB_ICONINFORMATION);
}

void on_settings_typography_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app || id != L"btn_save_typography") return;
    AdminApp& app = *g_app;
    read_settings_typography_from_ui(app);
    if (!ogg::settings::save(app.settings)) {
        MessageBoxW(app.window.hwnd(), L"Failed to save typography.", L"OGG Admin", MB_ICONERROR);
        return;
    }
    apply_typography(app);
    MessageBoxW(app.window.hwnd(), L"Typography saved to %LOCALAPPDATA%\\OffGridGames\\appsettings.json.", L"OGG Admin", MB_ICONINFORMATION);
}

void reload_login_gallery(AdminApp& app) {
    const std::string xml = build_login_gallery_xml(app);
    std::string last_prompt = app.settings.last_image_prompt;
    if (last_prompt.empty()) {
        ogg::assets::read_last_image_prompt(last_prompt);
        if (!last_prompt.empty()) app.settings.last_image_prompt = last_prompt;
    }
    const std::wstring last_prompt_wide = utf8_to_wide(last_prompt);

    auto& login_ui = app.window.assets_login_ui();
    const HWND parent = app.window.hwnd();
    if (!login_ui.hwnd()) {
        if (!login_ui.create(parent, xml.c_str())) {
            MessageBoxW(app.window.hwnd(), L"Failed to load login gallery.", L"OGG Admin", MB_ICONERROR);
            return;
        }
        login_ui.set_button_handler(on_login_button);
        login_ui.set_image_handler(on_login_image);
        apply_typography(app);
        apply_control_width(app);
        apply_button_widths(app);
        apply_label_control_gap(app);
        apply_scroll_wheel_step(app);
    } else {
        if (!login_ui.reload_xml(xml.c_str())) {
            MessageBoxW(app.window.hwnd(), L"Failed to reload login gallery.", L"OGG Admin", MB_ICONERROR);
            return;
        }
    }
    if (!last_prompt_wide.empty()) {
        login_ui.set_input_text(L"image_prompt", last_prompt_wide);
    }
    login_ui.set_busy(false);
    login_ui.set_button_enabled(L"btn_generate_image", true);
    refresh_selected_login_image_label(app);
    update_api_call_display(app);
    app.window.layout_content();
}

void reload_asset_images_gallery(AdminApp& app) {
    const std::string xml = build_asset_images_gallery_xml(app);
    std::string last_prompt = app.settings.last_asset_image_prompt;
    if (last_prompt.empty()) {
        ogg::assets::read_last_asset_image_prompt(last_prompt);
        if (!last_prompt.empty()) app.settings.last_asset_image_prompt = last_prompt;
    }
    const std::wstring last_prompt_wide = utf8_to_wide(last_prompt);

    auto& images_ui = app.window.assets_images_ui();
    const HWND parent = app.window.hwnd();
    if (!images_ui.hwnd()) {
        if (!images_ui.create(parent, xml.c_str())) {
            MessageBoxW(app.window.hwnd(), L"Failed to load images gallery.", L"OGG Admin", MB_ICONERROR);
            return;
        }
        images_ui.set_button_handler(on_asset_images_button);
        images_ui.set_image_handler(on_asset_images_image);
        apply_typography(app);
        apply_control_width(app);
        apply_button_widths(app);
        apply_label_control_gap(app);
        apply_scroll_wheel_step(app);
    } else if (!images_ui.reload_xml(xml.c_str())) {
        MessageBoxW(app.window.hwnd(), L"Failed to reload images gallery.", L"OGG Admin", MB_ICONERROR);
        return;
    }

    if (!last_prompt_wide.empty()) {
        images_ui.set_input_text(L"asset_image_prompt", last_prompt_wide);
    }
    if (!app.selected_asset_image.empty()) {
        images_ui.set_text(L"selected_asset_image", L"Selected: " + utf8_to_wide(app.selected_asset_image));
    }
    images_ui.set_busy(false);
    images_ui.set_button_enabled(L"btn_generate_asset_image", true);
    update_api_call_display(app);
    app.window.layout_content();
}

void reload_videos_gallery(AdminApp& app) {
    const std::string xml = build_asset_videos_gallery_xml(app);
    std::string last_prompt = app.settings.last_video_prompt;
    if (last_prompt.empty()) {
        ogg::assets::read_last_video_prompt(last_prompt);
        if (!last_prompt.empty()) app.settings.last_video_prompt = last_prompt;
    }
    const std::wstring last_prompt_wide = utf8_to_wide(last_prompt);

    auto& videos_ui = app.window.assets_videos_ui();
    const HWND parent = app.window.hwnd();
    if (!videos_ui.hwnd()) {
        if (!videos_ui.create(parent, xml.c_str())) {
            MessageBoxW(app.window.hwnd(), L"Failed to load videos gallery.", L"OGG Admin", MB_ICONERROR);
            return;
        }
        videos_ui.set_button_handler(on_asset_videos_button);
        videos_ui.set_image_handler(on_asset_videos_image);
        apply_typography(app);
        apply_control_width(app);
        apply_button_widths(app);
        apply_label_control_gap(app);
        apply_scroll_wheel_step(app);
    } else if (!videos_ui.reload_xml(xml.c_str())) {
        MessageBoxW(app.window.hwnd(), L"Failed to reload videos gallery.", L"OGG Admin", MB_ICONERROR);
        return;
    }

    if (!last_prompt_wide.empty()) {
        videos_ui.set_input_text(L"video_prompt", last_prompt_wide);
    }
    if (!app.selected_video.empty()) {
        videos_ui.set_text(L"selected_video", L"Selected: " + utf8_to_wide(app.selected_video));
    }
    videos_ui.set_busy(false);
    videos_ui.set_button_enabled(L"btn_generate_video", true);
    update_api_call_display(app);
    app.window.layout_content();
}

void on_nav(AdminApp& app, AdminSection section) {
    if (section == AdminSection::Dashboard) {
        populate_dashboard_builds(app);
    }
    if (section == AdminSection::Settings) {
        on_settings_tab(app, SettingsTab::AI);
    }
}

void on_assets_tab(AdminApp& app, AssetsTab tab) {
    app.window.show_assets_tab(tab);
    if (tab == AssetsTab::LoginImages) {
        reload_login_gallery(app);
    } else if (tab == AssetsTab::Images) {
        reload_asset_images_gallery(app);
    } else if (tab == AssetsTab::Videos) {
        reload_videos_gallery(app);
    }
}

void on_settings_tab(AdminApp& app, SettingsTab tab) {
    app.window.show_settings_tab(tab);
    if (tab == SettingsTab::AI) {
        ogg::settings::load(app.settings);
        populate_settings_ai(app);
    } else if (tab == SettingsTab::Appearance) {
        ogg::settings::load(app.settings);
        populate_settings_appearance(app);
    } else if (tab == SettingsTab::Typography) {
        ogg::settings::load(app.settings);
        populate_settings_typography(app);
    } else if (tab == SettingsTab::Docs) {
        apply_control_width(app);
        apply_button_widths(app);
        apply_typography(app);
        apply_input_insets(app);
    }
}

void on_nav_static(void* ctx, AdminSection section) {
    on_nav(*static_cast<AdminApp*>(ctx), section);
}

void on_assets_tab_static(void* ctx, AssetsTab tab) {
    on_assets_tab(*static_cast<AdminApp*>(ctx), tab);
}

void on_settings_tab_static(void* ctx, SettingsTab tab) {
    on_settings_tab(*static_cast<AdminApp*>(ctx), tab);
}

bool load_static_xml(ogg::ui::XmlUiHost& host, HWND parent, const wchar_t* filename) {
    const std::wstring path = admin_xml_path(filename);
    const std::string xml = read_file_utf8(path);
    if (xml.empty()) return false;
    return host.create(parent, xml.c_str());
}

void populate_dashboard_builds(AdminApp& app) {
    auto& ui = app.window.dashboard_ui();
    if (!ui.hwnd()) return;
    refresh_dashboard_version_label(app);
    const std::string source_dir = ogg::settings::resolve_source_dev_dir(app.settings);
    ui.set_text(L"build_source_dir", L"Source dev dir: " + utf8_to_wide(source_dir));
}

bool launch_make_target(AdminApp& app, const wchar_t* target) {
    if (!target || !*target) return false;

    auto& appearance_ui = app.window.settings_appearance_ui();
    if (appearance_ui.hwnd()) {
        const std::string from_ui = http_client::trim(
            wide_to_utf8(appearance_ui.get_input_text(L"source_dev_dir"))
        );
        if (!from_ui.empty()) app.settings.source_dev_dir = from_ui;
    }

    const std::string dir_utf8 = ogg::settings::resolve_source_dev_dir(app.settings);
    if (dir_utf8.empty()) {
        MessageBoxW(app.window.hwnd(), L"Source dev dir is not set.", L"OGG Admin", MB_ICONERROR);
        return false;
    }

    const std::wstring dir = utf8_to_wide(dir_utf8);
    const std::wstring inner = L"cd /d \"" + dir + L"\" && make " + target;
    std::wstring cmd = L"cmd.exe /c \"" + inner + L"\"";
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            dir.c_str(),
            &si,
            &pi)) {
        MessageBoxW(app.window.hwnd(), L"Could not start make (is make on PATH?).", L"OGG Admin", MB_ICONERROR);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void on_dashboard_button(void* /*ctx*/, const std::wstring& id) {
    if (!g_app) return;
    AdminApp& app = *g_app;
    if (id == L"btn_go_login_images") {
        reload_login_gallery(app);
        return;
    }
    if (id == L"btn_make_version") {
        if (launch_make_target(app, L"version")) {
            schedule_dashboard_version_refresh(app.window.hwnd());
        }
        return;
    }
    if (id == L"btn_make_server") {
        launch_make_target(app, L"server");
        return;
    }
    if (id == L"btn_make_client") {
        launch_make_target(app, L"client");
        return;
    }
    if (id == L"btn_make_launcher") {
        launch_make_target(app, L"launcher");
    }
}

void wire_settings_handlers(AdminApp& app) {
    app.window.dashboard_ui().set_button_handler(on_dashboard_button);
    app.window.settings_ai_ui().set_button_handler(on_settings_ai_button);
    app.window.settings_ai_ui().set_radio_handler(on_ai_provider_radio, &app);
    app.window.settings_appearance_ui().set_button_handler(on_settings_appearance_button);
    app.window.settings_appearance_ui().set_range_handler(on_appearance_range, &app);
    app.window.settings_typography_ui().set_button_handler(on_settings_typography_button);
    app.window.settings_typography_ui().set_select_handler(on_typography_select, &app);
    app.window.settings_typography_ui().set_range_handler(on_typography_range, &app);
}

} // namespace

int run_gui() {
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_should_uninit = (com_hr == S_OK);

    ogg::assets::ensure_login_images_seeded();
    ogg::assets::ensure_generated_media_dirs();

    AdminApp app{};
    g_app = &app;
    ogg::settings::load(app.settings);
    ogg::settings::export_for_build();
    apply_theme(app);

    if (!app.window.create()) {
        MessageBoxW(nullptr, L"Failed to create admin window.", L"OGG Admin", MB_ICONERROR);
        if (com_should_uninit) CoUninitialize();
        return 1;
    }

    if (!load_static_xml(app.window.dashboard_ui(), app.window.hwnd(), L"admin_dashboard.xml") ||
        !load_static_xml(app.window.users_ui(), app.window.hwnd(), L"admin_users.xml") ||
        !load_static_xml(app.window.assets_general_ui(), app.window.hwnd(), L"admin_assets_general.xml") ||
        !load_static_xml(app.window.settings_ai_ui(), app.window.hwnd(), L"admin_settings_ai.xml") ||
        !load_static_xml(app.window.settings_appearance_ui(), app.window.hwnd(), L"admin_settings_appearance.xml") ||
        !load_static_xml(app.window.settings_typography_ui(), app.window.hwnd(), L"admin_settings_typography.xml") ||
        !load_static_xml(app.window.settings_docs_ui(), app.window.hwnd(), L"admin_settings_docs.xml")) {
        MessageBoxW(nullptr, L"Failed to load admin UI.", L"OGG Admin", MB_ICONERROR);
        app.window.close();
        if (com_should_uninit) CoUninitialize();
        return 1;
    }

    wire_settings_handlers(app);
    apply_input_insets(app);
    apply_typography(app);
    apply_control_width(app);
    apply_button_widths(app);
    apply_tab_layout(app);
    apply_label_control_gap(app);
    apply_scroll_wheel_step(app);
    populate_settings_ai(app);
    populate_dashboard_builds(app);
    app.window.set_nav_handler(&app, on_nav_static);
    app.window.set_assets_tab_handler(&app, on_assets_tab_static);
    app.window.set_settings_tab_handler(&app, on_settings_tab_static);

    ogg::assets::read_selected_image(app.selected_image);
    app.window.show_section(AdminSection::Dashboard);
    app.window.show();

    MSG msg{};
    const HWND shell_hwnd = app.window.hwnd();
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMsgReloadLoginGallery && g_app) {
            reload_login_gallery(*g_app);
            continue;
        }
        if (msg.message == kMsgAiSaveComplete && g_app) {
            complete_ai_save(*g_app, reinterpret_cast<AiSaveWork*>(msg.lParam));
            continue;
        }
        if (msg.message == kMsgImageGenerateComplete && g_app) {
            complete_image_generate(*g_app, reinterpret_cast<ImageGenerateWork*>(msg.lParam));
            continue;
        }
        if (msg.message == kMsgReloadAssetImagesGallery && g_app) {
            reload_asset_images_gallery(*g_app);
            continue;
        }
        if (msg.message == kMsgReloadVideosGallery && g_app) {
            reload_videos_gallery(*g_app);
            continue;
        }
        if (msg.message == kMsgAssetImageGenerateComplete && g_app) {
            complete_asset_image_generate(*g_app, reinterpret_cast<AssetImageGenerateWork*>(msg.lParam));
            continue;
        }
        if (msg.message == kMsgVideoGenerateComplete && g_app) {
            complete_video_generate(*g_app, reinterpret_cast<VideoGenerateWork*>(msg.lParam));
            continue;
        }
        if (msg.message == kMsgRefreshDashboardVersion && g_app) {
            refresh_dashboard_version_label(*g_app);
            continue;
        }
        if (shell_hwnd && IsDialogMessageW(shell_hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_app = nullptr;
    app.window.close();
    if (com_should_uninit) CoUninitialize();
    return static_cast<int>(msg.wParam);
}

} // namespace ogg::admin
