#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_settings.hpp"

namespace ogg::ai_image {

struct GenerateResult {
    bool ok = false;
    std::string message;
    std::vector<std::uint8_t> jpeg_bytes;
};

GenerateResult generate_login_hero(const ogg::settings::AppSettings& settings, const std::string& user_prompt);
GenerateResult generate_asset_image(const ogg::settings::AppSettings& settings, const std::string& user_prompt);
bool save_generated_jpeg(const std::vector<std::uint8_t>& jpeg_bytes, std::string& filename_out);
bool save_generated_asset_image(const std::vector<std::uint8_t>& jpeg_bytes, std::string& filename_out);

// GET /v1beta/models — returns image-capable model ids (no "models/" prefix).
std::vector<std::string> list_gemini_image_models(const std::string& api_key);
bool gemini_model_uses_predict_api(const std::string& model);

} // namespace ogg::ai_image
