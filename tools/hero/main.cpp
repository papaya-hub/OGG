#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <string>
#include <vector>

#include "hero_art_config.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr DWORD kResolveTimeoutMs = 15000;
constexpr DWORD kConnectTimeoutMs = 15000;
constexpr DWORD kSendTimeoutMs = 30000;
constexpr DWORD kReceiveTimeoutMs = 120000;
constexpr ULONG kJpegQuality = 90;

std::wstring utf8_to_wide(const char* text) {
    if (!text || !*text) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

bool parse_https_url(const char* url, std::wstring& host, std::wstring& path) {
    if (!url || std::strncmp(url, "https://", 8) != 0) return false;

    const char* start = url + 8;
    const char* slash = std::strchr(start, '/');
    const std::string host_utf8 = slash ? std::string(start, slash - start) : std::string(start);
    const std::string path_utf8 = slash ? std::string(slash) : "/";
    if (host_utf8.empty()) return false;

    host = utf8_to_wide(host_utf8.c_str());
    path = utf8_to_wide(path_utf8.c_str());
    return !host.empty() && !path.empty();
}

bool https_download(const std::wstring& host, const std::wstring& path, std::vector<std::uint8_t>& body) {
    body.clear();

    HINTERNET session = WinHttpOpen(
        L"OGG.HeroGen/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!session) return false;

    WinHttpSetTimeouts(session, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs);

    HINTERNET connect = WinHttpConnect(
        session,
        host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const bool sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    const bool received = sent && WinHttpReceiveResponse(request, nullptr);
    if (!received) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    );
    if (status != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::array<std::uint8_t, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            break;
        }
        if (read == 0) break;
        const std::size_t offset = body.size();
        body.resize(offset + read);
        std::memcpy(body.data() + offset, buffer.data(), read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return !body.empty();
}

int get_encoder_clsid(const WCHAR* format, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return -1;

    std::vector<std::uint8_t> buffer(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count, size, codecs);

    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(codecs[i].MimeType, format) == 0) {
            *clsid = codecs[i].Clsid;
            return 0;
        }
    }
    return -1;
}

Gdiplus::Bitmap* decode_image(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;

    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!h_mem) return nullptr;

    void* mem = GlobalLock(h_mem);
    if (!mem) {
        GlobalFree(h_mem);
        return nullptr;
    }
    std::memcpy(mem, bytes.data(), bytes.size());
    GlobalUnlock(h_mem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(h_mem, TRUE, &stream) != S_OK) {
        GlobalFree(h_mem);
        return nullptr;
    }

    auto* image = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (!image || image->GetLastStatus() != Gdiplus::Ok) {
        delete image;
        return nullptr;
    }

    const UINT w = image->GetWidth();
    const UINT h = image->GetHeight();
    if (w == 0 || h == 0) {
        delete image;
        return nullptr;
    }

    auto* bitmap = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        delete image;
        return nullptr;
    }

    Gdiplus::Graphics surface(bitmap);
    surface.DrawImage(image, 0, 0, w, h);
    delete image;

    if (surface.GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }

    return bitmap;
}

Gdiplus::Bitmap* cover_crop(Gdiplus::Bitmap* source, int dest_w, int dest_h) {
    if (!source || dest_w <= 0 || dest_h <= 0) return nullptr;

    const UINT src_w = source->GetWidth();
    const UINT src_h = source->GetHeight();
    if (src_w == 0 || src_h == 0) return nullptr;

    const Gdiplus::REAL dest_w_f = static_cast<Gdiplus::REAL>(dest_w);
    const Gdiplus::REAL dest_h_f = static_cast<Gdiplus::REAL>(dest_h);
    const Gdiplus::REAL src_w_f = static_cast<Gdiplus::REAL>(src_w);
    const Gdiplus::REAL src_h_f = static_cast<Gdiplus::REAL>(src_h);

    const Gdiplus::REAL scale_w = dest_w_f / src_w_f;
    const Gdiplus::REAL scale_h = dest_h_f / src_h_f;
    const Gdiplus::REAL scale = scale_w > scale_h ? scale_w : scale_h;

    const Gdiplus::REAL crop_w = dest_w_f / scale;
    const Gdiplus::REAL crop_h = dest_h_f / scale;
    const Gdiplus::REAL crop_x = (src_w_f - crop_w) / 2.f;
    const Gdiplus::REAL crop_y = (src_h_f - crop_h) / 2.f;

    auto* output = new Gdiplus::Bitmap(dest_w, dest_h, PixelFormat24bppRGB);
    if (!output || output->GetLastStatus() != Gdiplus::Ok) {
        delete output;
        return nullptr;
    }

    Gdiplus::Graphics graphics(output);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    const Gdiplus::RectF dest_rect(0.f, 0.f, dest_w_f, dest_h_f);
    graphics.DrawImage(
        source,
        dest_rect,
        crop_x,
        crop_y,
        crop_w,
        crop_h,
        Gdiplus::UnitPixel
    );

    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        delete output;
        return nullptr;
    }

    return output;
}

bool save_jpeg(Gdiplus::Bitmap* bitmap, const wchar_t* output_path) {
    if (!bitmap || !output_path) return false;

    CLSID jpeg_clsid{};
    if (get_encoder_clsid(L"image/jpeg", &jpeg_clsid) != 0) return false;

    ULONG quality = kJpegQuality;
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;

    const Gdiplus::Status status = bitmap->Save(output_path, &jpeg_clsid, &params);
    return status == Gdiplus::Ok;
}

} // namespace

int main(int argc, char* argv[]) {
    const char* output_path = argc > 1
        ? argv[1]
        : "src/OffGridGamer.OffGridGames.Client/hero_art.jpg";
    const char* source_url = argc > 2 ? argv[2] : ogg::hero_art::kDefaultSourceUrl;

    std::wstring host;
    std::wstring path;
    if (!parse_https_url(source_url, host, path)) {
        std::fprintf(stderr, "ogg.hero: invalid https url: %s\n", source_url);
        return 1;
    }

    std::vector<std::uint8_t> bytes;
    std::fprintf(stderr, "ogg.hero: downloading %s\n", source_url);
    if (!https_download(host, path, bytes)) {
        std::fprintf(stderr, "ogg.hero: download failed\n");
        return 1;
    }
    std::fprintf(stderr, "ogg.hero: downloaded %zu bytes\n", bytes.size());

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        std::fprintf(stderr, "ogg.hero: GDI+ startup failed\n");
        return 1;
    }

    int exit_code = 1;
    Gdiplus::Bitmap* source = decode_image(bytes);
    if (!source) {
        std::fprintf(stderr, "ogg.hero: failed to decode image\n");
    } else {
        Gdiplus::Bitmap* cropped = cover_crop(
            source,
            ogg::hero_art::kPanelWidth,
            ogg::hero_art::kPanelHeight
        );
        delete source;

        if (!cropped) {
            std::fprintf(stderr, "ogg.hero: cover crop failed\n");
        } else {
            const std::wstring output_wide = utf8_to_wide(output_path);
            if (output_wide.empty()) {
                std::fprintf(stderr, "ogg.hero: invalid output path\n");
            } else if (!save_jpeg(cropped, output_wide.c_str())) {
                std::fprintf(stderr, "ogg.hero: failed to write %s\n", output_path);
            } else {
                std::fprintf(
                    stderr,
                    "ogg.hero: wrote %s (%dx%d)\n",
                    output_path,
                    ogg::hero_art::kPanelWidth,
                    ogg::hero_art::kPanelHeight
                );
                exit_code = 0;
            }
            delete cropped;
        }
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    return exit_code;
}
