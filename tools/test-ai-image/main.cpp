#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "ai_image.hpp"
#include "app_settings.hpp"

namespace {

bool write_jpeg(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool generate_one(
    ogg::settings::AppSettings settings,
    const char* provider,
    const std::string& prompt,
    const std::string& out_path
) {
    settings.ai_provider = provider;
    const auto result = ogg::ai_image::generate_login_hero(settings, prompt);
    if (!result.ok) {
        std::fprintf(stderr, "%s: %s\n", provider, result.message.c_str());
        return false;
    }
    if (!write_jpeg(out_path, result.jpeg_bytes)) {
        std::fprintf(stderr, "%s: failed to write %s\n", provider, out_path.c_str());
        return false;
    }
    std::printf("%s: ok -> %s\n", provider, out_path.c_str());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* prompt = "five stax gaming party";
    if (argc > 1 && argv[1] && argv[1][0]) prompt = argv[1];

    ogg::settings::AppSettings settings{};
    if (!ogg::settings::load(settings)) {
        std::fprintf(stderr, "Failed to load settings.\n");
        return 1;
    }

    const std::string out_dir = "build/test_ai_images";
    CreateDirectoryA(out_dir.c_str(), nullptr);

    const std::string openai_path = out_dir + "/openai_five_stax.jpg";
    const std::string gemini_path = out_dir + "/gemini_five_stax.jpg";

    bool ok = true;
    if (settings.openai_api_key.empty()) {
        std::fprintf(stderr, "openai: no key configured\n");
        ok = false;
    } else {
        ok = generate_one(settings, "openai", prompt, openai_path) && ok;
    }

    if (settings.gemini_api_key.empty()) {
        std::fprintf(stderr, "gemini: no key configured\n");
        ok = false;
    } else {
        ok = generate_one(settings, "gemini", prompt, gemini_path) && ok;
    }

    return ok ? 0 : 1;
}
