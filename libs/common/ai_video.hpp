#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_settings.hpp"

namespace ogg::ai_video {

struct VideoGenerateResult {
    bool ok = false;
    std::string message;
    std::vector<std::uint8_t> mp4_bytes;
};

VideoGenerateResult generate_asset_video(const ogg::settings::AppSettings& settings, const std::string& user_prompt);
bool save_generated_video(const std::vector<std::uint8_t>& mp4_bytes, std::string& filename_out);

// GET /v1/models — OpenAI models whose id contains "sora".
std::vector<std::string> list_openai_video_models(const std::string& api_key);

// GET /v1beta/models — Gemini models that support generateVideos / Veo.
std::vector<std::string> list_gemini_video_models(const std::string& api_key);

} // namespace ogg::ai_video
