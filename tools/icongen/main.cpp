#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace {

std::vector<std::uint8_t> rgba_to_bmp(const unsigned char* rgba, int width, int height) {
    const int row_bytes = width * 4;
    const int pixel_bytes = row_bytes * height;
    std::vector<std::uint8_t> bmp(40 + pixel_bytes);

    const std::uint32_t header[11] = {
        40u,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height * 2),
        1u,
        32u,
        0u,
        static_cast<std::uint32_t>(pixel_bytes),
        0u,
        0u,
        0u,
        0u,
    };
    std::memcpy(bmp.data(), header, sizeof(header));

    for (int y = 0; y < height; ++y) {
        const int src_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const int src = (src_y * width + x) * 4;
            const int dst = 40 + y * row_bytes + x * 4;
            bmp[dst] = rgba[src + 2];
            bmp[dst + 1] = rgba[src + 1];
            bmp[dst + 2] = rgba[src];
            bmp[dst + 3] = rgba[src + 3];
        }
    }

    return bmp;
}

std::vector<std::uint8_t> rasterize_size(NSVGrasterizer* rasterizer, NSVGimage* image, int size) {
    std::vector<unsigned char> rgba(static_cast<std::size_t>(size) * size * 4);
    const float scale = static_cast<float>(size) / image->height;
    nsvgRasterize(rasterizer, image, 0, 0, scale, rgba.data(), size, size, size * 4);
    return rgba_to_bmp(rgba.data(), size, size);
}

std::vector<std::uint8_t> make_ico(const std::vector<std::vector<std::uint8_t>>& images, const int* sizes, int count) {
    std::vector<std::uint8_t> ico;
    const std::uint16_t reserved = 0;
    const std::uint16_t type = 1;
    const std::uint16_t image_count = static_cast<std::uint16_t>(count);
    ico.resize(6);
    std::memcpy(&ico[0], &reserved, sizeof(reserved));
    std::memcpy(&ico[2], &type, sizeof(type));
    std::memcpy(&ico[4], &image_count, sizeof(image_count));
    ico.resize(6 + 16 * count);

    std::uint32_t offset = static_cast<std::uint32_t>(6 + 16 * count);
    for (int i = 0; i < count; ++i) {
        const int size = sizes[i];
        const auto& bmp = images[static_cast<std::size_t>(i)];
        const std::uint8_t w = size >= 256 ? 0 : static_cast<std::uint8_t>(size);
        const std::uint8_t h = size >= 256 ? 0 : static_cast<std::uint8_t>(size);

        const std::size_t entry_offset = 6 + static_cast<std::size_t>(i) * 16;
        ico[entry_offset] = w;
        ico[entry_offset + 1] = h;
        ico[entry_offset + 2] = 0;
        ico[entry_offset + 3] = 0;

        const std::uint16_t planes = 1;
        const std::uint16_t bit_count = 32;
        const std::uint32_t bmp_size = static_cast<std::uint32_t>(bmp.size());
        std::memcpy(&ico[entry_offset + 4], &planes, sizeof(planes));
        std::memcpy(&ico[entry_offset + 6], &bit_count, sizeof(bit_count));
        std::memcpy(&ico[entry_offset + 8], &bmp_size, sizeof(bmp_size));
        std::memcpy(&ico[entry_offset + 12], &offset, sizeof(offset));
        offset += bmp_size;
    }

    for (const auto& bmp : images) {
        ico.insert(ico.end(), bmp.begin(), bmp.end());
    }

    return ico;
}

} // namespace

int main(int argc, char* argv[]) {
    const char* svg_path = argc > 1
        ? argv[1]
        : "src/OffGridGamer.OffGridGames.Launcher/icon.svg";
    const char* output_path = argc > 2
        ? argv[2]
        : "src/OffGridGamer.OffGridGames.Launcher/icon.ico";

    NSVGimage* image = nsvgParseFromFile(svg_path, "px", 96.0f);
    if (!image) {
        std::fprintf(stderr, "icongen: failed to parse %s\n", svg_path);
        return 1;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        std::fprintf(stderr, "icongen: failed to create rasterizer\n");
        nsvgDelete(image);
        return 1;
    }

    const int sizes[] = {256, 128, 64, 48, 32, 16};
    std::vector<std::vector<std::uint8_t>> images;
    for (int size : sizes) {
        images.push_back(rasterize_size(rasterizer, image, size));
    }

    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);

    const std::vector<std::uint8_t> ico = make_ico(images, sizes, 6);

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        std::fprintf(stderr, "icongen: cannot write %s\n", output_path);
        return 1;
    }

    out.write(reinterpret_cast<const char*>(ico.data()), static_cast<std::streamsize>(ico.size()));
    if (!out) {
        std::fprintf(stderr, "icongen: write failed for %s\n", output_path);
        return 1;
    }

    std::printf("icongen: %s -> %s (%zu bytes)\n", svg_path, output_path, ico.size());
    return 0;
}
