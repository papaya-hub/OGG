#pragma once

#include <string>
#include <vector>

namespace ogg::assets {

struct LoginImageEntry {
    std::string filename;
    std::string absolute_path_utf8;
};

// Gallery + selection live under %LOCALAPPDATA%/OffGridGames (persistent across builds).
constexpr wchar_t kAppDataFolderName[] = L"OffGridGames";
constexpr char kLoginImagesSubdir[] = "login_images";
constexpr char kSelectedFilename[] = "selected_login_image.txt";
constexpr char kLastImagePromptFilename[] = "last_image_prompt.txt";

// Build-time embed source in the repo (synced from AppData before make client).
constexpr char kHeroArtRelativePath[] =
    "src/OffGridGamer.OffGridGames.Client/hero_art.jpg";

// Legacy repo paths migrated into AppData on first use.
constexpr char kLegacyLoginImagesRelativeDir[] =
    "src/OffGridGamer.OffGridGames.Client/login_images";
constexpr char kLegacySelectedImageRelativeFile[] =
    "src/OffGridGamer.OffGridGames.Client/selected_login_image.txt";

bool find_repo_root(std::string& repo_root_utf8_out);
std::string join_path(const std::string& left, const std::string& right);
std::string appdata_root_utf8();
std::string login_images_dir_utf8();
std::string selected_image_file_utf8();
std::string last_image_prompt_file_utf8();
std::string hero_art_path_utf8();

std::vector<LoginImageEntry> list_login_images();
bool read_selected_image(std::string& filename_out);
bool write_selected_image(const std::string& filename);
bool read_last_image_prompt(std::string& prompt_out);
bool write_last_image_prompt(const std::string& prompt);
bool sync_hero_art_from_selection();
bool ensure_login_images_seeded();

} // namespace ogg::assets
