#include "app_settings.hpp"

#include "http_client.hpp"
#include "login_image_assets.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace ogg::settings {

namespace {

namespace fs = std::filesystem;

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

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string extract_json_string(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) return {};
    const size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) return {};
    const size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return {};
    std::string out;
    bool escape = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (escape) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') break;
        out.push_back(c);
    }
    return out;
}

bool write_text_file(const fs::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << text;
    return out.good();
}

bool parse_hex_color(const std::string& hex, int& r, int& g, int& b) {
    if (hex.size() < 7 || hex[0] != '#') return false;
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int r1 = hex_val(hex[1]);
    const int r2 = hex_val(hex[2]);
    const int g1 = hex_val(hex[3]);
    const int g2 = hex_val(hex[4]);
    const int b1 = hex_val(hex[5]);
    const int b2 = hex_val(hex[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
    r = (r1 << 4) | r2;
    g = (g1 << 4) | g2;
    b = (b1 << 4) | b2;
    return true;
}

std::string color_to_hex(int r, int g, int b) {
    char buf[8]{};
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return buf;
}

std::string darken_hex_color(const std::string& hex, float factor = 0.92f) {
    int r = 89, g = 191, b = 255;
    if (!parse_hex_color(hex, r, g, b)) return hex;
    r = static_cast<int>(r * factor);
    g = static_cast<int>(g * factor);
    b = static_cast<int>(b * factor);
    return color_to_hex(r, g, b);
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool read_file(const std::string& path_utf8, std::string& out) {
    std::ifstream file(fs::path(utf8_to_wide(path_utf8)), std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

bool write_themed_client_login(const std::string& repo_root, const AppSettings& settings) {
    const std::string source = assets::join_path(
        assets::join_path(repo_root, "src/OffGridGamer.OffGridGames.Client"),
        "client_login.xml");
    std::string xml;
    if (!read_file(source, xml)) return false;

    const std::string primary = settings.primary_color.empty() ? "#59bfff" : settings.primary_color;
    const std::string hover = darken_hex_color(primary);
    replace_all(xml, "#59bfff", primary);
    replace_all(xml, "#4aaef0", hover);

    const fs::path out = fs::path(utf8_to_wide(
        assets::join_path(repo_root, "build/generated/client_login.xml")));
    return write_text_file(out, xml);
}

bool write_client_theme_header(const std::string& repo_root, const AppSettings& settings) {
    int pr = 89, pg = 191, pb = 255;
    int sr = 75, sg = 85, sb = 99;
    parse_hex_color(settings.primary_color, pr, pg, pb);
    parse_hex_color(settings.secondary_color, sr, sg, sb);

    const fs::path out = fs::path(utf8_to_wide(assets::join_path(repo_root, "build/generated/client_theme.hpp")));
    if (out.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
    }

    std::ostringstream header;
    header << "#pragma once\n\n#include <d2d1.h>\n\nnamespace ogg { namespace ui { namespace theme {\n\n";
    auto color_lit = [](float v) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.6ff", static_cast<double>(v));
        return std::string(buf);
    };
    header << "inline D2D1_COLOR_F primary_color() { return D2D1::ColorF("
           << color_lit(pr / 255.f) << ", " << color_lit(pg / 255.f) << ", "
           << color_lit(pb / 255.f) << ", 1.f); }\n";
    header << "inline D2D1_COLOR_F secondary_color() { return D2D1::ColorF("
           << color_lit(sr / 255.f) << ", " << color_lit(sg / 255.f) << ", "
           << color_lit(sb / 255.f) << ", 1.f); }\n";
    header << "\n} } } // namespace ogg::ui::theme\n";
    return write_text_file(out, header.str());
}

bool extract_json_int(const std::string& json, const char* key, int& out) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) return false;
    const size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    bool negative = false;
    if (i < json.size() && json[i] == '-') {
        negative = true;
        ++i;
    }
    int value = 0;
    bool any = false;
    while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
        any = true;
        value = value * 10 + (json[i] - '0');
        ++i;
    }
    if (!any) return false;
    out = negative ? -value : value;
    return true;
}

bool write_input_insets_header(const std::string& repo_root, const AppSettings& settings) {
    const fs::path out = fs::path(utf8_to_wide(assets::join_path(repo_root, "build/generated/input_insets.hpp")));
    if (out.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
    }
    std::ostringstream header;
    header << "#pragma once\n\nnamespace ogg { namespace ui { namespace theme {\n\n";
    header << "inline int input_inset_left() { return " << settings.input_inset_left << "; }\n";
    header << "inline int input_inset_right() { return " << settings.input_inset_right << "; }\n";
    header << "inline int input_inset_top() { return " << settings.input_inset_top << "; }\n";
    header << "inline int input_inset_bottom() { return " << settings.input_inset_bottom << "; }\n";
    header << "\n} } } // namespace ogg::ui::theme\n";
    return write_text_file(out, header.str());
}

std::string escape_c_string(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

bool write_ui_typography_header(const std::string& repo_root, const AppSettings& settings) {
    const fs::path out = fs::path(utf8_to_wide(assets::join_path(repo_root, "build/generated/ui_typography.hpp")));
    if (out.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
    }
    const std::string family = settings.ui_font_family.empty() ? "Segoe UI" : settings.ui_font_family;
    std::ostringstream header;
    header << "#pragma once\n\nnamespace ogg { namespace ui { namespace theme {\n\n";
    header << "inline const wchar_t* ui_font_family() { return L\"" << escape_c_string(family) << "\"; }\n";
    header << "inline float label_font_size() { return " << settings.label_font_size << ".0f; }\n";
    header << "inline float input_font_size() { return " << settings.input_font_size << ".0f; }\n";
    header << "inline float button_font_size() { return " << settings.button_font_size << ".0f; }\n";
    header << "inline int ui_control_width() { return " << settings.ui_control_width << "; }\n";
    header << "inline int ui_button_width() { return " << settings.ui_button_width << "; }\n";
    header << "inline int ui_small_button_width() { return " << settings.ui_small_button_width << "; }\n";
    header << "inline int label_control_gap() { return " << settings.label_control_gap << "; }\n";
    header << "inline int ui_scroll_step() { return " << settings.ui_scroll_step << "; }\n";
    header << "\n} } } // namespace ogg::ui::theme\n";
    return write_text_file(out, header.str());
}

std::string serialize(const AppSettings& s) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"theme\": {\n";
    json << "    \"primary_color\": \"" << escape_json(s.primary_color) << "\",\n";
    json << "    \"secondary_color\": \"" << escape_json(s.secondary_color) << "\",\n";
    json << "    \"input_inset_left\": " << s.input_inset_left << ",\n";
    json << "    \"input_inset_right\": " << s.input_inset_right << ",\n";
    json << "    \"input_inset_top\": " << s.input_inset_top << ",\n";
    json << "    \"input_inset_bottom\": " << s.input_inset_bottom << ",\n";
    json << "    \"ui_font_family\": \"" << escape_json(s.ui_font_family) << "\",\n";
    json << "    \"label_font_size\": " << s.label_font_size << ",\n";
    json << "    \"input_font_size\": " << s.input_font_size << ",\n";
    json << "    \"button_font_size\": " << s.button_font_size << ",\n";
    json << "    \"ui_control_width\": " << s.ui_control_width << ",\n";
    json << "    \"ui_button_width\": " << s.ui_button_width << ",\n";
    json << "    \"ui_small_button_width\": " << s.ui_small_button_width << ",\n";
    json << "    \"tab_margin_left\": " << s.tab_margin_left << ",\n";
    json << "    \"tab_pad_left\": " << s.tab_pad_left << ",\n";
    json << "    \"label_control_gap\": " << s.label_control_gap << ",\n";
    json << "    \"ui_scroll_step\": " << s.ui_scroll_step << "\n";
    json << "  },\n";
    json << "  \"build\": {\n";
    json << "    \"source_dev_dir\": \"" << escape_json(s.source_dev_dir) << "\"\n";
    json << "  },\n";
    json << "  \"ai\": {\n";
    json << "    \"provider\": \"" << escape_json(s.ai_provider) << "\",\n";
    json << "    \"openai_api_key\": \"" << escape_json(s.openai_api_key) << "\",\n";
    json << "    \"gemini_api_key\": \"" << escape_json(s.gemini_api_key) << "\",\n";
    json << "    \"openai_image_model\": \"" << escape_json(s.openai_image_model) << "\",\n";
    json << "    \"openai_image_quality\": \"" << escape_json(s.openai_image_quality) << "\",\n";
    json << "    \"openai_image_size\": \"" << escape_json(s.openai_image_size) << "\",\n";
    json << "    \"gemini_image_model\": \"" << escape_json(s.gemini_image_model) << "\",\n";
    json << "    \"openai_video_model\": \"" << escape_json(s.openai_video_model) << "\",\n";
    json << "    \"openai_video_size\": \"" << escape_json(s.openai_video_size) << "\",\n";
    json << "    \"openai_video_seconds\": \"" << escape_json(s.openai_video_seconds) << "\",\n";
    json << "    \"gemini_video_model\": \"" << escape_json(s.gemini_video_model) << "\",\n";
    json << "    \"system_prompt\": \"" << escape_json(s.ai_system_prompt) << "\",\n";
    json << "    \"last_image_prompt\": \"" << escape_json(s.last_image_prompt) << "\",\n";
    json << "    \"last_asset_image_prompt\": \"" << escape_json(s.last_asset_image_prompt) << "\",\n";
    json << "    \"last_video_prompt\": \"" << escape_json(s.last_video_prompt) << "\"\n";
    json << "  }\n";
    json << "}\n";
    return json.str();
}

bool parse(const std::string& json, AppSettings& out) {
    AppSettings parsed = out;
    const std::string primary = extract_json_string(json, "primary_color");
    const std::string secondary = extract_json_string(json, "secondary_color");
    const std::string provider = extract_json_string(json, "provider");
    const std::string openai_key = extract_json_string(json, "openai_api_key");
    const std::string gemini_key = extract_json_string(json, "gemini_api_key");
    const std::string openai_model = extract_json_string(json, "openai_image_model");
    const std::string openai_quality = extract_json_string(json, "openai_image_quality");
    const std::string openai_size = extract_json_string(json, "openai_image_size");
    const std::string gemini_model = extract_json_string(json, "gemini_image_model");
    const std::string openai_video_model = extract_json_string(json, "openai_video_model");
    const std::string openai_video_size = extract_json_string(json, "openai_video_size");
    const std::string openai_video_seconds = extract_json_string(json, "openai_video_seconds");
    const std::string gemini_video_model = extract_json_string(json, "gemini_video_model");
    const std::string system_prompt = extract_json_string(json, "system_prompt");
    const std::string last_prompt = extract_json_string(json, "last_image_prompt");
    const std::string last_asset_prompt = extract_json_string(json, "last_asset_image_prompt");
    const std::string last_video_prompt = extract_json_string(json, "last_video_prompt");
    const std::string source_dev_dir = extract_json_string(json, "source_dev_dir");

    if (!primary.empty()) parsed.primary_color = primary;
    if (!secondary.empty()) parsed.secondary_color = secondary;
    extract_json_int(json, "input_inset_left", parsed.input_inset_left);
    extract_json_int(json, "input_inset_right", parsed.input_inset_right);
    extract_json_int(json, "input_inset_top", parsed.input_inset_top);
    extract_json_int(json, "input_inset_bottom", parsed.input_inset_bottom);
    const std::string ui_font = extract_json_string(json, "ui_font_family");
    if (!ui_font.empty()) parsed.ui_font_family = ui_font;
    extract_json_int(json, "label_font_size", parsed.label_font_size);
    extract_json_int(json, "input_font_size", parsed.input_font_size);
    extract_json_int(json, "button_font_size", parsed.button_font_size);
    extract_json_int(json, "ui_control_width", parsed.ui_control_width);
    extract_json_int(json, "ui_button_width", parsed.ui_button_width);
    extract_json_int(json, "ui_small_button_width", parsed.ui_small_button_width);
    const bool has_tab_margin = json.find("\"tab_margin_left\"") != std::string::npos;
    extract_json_int(json, "tab_margin_left", parsed.tab_margin_left);
    extract_json_int(json, "tab_pad_left", parsed.tab_pad_left);
    if (!has_tab_margin && json.find("\"tab_pad_left\"") != std::string::npos) {
        parsed.tab_margin_left = parsed.tab_pad_left;
        parsed.tab_pad_left = 8;
    }
    extract_json_int(json, "label_control_gap", parsed.label_control_gap);
    extract_json_int(json, "ui_scroll_step", parsed.ui_scroll_step);
    if (!source_dev_dir.empty() || json.find("\"source_dev_dir\"") != std::string::npos) {
        parsed.source_dev_dir = source_dev_dir;
    }
    if (!provider.empty()) parsed.ai_provider = provider;
    if (!openai_key.empty() || json.find("\"openai_api_key\"") != std::string::npos) parsed.openai_api_key = openai_key;
    if (!gemini_key.empty() || json.find("\"gemini_api_key\"") != std::string::npos) parsed.gemini_api_key = gemini_key;
    if (json.find("\"openai_image_model\"") != std::string::npos) parsed.openai_image_model = openai_model;
    if (json.find("\"openai_image_quality\"") != std::string::npos) parsed.openai_image_quality = openai_quality;
    if (json.find("\"openai_image_size\"") != std::string::npos) parsed.openai_image_size = openai_size;
    if (json.find("\"gemini_image_model\"") != std::string::npos) parsed.gemini_image_model = gemini_model;
    if (json.find("\"openai_video_model\"") != std::string::npos) parsed.openai_video_model = openai_video_model;
    if (json.find("\"openai_video_size\"") != std::string::npos) parsed.openai_video_size = openai_video_size;
    if (json.find("\"openai_video_seconds\"") != std::string::npos) parsed.openai_video_seconds = openai_video_seconds;
    if (json.find("\"gemini_video_model\"") != std::string::npos) parsed.gemini_video_model = gemini_video_model;
    if (json.find("\"system_prompt\"") != std::string::npos) {
        parsed.ai_system_prompt = system_prompt;
    }
    if (!last_prompt.empty() || json.find("\"last_image_prompt\"") != std::string::npos) {
        parsed.last_image_prompt = last_prompt;
    }
    if (!last_asset_prompt.empty() || json.find("\"last_asset_image_prompt\"") != std::string::npos) {
        parsed.last_asset_image_prompt = last_asset_prompt;
    }
    if (!last_video_prompt.empty() || json.find("\"last_video_prompt\"") != std::string::npos) {
        parsed.last_video_prompt = last_video_prompt;
    }
    out = parsed;
    return true;
}

bool seed_appdata_from_repo_if_missing() {
    const std::string appdata_path = appdata_settings_path_utf8();
    if (appdata_path.empty()) return false;
    const fs::path appdata_file = fs::path(utf8_to_wide(appdata_path));
    if (fs::exists(appdata_file)) return true;

    const std::string repo_path = repo_settings_path_utf8();
    if (repo_path.empty()) return false;
    std::string json;
    if (!read_file(repo_path, json)) return false;
    return write_text_file(appdata_file, json);
}

} // namespace

