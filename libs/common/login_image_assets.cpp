#include "login_image_assets.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ogg::assets {

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

std::string normalize_slashes(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

bool ends_with_ignore_case(const std::string& value, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len) return false;
    const size_t start = value.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[start + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

bool copy_file_binary(const fs::path& from, const fs::path& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

bool directory_has_jpg(const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = wide_to_utf8(entry.path().filename().wstring());
        if (ends_with_ignore_case(name, ".jpg") || ends_with_ignore_case(name, ".jpeg")) {
            return true;
        }
    }
    return false;
}

void migrate_legacy_gallery(const std::string& legacy_dir_utf8, const std::string& appdata_dir_utf8) {
    if (legacy_dir_utf8.empty() || appdata_dir_utf8.empty()) return;
    const fs::path legacy = fs::path(utf8_to_wide(legacy_dir_utf8));
    const fs::path appdata = fs::path(utf8_to_wide(appdata_dir_utf8));
    if (!fs::exists(legacy) || !fs::is_directory(legacy)) return;

    std::error_code ec;
    fs::create_directories(appdata, ec);
    for (const auto& entry : fs::directory_iterator(legacy)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = wide_to_utf8(entry.path().filename().wstring());
        if (!ends_with_ignore_case(name, ".jpg") && !ends_with_ignore_case(name, ".jpeg")) continue;
        const fs::path dest = appdata / entry.path().filename();
        if (!fs::exists(dest)) {
            copy_file_binary(entry.path(), dest);
        }
    }
}

void migrate_legacy_selection(const std::string& legacy_file_utf8, const std::string& appdata_file_utf8) {
    if (legacy_file_utf8.empty() || appdata_file_utf8.empty()) return;
    const fs::path legacy = fs::path(utf8_to_wide(legacy_file_utf8));
    const fs::path appdata = fs::path(utf8_to_wide(appdata_file_utf8));
    if (fs::exists(appdata) || !fs::exists(legacy)) return;

    std::ifstream in(legacy);
    if (!in) return;
    std::string selected;
    std::getline(in, selected);
    while (!selected.empty() && (selected.back() == '\r' || selected.back() == '\n' || selected.back() == ' ')) {
        selected.pop_back();
    }
    if (selected.empty()) return;

    if (appdata.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(appdata.parent_path(), ec);
    }
    std::ofstream out(appdata, std::ios::trunc);
    if (!out) return;
    out << selected;
}

std::string legacy_login_images_dir_utf8() {
    std::string root;
    if (!find_repo_root(root)) return {};
    return join_path(root, kLegacyLoginImagesRelativeDir);
}

std::string legacy_selected_image_file_utf8() {
    std::string root;
    if (!find_repo_root(root)) return {};
    return join_path(root, kLegacySelectedImageRelativeFile);
}

} // namespace

std::string join_path(const std::string& left, const std::string& right) {
    if (left.empty()) return normalize_slashes(right);
    if (right.empty()) return normalize_slashes(left);
    std::string out = left;
    if (out.back() != '/' && out.back() != '\\') out.push_back('/');
    out += right;
    return normalize_slashes(out);
}

bool find_repo_root(std::string& repo_root_utf8_out) {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return false;

    fs::path cursor = fs::path(exe_path).parent_path();
    for (int depth = 0; depth < 12; ++depth) {
        const fs::path marker = cursor / "src" / "version.txt";
        if (fs::exists(marker)) {
            repo_root_utf8_out = normalize_slashes(wide_to_utf8(cursor.wstring()));
            return true;
        }
        if (!cursor.has_parent_path()) break;
        cursor = cursor.parent_path();
    }
    return false;
}

std::string appdata_root_utf8() {
    wchar_t* local_app_data = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&local_app_data, &len, L"LOCALAPPDATA") != 0 || !local_app_data) {
        return {};
    }
    std::wstring root(local_app_data);
    free(local_app_data);
    if (root.empty()) return {};
    if (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) {
        root.pop_back();
    }
    root += L"\\";
    root += kAppDataFolderName;
    return normalize_slashes(wide_to_utf8(root));
}

std::string login_images_dir_utf8() {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, kLoginImagesSubdir);
}

std::string selected_image_file_utf8() {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, kSelectedFilename);
}

std::string last_image_prompt_file_utf8() {
    const std::string root = appdata_root_utf8();
    if (root.empty()) return {};
    return join_path(root, kLastImagePromptFilename);
}

std::string hero_art_path_utf8() {
    std::string root;
    if (!find_repo_root(root)) return {};
    return join_path(root, kHeroArtRelativePath);
}

