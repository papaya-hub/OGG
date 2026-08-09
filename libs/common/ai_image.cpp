#include "ai_image.hpp"

#include "app_settings.hpp"
#include "hero_art_config.hpp"
#include "http_client.hpp"
#include "generated_media_assets.hpp"
#include "login_image_assets.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <objbase.h>
#include <gdiplus.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace ogg::ai_image {

namespace {

namespace fs = std::filesystem;

std::string body_as_string(const std::vector<std::uint8_t>& body) {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
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

std::vector<std::uint8_t> base64_decode(const std::string& encoded) {
    constexpr std::size_t kMaxDecodedBytes = 32u * 1024u * 1024u;
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

Gdiplus::Bitmap* decode_bitmap(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem) return nullptr;
    void* dest = GlobalLock(mem);
    if (!dest) {
        GlobalFree(mem);
        return nullptr;
    }
    std::memcpy(dest, bytes.data(), bytes.size());
    GlobalUnlock(mem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &stream) != S_OK) {
        GlobalFree(mem);
        return nullptr;
    }
    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromStream(stream);
    stream->Release();
    return bitmap;
}

Gdiplus::Bitmap* cover_crop(Gdiplus::Bitmap* source, int dest_w, int dest_h) {
    if (!source) return nullptr;
    const int src_w = static_cast<int>(source->GetWidth());
    const int src_h = static_cast<int>(source->GetHeight());
    if (src_w <= 0 || src_h <= 0) return nullptr;

    const double src_ratio = static_cast<double>(src_w) / static_cast<double>(src_h);
    const double dest_ratio = static_cast<double>(dest_w) / static_cast<double>(dest_h);

    int crop_w = src_w;
    int crop_h = src_h;
    int crop_x = 0;
    int crop_y = 0;
    if (src_ratio > dest_ratio) {
        crop_w = static_cast<int>(src_h * dest_ratio);
        crop_x = (src_w - crop_w) / 2;
    } else {
        crop_h = static_cast<int>(src_w / dest_ratio);
        crop_y = (src_h - crop_h) / 2;
    }

    auto* output = new Gdiplus::Bitmap(dest_w, dest_h, PixelFormat24bppRGB);
    Gdiplus::Graphics graphics(output);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(
        source,
        Gdiplus::Rect(0, 0, dest_w, dest_h),
        crop_x,
        crop_y,
        crop_w,
        crop_h,
        Gdiplus::UnitPixel
    );
    return output;
}

bool encode_jpeg(Gdiplus::Bitmap* bitmap, std::vector<std::uint8_t>& out) {
    out.clear();
    if (!bitmap) return false;

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;

    CLSID jpeg_clsid{};
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0 || size > 1024u * 1024u || num == 0) {
        stream->Release();
        return false;
    }
    std::vector<std::uint8_t> codec_info(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(codec_info.data());
    Gdiplus::GetImageEncoders(num, size, codecs);
    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/jpeg") == 0) {
            jpeg_clsid = codecs[i].Clsid;
            found = true;
            break;
        }
    }
    if (!found) {
        stream->Release();
        return false;
    }

    ULONG quality = 90;
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;

    if (bitmap->Save(stream, &jpeg_clsid, &params) != Gdiplus::Ok) {
        stream->Release();
        return false;
    }

    STATSTG stats{};
    if (stream->Stat(&stats, STATFLAG_NONAME) != S_OK) {
        stream->Release();
        return false;
    }
    const ULONG blob_size = stats.cbSize.LowPart;
    out.resize(blob_size);

    LARGE_INTEGER seek{};
    stream->Seek(seek, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(out.data(), blob_size, &read);
    stream->Release();
    return read == blob_size;
}

class ImageDecodeRuntime {
public:
    ImageDecodeRuntime() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_owned_ = (hr == S_OK);
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&gdi_token_, &input, nullptr) == Gdiplus::Ok) {
            gdi_owned_ = true;
        }
    }
    ~ImageDecodeRuntime() {
        if (gdi_owned_) {
            Gdiplus::GdiplusShutdown(gdi_token_);
            gdi_owned_ = false;
        }
        if (com_owned_) CoUninitialize();
    }
    bool ready() const { return gdi_owned_; }

private:
    bool com_owned_ = false;
    bool gdi_owned_ = false;
    ULONG_PTR gdi_token_ = 0;
};