std::string appdata_settings_path_utf8() {
    const std::string root = assets::appdata_root_utf8();
    if (root.empty()) return {};
    return assets::join_path(root, "appsettings.json");
}

std::string repo_settings_path_utf8() {
    std::string root;
    if (!assets::find_repo_root(root)) return {};
    return assets::join_path(root, "config/appsettings.json");
}

bool load(AppSettings& out) {
    seed_appdata_from_repo_if_missing();
    std::string json;
    const std::string appdata_path = appdata_settings_path_utf8();
    if (!appdata_path.empty() && read_file(appdata_path, json)) {
        parse(json, out);
    } else {
        const std::string repo_path = repo_settings_path_utf8();
        if (!repo_path.empty() && read_file(repo_path, json)) {
            parse(json, out);
        }
    }
    std::string prompt_file;
    if (assets::read_last_image_prompt(prompt_file)) {
        out.last_image_prompt = prompt_file;
    }
    return true;
}

bool save(const AppSettings& settings) {
    assets::write_last_image_prompt(settings.last_image_prompt);
    const std::string json = serialize(settings);
    const std::string appdata_path = appdata_settings_path_utf8();
    if (appdata_path.empty()) return false;
    if (!write_text_file(fs::path(utf8_to_wide(appdata_path)), json)) return false;
    return export_for_build();
}

