#pragma once

#include <string>
#include <vector>

namespace ogg::assets {

struct MediaEntry {
    std::string filename;
    std::string absolute_path_utf8;
};

constexpr char kGeneratedImagesSubdir[] = "generated_images";
constexpr char kGeneratedVideosSubdir[] = "generated_videos";
constexpr char kLastAssetImagePromptFilename[] = "last_asset_image_prompt.txt";
constexpr char kLastVideoPromptFilename[] = "last_video_prompt.txt";

std::string generated_images_dir_utf8();
std::string generated_videos_dir_utf8();

std::vector<MediaEntry> list_generated_images();
std::vector<MediaEntry> list_generated_videos();

bool read_last_asset_image_prompt(std::string& prompt_out);
bool write_last_asset_image_prompt(const std::string& prompt);
bool read_last_video_prompt(std::string& prompt_out);
bool write_last_video_prompt(const std::string& prompt);

bool ensure_generated_media_dirs();

} // namespace ogg::assets