std::vector<std::uint8_t> bytes_to_stored_jpeg(const std::vector<std::uint8_t>& image_bytes, bool hero_crop) {
    std::vector<std::uint8_t> out;
    ImageDecodeRuntime runtime;
    if (!runtime.ready() || image_bytes.empty()) return out;

    Gdiplus::Bitmap* source = decode_bitmap(image_bytes);
    if (!source) return out;

    if (hero_crop) {
        Gdiplus::Bitmap* cropped = cover_crop(source, hero_art::kPanelWidth, hero_art::kPanelHeight);
        delete source;
        if (cropped) {
            encode_jpeg(cropped, out);
            delete cropped;
        }
        return out;
    }

    encode_jpeg(source, out);
    delete source;
    return out;
}

std::vector<std::uint8_t> png_or_jpeg_to_hero_jpeg(const std::vector<std::uint8_t>& image_bytes) {
    return bytes_to_stored_jpeg(image_bytes, true);
}

std::string escape_json_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (unsigned char c : value) {
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

std::string openai_size_value(const ogg::settings::AppSettings& settings) {
    if (!settings.openai_image_size.empty() && settings.openai_image_size != "auto") {
        return settings.openai_image_size;
    }
    return "1536x1024";
}

std::string openai_quality_value(const ogg::settings::AppSettings& settings) {
    if (settings.openai_image_quality.empty() || settings.openai_image_quality == "auto") {
        return "medium";
    }
    return settings.openai_image_quality;
}

std::string build_openai_request_json(
    const std::string& model,
    const std::string& prompt,
    const std::string& size,
    const std::string& quality
) {
    std::string body;
    body.reserve(256 + model.size() + prompt.size() + size.size() + quality.size());
    body += "{\"model\":\"";
    body += escape_json_string(model);
    body += "\",\"prompt\":\"";
    body += escape_json_string(prompt);
    body += "\",\"n\":1,\"size\":\"";
    body += escape_json_string(size);
    body += "\",\"quality\":\"";
    body += escape_json_string(quality);
    body += "\"}";
    return body;
}

std::string build_gemini_predict_json(const std::string& prompt) {
    std::string body;
    body.reserve(128 + prompt.size());
    body += "{\"instances\":[{\"prompt\":\"";
    body += escape_json_string(prompt);
    body += "\"}],\"parameters\":{\"sampleCount\":1,\"aspectRatio\":\"4:3\"}}";
    return body;
}

std::string build_gemini_generate_content_json(const std::string& prompt) {
    std::string body;
    body.reserve(160 + prompt.size());
    body += "{\"contents\":[{\"parts\":[{\"text\":\"";
    body += escape_json_string(prompt);
    body += "\"}]}],\"generationConfig\":{\"responseModalities\":[\"TEXT\",\"IMAGE\"]}}";
    return body;
}

bool gemini_model_block_is_image(const std::string& model, const std::string& block) {
    if (model.find("imagen") != std::string::npos && block.find("\"predict\"") != std::string::npos) {
        return true;
    }
    if (block.find("\"generateContent\"") == std::string::npos) return false;
    if (model.find("image") != std::string::npos) return true;
    return false;
}

std::string extract_gemini_image_b64(const std::string& json) {
    std::string b64 = extract_json_string(json, "bytesBase64Encoded");
    if (!b64.empty()) return b64;

    size_t search_from = 0;
    while (true) {
        const size_t mime = json.find("\"mimeType\"", search_from);
        if (mime == std::string::npos) break;
        const size_t image_tag = json.find("image/", mime);
        if (image_tag == std::string::npos || image_tag > mime + 32) {
            search_from = mime + 10;
            continue;
        }
        const size_t data_key = json.find("\"data\"", image_tag);
        if (data_key != std::string::npos && data_key < image_tag + 120) {
            const size_t colon = json.find(':', data_key + 6);
            if (colon != std::string::npos) {
                const size_t quote = json.find('"', colon + 1);
                if (quote != std::string::npos) {
                    const size_t end = json.find('"', quote + 1);
                    if (end != std::string::npos && end > quote + 1) {
                        return json.substr(quote + 1, end - quote - 1);
                    }
                }
            }
        }
        search_from = image_tag + 6;
    }
    return {};
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

std::vector<std::string> parse_gemini_image_models(const std::string& json) {
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
        if (gemini_model_block_is_image(model, block)) {
            models.push_back(model);
        }
        pos = end + 1;
    }
    return models;
}

std::vector<std::uint8_t> bytes_from_string(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

GenerateResult generate_gemini_predict(const ogg::settings::AppSettings& settings, const std::string& prompt, bool hero_crop) {
    GenerateResult result{};
    const std::string model = settings.gemini_image_model.empty() ? "gemini-2.5-flash-image" : settings.gemini_image_model;
    const std::string json_body = build_gemini_predict_json(prompt);
    const std::vector<std::uint8_t> request = bytes_from_string(json_body);
    const std::wstring path = L"/v1beta/models/" + http_client::to_wide(model) + L":predict";

    std::string headers;
    headers.reserve(24 + settings.gemini_api_key.size());
    headers += "x-goog-api-key: ";
    headers += settings.gemini_api_key;
    headers += "\r\n";

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
        result.message = "Gemini request failed.";
        return result;
    }
    if (status < 200 || status >= 300) {
        result.message = "Gemini HTTP " + std::to_string(status) + ": " + body_as_string(response);
        return result;
    }

    const std::string json = body_as_string(response);
    const std::string b64 = extract_gemini_image_b64(json);
    if (b64.empty()) {
        result.message = "Gemini response missing image data.";
        return result;
    }
    result.jpeg_bytes = bytes_to_stored_jpeg(base64_decode(b64), hero_crop);
    if (result.jpeg_bytes.empty()) {
        result.message = "Failed to decode Gemini image.";
        return result;
    }
    result.ok = true;
    result.message = "Generated with Gemini (Imagen).";
    return result;
}

GenerateResult generate_gemini_content(const ogg::settings::AppSettings& settings, const std::string& prompt, bool hero_crop) {
    GenerateResult result{};
    const std::string model = settings.gemini_image_model.empty() ? "gemini-2.5-flash-image" : settings.gemini_image_model;
    const std::string json_body = build_gemini_generate_content_json(prompt);
    const std::vector<std::uint8_t> request = bytes_from_string(json_body);
    const std::wstring path = L"/v1beta/models/" + http_client::to_wide(model) + L":generateContent";

    std::string headers;
    headers.reserve(24 + settings.gemini_api_key.size());
    headers += "x-goog-api-key: ";
    headers += settings.gemini_api_key;
    headers += "\r\n";

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
        result.message = "Gemini request failed.";
        return result;
    }
    if (status < 200 || status >= 300) {
        result.message = "Gemini HTTP " + std::to_string(status) + ": " + body_as_string(response);
        return result;
    }

    const std::string json = body_as_string(response);
    const std::string b64 = extract_gemini_image_b64(json);
    if (b64.empty()) {
        result.message = "Gemini response missing image data.";
        return result;
    }
    result.jpeg_bytes = bytes_to_stored_jpeg(base64_decode(b64), hero_crop);
    if (result.jpeg_bytes.empty()) {
        result.message = "Failed to decode Gemini image.";
        return result;
    }
    result.ok = true;
    result.message = "Generated with Gemini.";
    return result;
}

