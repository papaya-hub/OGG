#include "generated_media_assets.hpp"

#include "login_image_assets.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ogg::assets {

namespace {

namespace fs = std::filesystem;

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::string normalize_slashes(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

bool ends_with_ignore_case(const std::string& value, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len) return false;
    const size_t offset = value.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[offset + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

bool read_prompt_file(const std::string& path_utf8, std::string& prompt_out) {
    prompt_out.clear();
    if (path_utf8.empty()) return false;
    std::ifstream file(fs::path(utf8_to_wide(path_utf8)), std::ios::binary);
    if (!file) return false;
    std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    prompt_out = buffer;
    while (!prompt_out.empty() && (prompt_out.back() == '\r' || prompt_out.back() == '\n')) {
        prompt_out.pop_back();
    }
    return true;
}

bool write_prompt_file(const std::string& path_utf8, const std::string& prompt) {
    if (path_utf8.empty()) return false;
    const fs::path file_path = fs::path(utf8_to_wide(path_utf8));
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }
    std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(prompt.data(), static_cast<std::streamsize>(prompt.size()));
    return file.good();
}

std::string prompt_file_path(const char* filename) {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, filename);
}

std::vector<MediaEntry> list_media_dir(const std::string& dir_utf8, const char* const* extensions, std::size_t extension_count) {
    std::vector<MediaEntry> entries;
    if (dir_utf8.empty()) return entries;

    const fs::path dir = fs::path(utf8_to_wide(dir_utf8));
    if (!fs::exists(dir) || !fs::is_directory(dir)) return entries;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string filename = wide_to_utf8(entry.path().filename().wstring());
        bool matches = false;
        for (std::size_t i = 0; i < extension_count; ++i) {
            if (ends_with_ignore_case(filename, extensions[i])) {
                matches = true;
                break;
            }
        }
        if (!matches) continue;
        MediaEntry item{};
        item.filename = filename;
        item.absolute_path_utf8 = normalize_slashes(wide_to_utf8(entry.path().wstring()));
        entries.push_back(item);
    }

    std::sort(entries.begin(), entries.end(), [](const MediaEntry& a, const MediaEntry& b) {
        return a.filename > b.filename;
    });
    return entries;
}

} // namespace

std::string generated_images_dir_utf8() {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, kGeneratedImagesSubdir);
}

std::string generated_videos_dir_utf8() {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, kGeneratedVideosSubdir);
}

bool ensure_generated_media_dirs() {
    const std::string images = generated_images_dir_utf8();
    const std::string videos = generated_videos_dir_utf8();
    if (images.empty() || videos.empty()) return false;
    std::error_code ec;
    fs::create_directories(fs::path(utf8_to_wide(images)), ec);
    fs::create_directories(fs::path(utf8_to_wide(videos)), ec);
    return true;
}

std::vector<MediaEntry> list_generated_images() {
    static const char* kExtensions[] = { ".jpg", ".jpeg", ".png" };
    return list_media_dir(generated_images_dir_utf8(), kExtensions, 3);
}

std::vector<MediaEntry> list_generated_videos() {
    static const char* kExtensions[] = { ".mp4", ".webm" };
    return list_media_dir(generated_videos_dir_utf8(), kExtensions, 2);
}

bool read_last_asset_image_prompt(std::string& prompt_out) {
    return read_prompt_file(prompt_file_path(kLastAssetImagePromptFilename), prompt_out);
}

bool write_last_asset_image_prompt(const std::string& prompt) {
    return write_prompt_file(prompt_file_path(kLastAssetImagePromptFilename), prompt);
}

bool read_last_video_prompt(std::string& prompt_out) {
    return read_prompt_file(prompt_file_path(kLastVideoPromptFilename), prompt_out);
}

bool write_last_video_prompt(const std::string& prompt) {
    return write_prompt_file(prompt_file_path(kLastVideoPromptFilename), prompt);
}

} // namespace ogg::assets

#else

namespace ogg::assets {

std::string generated_images_dir_utf8() { return {}; }
std::string generated_videos_dir_utf8() { return {}; }
std::vector<MediaEntry> list_generated_images() { return {}; }
std::vector<MediaEntry> list_generated_videos() { return {}; }
bool read_last_asset_image_prompt(std::string&) { return false; }
bool write_last_asset_image_prompt(const std::string&) { return false; }
bool read_last_video_prompt(std::string&) { return false; }
bool write_last_video_prompt(const std::string&) { return false; }
bool ensure_generated_media_dirs() { return false; }

} // namespace ogg::assets

#endif