std::vector<LoginImageEntry> list_login_images() {
    std::vector<LoginImageEntry> entries;
    const std::string dir_utf8 = login_images_dir_utf8();
    if (dir_utf8.empty()) return entries;

    const fs::path dir = fs::path(utf8_to_wide(dir_utf8));
    if (!fs::exists(dir) || !fs::is_directory(dir)) return entries;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string filename = wide_to_utf8(entry.path().filename().wstring());
        if (!ends_with_ignore_case(filename, ".jpg") && !ends_with_ignore_case(filename, ".jpeg")) {
            continue;
        }
        LoginImageEntry item{};
        item.filename = filename;
        item.absolute_path_utf8 = normalize_slashes(wide_to_utf8(entry.path().wstring()));
        entries.push_back(item);
    }

    std::sort(entries.begin(), entries.end(), [](const LoginImageEntry& a, const LoginImageEntry& b) {
        return a.filename > b.filename;
    });
    return entries;
}

bool read_selected_image(std::string& filename_out) {
    filename_out.clear();
    const std::string path = selected_image_file_utf8();
    if (path.empty()) return false;

    std::ifstream file(fs::path(utf8_to_wide(path)));
    if (!file) return false;
    std::getline(file, filename_out);
    while (!filename_out.empty() && (filename_out.back() == '\r' || filename_out.back() == '\n' || filename_out.back() == ' ')) {
        filename_out.pop_back();
    }
    return !filename_out.empty();
}

bool write_selected_image(const std::string& filename) {
    if (filename.empty()) return false;
    const std::string path = selected_image_file_utf8();
    if (path.empty()) return false;

    const fs::path file_path = fs::path(utf8_to_wide(path));
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }

    std::ofstream file(file_path, std::ios::trunc);
    if (!file) return false;
    file << filename;
    return file.good();
}

bool read_last_image_prompt(std::string& prompt_out) {
    prompt_out.clear();
    const std::string path = last_image_prompt_file_utf8();
    if (path.empty()) return false;

    std::ifstream file(fs::path(utf8_to_wide(path)), std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    prompt_out = buffer.str();
    while (!prompt_out.empty() && (prompt_out.back() == '\r' || prompt_out.back() == '\n')) {
        prompt_out.pop_back();
    }
    return true;
}

bool write_last_image_prompt(const std::string& prompt) {
    const std::string path = last_image_prompt_file_utf8();
    if (path.empty()) return false;

    const fs::path file_path = fs::path(utf8_to_wide(path));
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }

    std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(prompt.data(), static_cast<std::streamsize>(prompt.size()));
    return file.good();
}

bool sync_hero_art_from_selection() {
    std::string selected;
    if (!read_selected_image(selected)) return false;

    const std::string images_dir = login_images_dir_utf8();
    const std::string hero_path = hero_art_path_utf8();
    if (images_dir.empty() || hero_path.empty()) return false;

    const fs::path source = fs::path(utf8_to_wide(join_path(images_dir, selected)));
    const fs::path dest = fs::path(utf8_to_wide(hero_path));
    if (!fs::exists(source)) return false;

    if (dest.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
    }
    return copy_file_binary(source, dest);
}

bool ensure_login_images_seeded() {
    const std::string appdata_images = login_images_dir_utf8();
    const std::string appdata_selected = selected_image_file_utf8();
    if (appdata_images.empty() || appdata_selected.empty()) return false;

    std::error_code ec;
    fs::create_directories(fs::path(utf8_to_wide(appdata_images)), ec);

    migrate_legacy_gallery(legacy_login_images_dir_utf8(), appdata_images);
    migrate_legacy_selection(legacy_selected_image_file_utf8(), appdata_selected);

    const fs::path dir = fs::path(utf8_to_wide(appdata_images));
    const bool has_image = directory_has_jpg(dir);

    if (!has_image) {
        const std::string hero_path = hero_art_path_utf8();
        if (!hero_path.empty()) {
            const fs::path hero = fs::path(utf8_to_wide(hero_path));
            if (fs::exists(hero)) {
                const fs::path seed = dir / "login_default.jpg";
                if (copy_file_binary(hero, seed)) {
                    write_selected_image("login_default.jpg");
                }
            }
        }
    }

    std::string selected;
    if (!read_selected_image(selected)) {
        if (directory_has_jpg(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                const std::string name = wide_to_utf8(entry.path().filename().wstring());
                if (ends_with_ignore_case(name, ".jpg") || ends_with_ignore_case(name, ".jpeg")) {
                    write_selected_image(name);
                    break;
                }
            }
        }
    }

    return sync_hero_art_from_selection();
}

} // namespace ogg::assets

#else

namespace ogg::assets {

bool find_repo_root(std::string&) { return false; }
std::string join_path(const std::string& left, const std::string& right) { return left.empty() ? right : left; }
std::string appdata_root_utf8() { return {}; }
std::string login_images_dir_utf8() { return {}; }
std::string selected_image_file_utf8() { return {}; }
std::string last_image_prompt_file_utf8() { return {}; }
std::string hero_art_path_utf8() { return {}; }
std::vector<LoginImageEntry> list_login_images() { return {}; }
bool read_selected_image(std::string&) { return false; }
bool write_selected_image(const std::string&) { return false; }
bool read_last_image_prompt(std::string&) { return false; }
bool write_last_image_prompt(const std::string&) { return false; }
bool sync_hero_art_from_selection() { return false; }
bool ensure_login_images_seeded() { return false; }

} // namespace ogg::assets

#endif