GenerateResult generate_gemini(const ogg::settings::AppSettings& settings, const std::string& prompt, bool hero_crop) {
    const std::string model = settings.gemini_image_model.empty() ? "gemini-2.5-flash-image" : settings.gemini_image_model;
    if (model.find("imagen") != std::string::npos) {
        return generate_gemini_predict(settings, prompt, hero_crop);
    }
    return generate_gemini_content(settings, prompt, hero_crop);
}

GenerateResult generate_openai(const ogg::settings::AppSettings& settings, const std::string& prompt, bool hero_crop) {
    GenerateResult result{};
    const std::string model = settings.openai_image_model.empty() ? "gpt-image-2" : settings.openai_image_model;
    const std::string quality = openai_quality_value(settings);
    const std::string size = openai_size_value(settings);
    const std::string json_body = build_openai_request_json(model, prompt, size, quality);
    const std::vector<std::uint8_t> request = bytes_from_string(json_body);

    std::string headers;
    headers.reserve(32 + settings.openai_api_key.size());
    headers += "Authorization: Bearer ";
    headers += settings.openai_api_key;
    headers += "\r\n";

    int status = 0;
    std::vector<std::uint8_t> response;
    if (!http_client::https_request(
            L"api.openai.com",
            L"/v1/images/generations",
            L"POST",
            headers,
            request,
            status,
            response)) {
        result.message = "OpenAI request failed.";
        return result;
    }
    if (status < 200 || status >= 300) {
        result.message = "OpenAI HTTP " + std::to_string(status) + ": " + body_as_string(response);
        return result;
    }

    const std::string json = body_as_string(response);
    const std::string b64 = extract_json_string(json, "b64_json");
    if (b64.empty()) {
        result.message = "OpenAI response missing image data.";
        return result;
    }
    result.jpeg_bytes = bytes_to_stored_jpeg(base64_decode(b64), hero_crop);
    if (result.jpeg_bytes.empty()) {
        result.message = "Failed to decode OpenAI image.";
        return result;
    }
    result.ok = true;
    result.message = "Generated with OpenAI.";
    return result;
}

} // namespace

