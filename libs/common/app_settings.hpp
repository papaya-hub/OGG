#pragma once

#include <string>

namespace ogg::settings {

struct AppSettings {
    std::string primary_color = "#59bfff";
    std::string secondary_color = "#4b5563";
    std::string ai_provider = "openai";
    std::string openai_api_key;
    std::string gemini_api_key;
    std::string openai_image_model = "gpt-image-2";
    std::string openai_image_quality = "medium";
    std::string openai_image_size = "1536x1024";
    std::string gemini_image_model = "gemini-2.5-flash-image";
    std::string openai_video_model = "sora-2";
    std::string openai_video_size = "1280x720";
    std::string openai_video_seconds = "8";
    std::string gemini_video_model = "veo-3.1-fast-generate-preview";
    std::string ai_system_prompt =
        "Abstract cinematic landscape art for a game login hero panel. No text, no logos.";
    std::string last_image_prompt;
    std::string last_asset_image_prompt;
    std::string last_video_prompt;
    int input_inset_left = 0;
    int input_inset_right = 0;
    int input_inset_top = 4;
    int input_inset_bottom = 0;
    std::string ui_font_family = "Segoe UI";
    int label_font_size = 12;
    int input_font_size = 14;
    int button_font_size = 13;
    int ui_control_width = 320;
    int ui_button_width = 140;
    int ui_small_button_width = 80;
    int tab_margin_left = 24;
    int tab_pad_left = 8;
    int label_control_gap = 6;
    int ui_scroll_step = 25;
    std::string source_dev_dir;
};

std::string resolve_source_dev_dir(const AppSettings& settings);

std::string appdata_settings_path_utf8();
std::string repo_settings_path_utf8();
bool load(AppSettings& out);
bool save(const AppSettings& settings);
bool export_for_build();

struct KeyValidationResult {
    bool ok = false;
    std::string message;
};

KeyValidationResult validate_openai_key(const std::string& api_key);
KeyValidationResult validate_gemini_key(const std::string& api_key);

} // namespace ogg::settings