bool export_for_build() {
    AppSettings settings{};
    load(settings);

    std::string root;
    if (!assets::find_repo_root(root)) return false;

    const fs::path generated_dir = fs::path(utf8_to_wide(assets::join_path(root, "build/generated")));
    std::error_code ec;
    fs::create_directories(generated_dir, ec);

    if (!write_client_theme_header(root, settings)) return false;
    if (!write_input_insets_header(root, settings)) return false;
    if (!write_ui_typography_header(root, settings)) return false;
    return write_themed_client_login(root, settings);
}

KeyValidationResult validate_openai_key(const std::string& api_key) {
    KeyValidationResult result{};
    const std::string key = http_client::trim(api_key);
    if (key.empty()) {
        result.message = "OpenAI API key is empty.";
        return result;
    }
    int status = 0;
    std::vector<std::uint8_t> body;
    const std::string headers =
        "Authorization: Bearer " + key + "\r\n";
    if (!http_client::https_request(
            L"api.openai.com",
            L"/v1/models",
            L"GET",
            headers,
            {},
            status,
            body)) {
        result.message = "Could not reach OpenAI API.";
        return result;
    }
    if (status >= 200 && status < 300) {
        result.ok = true;
        result.message = "OpenAI key is valid.";
        return result;
    }
    result.message = "OpenAI rejected the key (HTTP " + std::to_string(status) + ").";
    return result;
}

