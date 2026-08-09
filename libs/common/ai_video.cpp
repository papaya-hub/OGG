#include "ai_video.hpp"

#include "generated_media_assets.hpp"
#include "http_client.hpp"
#include "login_image_assets.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ogg::ai_video {

namespace {

namespace fs = std::filesystem;

std::string body_as_string(const std::vector<std::uint8_t>& body) {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

std::vector<std::uint8_t> base64_decode(const std::string& encoded) {
    constexpr std::size_t kMaxDecodedBytes = 128u * 1024u * 1024u;
    if (encoded.size() > (kMaxDecodedBytes * 4u) / 3u + 4u) return {};
    static const int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };

    std::vector<std::uint8_t> out;
    out.reserve(encoded.size() * 3 / 4);
    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int decoded = table[c];
        if (decoded < 0) continue;
        val = (val << 6) + decoded;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
            if (out.size() > kMaxDecodedBytes) return {};
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
            out.push_back(c);
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

std::string escape_json_string(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8]{};
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

std::string build_prompt(const ogg::settings::AppSettings& settings, const std::string& user_prompt) {
    std::string prompt = settings.ai_system_prompt;
    if (!prompt.empty() && !user_prompt.empty()) prompt += " ";
    prompt += user_prompt;
    return prompt;
}

std::string build_multipart_video_body(
    const std::string& boundary,
    const std::string& prompt,
    const std::string& model,
    const std::string& size,
    const std::string& seconds
) {
    std::string body;
    body.reserve(256 + prompt.size() + model.size() + size.size() + seconds.size());
    auto append_field = [&](const char* name, const std::string& value) {
        body += "--";
        body += boundary;
        body += "\r\nContent-Disposition: form-data; name=\"";
        body += name;
        body += "\"\r\n\r\n";
        body += value;
        body += "\r\n";
    };
    append_field("prompt", prompt);
    append_field("model", model);
    append_field("size", size);
    append_field("seconds", seconds);
    body += "--";
    body += boundary;
    body += "--\r\n";
    return body;
}

bool fetch_gemini_models_json(const std::string& api_key, std::string& json_out) {
    const std::string key = http_client::trim(api_key);
    if (key.empty()) return false;

    std::string headers;
    headers.reserve(24 + key.size());
    headers += "x-goog-api-key: ";
    headers += key;
    headers += "\r\n";

    int status = 0;
    std::vector<std::uint8_t> response;
    if (!http_client::https_request(
            L"generativelanguage.googleapis.com",
            L"/v1beta/models",
            L"GET",
            headers,
            {},
            status,
            response)) {
        return false;
    }
    if (status < 200 || status >= 300) return false;
    json_out = body_as_string(response);
    return !json_out.empty();
}

bool gemini_model_block_is_video(const std::string& model, const std::string& block) {
    if (model.find("veo") != std::string::npos) return true;
    if (block.find("\"generateVideos\"") != std::string::npos) return true;
    if (block.find("\"predictLongRunning\"") != std::string::npos && model.find("veo") != std::string::npos) {
        return true;
    }
    return false;
}

std::vector<std::string> parse_gemini_video_models(const std::string& json) {
    std::vector<std::string> models;
    size_t pos = 0;
    while (true) {
        const size_t name_key = json.find("\"name\"", pos);
        if (name_key == std::string::npos) break;
        const size_t models_prefix = json.find("models/", name_key);
        if (models_prefix == std::string::npos || models_prefix > name_key + 24) {
            pos = name_key + 6;
            continue;
        }
        const size_t start = models_prefix + 7;
        const size_t end = json.find('"', start);
        if (end == std::string::npos) break;
        const std::string model = json.substr(start, end - start);
        const size_t block_end = json.find('}', end);
        const std::string block = json.substr(name_key, block_end == std::string::npos ? 512 : block_end - name_key);
        if (gemini_model_block_is_video(model, block)) {
            models.push_back(model);
        }
        pos = end + 1;
    }
    return models;
}

std::vector<std::string> parse_openai_model_ids(const std::string& json) {
    std::vector<std::string> models;
    size_t pos = 0;
    while (true) {
        const size_t id_key = json.find("\"id\"", pos);
        if (id_key == std::string::npos) break;
        const std::string id = extract_json_string(json.substr(id_key, 128), "id");
        if (!id.empty() && id.find("sora") != std::string::npos) {
            models.push_back(id);
        }
        pos = id_key + 4;
    }
    if (models.empty()) {
        models.push_back("sora-2");
        models.push_back("sora-2-pro");
    }
    return models;
}

bool gemini_get_request(const std::string& api_key, const std::wstring& path, std::string& json_out) {
    std::string headers;
    headers.reserve(24 + api_key.size());
    headers += "x-goog-api-key: ";
    headers += api_key;
    headers += "\r\n";

    int status = 0;
    std::vector<std::uint8_t> response;
    if (!http_client::https_request(
            L"generativelanguage.googleapis.com",
            path.c_str(),
            L"GET",
            headers,
            {},
            status,
            response)) {
        return false;
    }
    if (status < 200 || status >= 300) return false;
    json_out = body_as_string(response);
    return !json_out.empty();
}

bool download_https_bytes(const std::string& api_key, const std::string& url, std::vector<std::uint8_t>& out) {
    out.clear();
    if (url.empty()) return false;

    const std::string prefix = "https://generativelanguage.googleapis.com";
    if (url.rfind(prefix, 0) != 0) return false;
    const std::string path = url.substr(prefix.size());
    if (path.empty() || path[0] != '/') return false;

    std::string headers;
    headers.reserve(24 + api_key.size());
    headers += "x-goog-api-key: ";
    headers += api_key;
    headers += "\r\n";

    int status = 0;
    return http_client::https_request(
        L"generativelanguage.googleapis.com",
        http_client::to_wide(path).c_str(),
        L"GET",
        headers,
        {},
        status,
        out) && status >= 200 && status < 300 && !out.empty();
}

std::string extract_first_uri(const std::string& json) {
    size_t pos = 0;
    while (true) {
        const size_t uri_key = json.find("\"uri\"", pos);
        if (uri_key == std::string::npos) break;
        const std::string uri = extract_json_string(json.substr(uri_key, 512), "uri");
        if (!uri.empty()) return uri;
        pos = uri_key + 5;
    }
    return {};
}

VideoGenerateResult generate_openai_video(const ogg::settings::AppSettings& settings, const std::string& prompt) {
    VideoGenerateResult result{};
    const std::string model = settings.openai_video_model.empty() ? "sora-2" : settings.openai_video_model;
    const std::string size = settings.openai_video_size.empty() ? "1280x720" : settings.openai_video_size;
    const std::string seconds = settings.openai_video_seconds.empty() ? "8" : settings.openai_video_seconds;
    const std::string boundary = "----OGGVideoForm7MA4YWxk";

    const std::string body = build_multipart_video_body(boundary, prompt, model, size, seconds);
    const std::vector<std::uint8_t> request(body.begin(), body.end());

    std::string headers;
    headers.reserve(96 + settings.openai_api_key.size());
    headers += "Authorization: Bearer ";
    headers += settings.openai_api_key;
    headers += "\r\nContent-Type: multipart/form-data; boundary=";
    headers += boundary;
    headers += "\r\n";

    int status = 0;
    std::vector<std::uint8_t> response;
    if (!http_client::https_request(
            L"api.openai.com",
            L"/v1/videos",
            L"POST",
            headers,
            request,
            status,
            response)) {
        result.message = "OpenAI video request failed.";
        return result;
    }
    if (status < 200 || status >= 300) {
        result.message = "OpenAI video HTTP " + std::to_string(status) + ": " + body_as_string(response);
        return result;
    }

    const std::string json = body_as_string(response);
    const std::string video_id = extract_json_string(json, "id");
    if (video_id.empty()) {
        result.message = "OpenAI video response missing id.";
        return result;
    }

    std::string status_value;
    for (int attempt = 0; attempt < 120; ++attempt) {
        Sleep(5000);
        const std::wstring poll_path = L"/v1/videos/" + http_client::to_wide(video_id);
        headers = "Authorization: Bearer ";
        headers += settings.openai_api_key;
        headers += "\r\n";
        response.clear();
        status = 0;
        if (!http_client::https_request(
                L"api.openai.com",
                poll_path.c_str(),
                L"GET",
                headers,
                {},
                status,
                response)) {
            continue;
        }
        if (status < 200 || status >= 300) {
            result.message = "OpenAI poll HTTP " + std::to_string(status) + ": " + body_as_string(response);
            return result;
        }
        const std::string poll_json = body_as_string(response);
        status_value = extract_json_string(poll_json, "status");
        if (status_value == "completed") break;
        if (status_value == "failed" || status_value == "cancelled") {
            result.message = "OpenAI video job " + status_value + ".";
            return result;
        }
    }
    if (status_value != "completed") {
        result.message = "OpenAI video timed out while processing.";
        return result;
    }

    const std::wstring content_path = L"/v1/videos/" + http_client::to_wide(video_id) + L"/content";
    headers = "Authorization: Bearer ";
    headers += settings.openai_api_key;
    headers += "\r\n";
    response.clear();
    status = 0;
    if (!http_client::https_request(
            L"api.openai.com",
            content_path.c_str(),
            L"GET",
            headers,
            {},
            status,
            response)) {
        result.message = "OpenAI video download failed.";
        return result;
    }
    if (status < 200 || status >= 300 || response.empty()) {
        result.message = "OpenAI video content HTTP " + std::to_string(status) + ".";
        return result;
    }

    result.mp4_bytes = response;
    result.ok = true;
    result.message = "Generated with OpenAI (" + model + ").";
    return result;
}

VideoGenerateResult generate_gemini_video(const ogg::settings::AppSettings& settings, const std::string& prompt) {
    VideoGenerateResult result{};
    const std::string model = settings.gemini_video_model.empty()
        ? "veo-3.1-fast-generate-preview"
        : settings.gemini_video_model;

    std::string body;
    body.reserve(128 + prompt.size());
    body += "{\"prompt\":\"";
    body += escape_json_string(prompt);
    body += "\",\"config\":{\"aspectRatio\":\"16:9\",\"resolution\":\"720p\"}}";

    const std::vector<std::uint8_t> request(body.begin(), body.end());
    const std::wstring path = L"/v1beta/models/" + http_client::to_wide(model) + L":generateVideos";

    std::string headers;
    headers.reserve(48 + settings.gemini_api_key.size());
    headers += "x-goog-api-key: ";
    headers += settings.gemini_api_key;
    headers += "\r\nContent-Type: application/json\r\n";

    int status = 0;
    std::vector<std::uint8_t> response;
    if (!http_client::https_request(
            L"generativelanguage.googleapis.com",
            path.c_str(),
            L"POST",
            headers,
            request,
            status,
            response)) {
        result.message = "Gemini video request failed.";
        return result;
    }
    if (status < 200 || status >= 300) {
        result.message = "Gemini video HTTP " + std::to_string(status) + ": " + body_as_string(response);
        return result;
    }

    std::string operation_json = body_as_string(response);
    std::string operation_name = extract_json_string(operation_json, "name");
    if (operation_name.empty()) {
        result.message = "Gemini video response missing operation name.";
        return result;
    }

    bool done = false;
    std::string final_json;
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(15000);
        std::wstring poll_path = L"/v1beta/";
        poll_path += http_client::to_wide(operation_name);
        if (!gemini_get_request(settings.gemini_api_key, poll_path, final_json)) {
            continue;
        }
        if (final_json.find("\"done\": true") != std::string::npos || final_json.find("\"done\":true") != std::string::npos) {
            done = true;
            break;
        }
    }
    if (!done) {
        result.message = "Gemini video timed out while processing.";
        return result;
    }

    const std::string uri = extract_first_uri(final_json);
    if (!uri.empty()) {
        if (!download_https_bytes(settings.gemini_api_key, uri, result.mp4_bytes)) {
            result.message = "Failed to download Gemini video.";
            return result;
        }
    } else {
        const std::string b64 = extract_json_string(final_json, "bytesBase64Encoded");
        if (b64.empty()) {
            result.message = "Gemini video response missing download URI.";
            return result;
        }
        result.mp4_bytes = base64_decode(b64);
        if (result.mp4_bytes.empty()) {
            result.message = "Failed to decode Gemini video bytes.";
            return result;
        }
    }

    result.ok = true;
    result.message = "Generated with Gemini (" + model + ").";
    return result;
}

} // namespace