GenerateResult generate_login_hero(const ogg::settings::AppSettings& settings, const std::string& user_prompt) {
    GenerateResult result{};
    if (user_prompt.empty()) {
        result.message = "Enter an image prompt.";
        return result;
    }

    std::string prompt = settings.ai_system_prompt;
    if (!prompt.empty() && !user_prompt.empty()) prompt += " ";
    prompt += user_prompt;

    if (settings.ai_provider == "gemini") {
        if (settings.gemini_api_key.empty()) {
            result.message = "Gemini API key is not configured in Settings.";
            return result;
        }
        return generate_gemini(settings, prompt, true);
    }

    if (settings.openai_api_key.empty()) {
        result.message = "OpenAI API key is not configured in Settings.";
        return result;
    }
    return generate_openai(settings, prompt, true);
}

GenerateResult generate_asset_image(const ogg::settings::AppSettings& settings, const std::string& user_prompt) {
    GenerateResult result{};
    if (user_prompt.empty()) {
        result.message = "Enter an image prompt.";
        return result;
    }

    std::string prompt = settings.ai_system_prompt;
    if (!prompt.empty() && !user_prompt.empty()) prompt += " ";
    prompt += user_prompt;

    if (settings.ai_provider == "gemini") {
        if (settings.gemini_api_key.empty()) {
            result.message = "Gemini API key is not configured in Settings.";
            return result;
        }
        return generate_gemini(settings, prompt, false);
    }

    if (settings.openai_api_key.empty()) {
        result.message = "OpenAI API key is not configured in Settings.";
        return result;
    }
    return generate_openai(settings, prompt, false);
}

bool gemini_model_uses_predict_api(const std::string& model) {
    return model.find("imagen") != std::string::npos;
}

std::vector<std::string> list_gemini_image_models(const std::string& api_key) {
    std::string json;
    if (!fetch_gemini_models_json(api_key, json)) return {};
    return parse_gemini_image_models(json);
}

bool save_generated_jpeg(const std::vector<std::uint8_t>& jpeg_bytes, std::string& filename_out) {
    if (jpeg_bytes.empty()) return false;
    const std::string dir = assets::login_images_dir_utf8();
    if (dir.empty()) return false;

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    char name[64]{};
    std::snprintf(name, sizeof(name), "login_ai_%lld.jpg", stamp);
    filename_out = name;

    const fs::path out_path = fs::path(http_client::to_wide(assets::join_path(dir, filename_out)));
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
    if (!out.good()) return false;

    assets::write_selected_image(filename_out);
    assets::sync_hero_art_from_selection();
    return true;
}

bool save_generated_asset_image(const std::vector<std::uint8_t>& jpeg_bytes, std::string& filename_out) {
    if (jpeg_bytes.empty()) return false;
    const std::string dir = assets::generated_images_dir_utf8();
    if (dir.empty()) return false;

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    char name[64]{};
    std::snprintf(name, sizeof(name), "image_ai_%lld.jpg", stamp);
    filename_out = name;

    const fs::path out_path = fs::path(http_client::to_wide(assets::join_path(dir, filename_out)));
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
    return out.good();
}

} // namespace ogg::ai_image

#else

namespace ogg::ai_image {

GenerateResult generate_login_hero(const ogg::settings::AppSettings&, const std::string&) { return {}; }
GenerateResult generate_asset_image(const ogg::settings::AppSettings&, const std::string&) { return {}; }
bool save_generated_jpeg(const std::vector<std::uint8_t>&, std::string&) { return false; }
bool save_generated_asset_image(const std::vector<std::uint8_t>&, std::string&) { return false; }
std::vector<std::string> list_gemini_image_models(const std::string&) { return {}; }
bool gemini_model_uses_predict_api(const std::string&) { return false; }

} // namespace ogg::ai_image

#endif