KeyValidationResult validate_gemini_key(const std::string& api_key) {
    KeyValidationResult result{};
    const std::string key = http_client::trim(api_key);
    if (key.empty()) {
        result.message = "Gemini API key is empty.";
        return result;
    }
    int status = 0;
    std::vector<std::uint8_t> body;
    const std::wstring path = L"/v1beta/models?key=" + http_client::to_wide(key);
    if (!http_client::https_request(
            L"generativelanguage.googleapis.com",
            path.c_str(),
            L"GET",
            "",
            {},
            status,
            body)) {
        result.message = "Could not reach Gemini API.";
        return result;
    }
    if (status >= 200 && status < 300) {
        result.ok = true;
        result.message = "Gemini key is valid.";
        return result;
    }
    result.message = "Gemini rejected the key (HTTP " + std::to_string(status) + ").";
    return result;
}

std::string default_source_dev_dir_utf8() {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return {};
    const fs::path parent = fs::path(exe_path).parent_path().parent_path();
    if (parent.empty()) return {};
    return assets::join_path(wide_to_utf8(parent.wstring()), std::string{});
}

std::string resolve_source_dev_dir(const AppSettings& settings) {
    const std::string configured = trim(settings.source_dev_dir);
    if (!configured.empty()) {
        const fs::path dir = fs::path(utf8_to_wide(configured));
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            return assets::join_path(configured, std::string{});
        }
    }
    std::string root;
    if (assets::find_repo_root(root)) return root;
    return default_source_dev_dir_utf8();
}

} // namespace ogg::settings

#else

namespace ogg::settings {

std::string appdata_settings_path_utf8() { return {}; }
std::string repo_settings_path_utf8() { return {}; }
bool load(AppSettings&) { return false; }
bool save(const AppSettings&) { return false; }
bool export_for_build() { return false; }
std::string resolve_source_dev_dir(const AppSettings&) { return {}; }
KeyValidationResult validate_openai_key(const std::string&) { return {}; }
KeyValidationResult validate_gemini_key(const std::string&) { return {}; }

} // namespace ogg::settings

#endif