VideoGenerateResult generate_asset_video(const ogg::settings::AppSettings& settings, const std::string& user_prompt) {
    VideoGenerateResult result{};
    if (user_prompt.empty()) {
        result.message = "Enter a video prompt.";
        return result;
    }

    const std::string prompt = build_prompt(settings, user_prompt);
    if (settings.ai_provider == "gemini") {
        if (settings.gemini_api_key.empty()) {
            result.message = "Gemini API key is not configured in Settings.";
            return result;
        }
        return generate_gemini_video(settings, prompt);
    }

    if (settings.openai_api_key.empty()) {
        result.message = "OpenAI API key is not configured in Settings.";
        return result;
    }
    return generate_openai_video(settings, prompt);
}

bool save_generated_video(const std::vector<std::uint8_t>& mp4_bytes, std::string& filename_out) {
    if (mp4_bytes.empty()) return false;
    const std::string dir = assets::generated_videos_dir_utf8();
    if (dir.empty()) return false;

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    char name[64]{};
    std::snprintf(name, sizeof(name), "video_ai_%lld.mp4", stamp);
    filename_out = name;

    const fs::path out_path = fs::path(http_client::to_wide(assets::join_path(dir, filename_out)));
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(mp4_bytes.data()), static_cast<std::streamsize>(mp4_bytes.size()));
    return out.good();
}

std::vector<std::string> list_openai_video_models(const std::string& api_key) {
    const std::string key = http_client::trim(api_key);
    if (key.empty()) return {};

    int status = 0;
    std::vector<std::uint8_t> body;
    const std::string headers = "Authorization: Bearer " + key + "\r\n";
    if (!http_client::https_request(
            L"api.openai.com",
            L"/v1/models",
            L"GET",
            headers,
            {},
            status,
            body)) {
        return {};
    }
    if (status < 200 || status >= 300) return {};
    return parse_openai_model_ids(body_as_string(body));
}

std::vector<std::string> list_gemini_video_models(const std::string& api_key) {
    std::string json;
    if (!fetch_gemini_models_json(api_key, json)) return {};
    return parse_gemini_video_models(json);
}

} // namespace ogg::ai_video

#else

namespace ogg::ai_video {

VideoGenerateResult generate_asset_video(const ogg::settings::AppSettings&, const std::string&) { return {}; }
bool save_generated_video(const std::vector<std::uint8_t>&, std::string&) { return false; }
std::vector<std::string> list_openai_video_models(const std::string&) { return {}; }
std::vector<std::string> list_gemini_video_models(const std::string&) { return {}; }

} // namespace ogg::ai_video

#endif
