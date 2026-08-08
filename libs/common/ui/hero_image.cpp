#if defined(_WIN32)

#include "hero_image.hpp"

#include <cstring>

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace ogg::ui {

namespace {

constexpr int kHeroArtResourceId = 102;

ULONG_PTR g_gdiplus_token = 0;
Gdiplus::Image* g_hero_image = nullptr;
bool g_gdiplus_started = false;

bool ensure_gdiplus() {
    if (g_gdiplus_started) return true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr) != Gdiplus::Ok) return false;
    g_gdiplus_started = true;
    return true;
}

bool load_hero_image() {
    if (g_hero_image) return true;
    if (!ensure_gdiplus()) return false;

    const HRSRC resource = FindResourceW(
        nullptr,
        MAKEINTRESOURCEW(kHeroArtResourceId),
        MAKEINTRESOURCEW(10)
    );
    if (!resource) return false;

    const HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) return false;

    const void* data = LockResource(loaded);
    const DWORD size = SizeofResource(nullptr, resource);
    if (!data || size == 0) return false;

    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!h_mem) return false;

    void* mem = GlobalLock(h_mem);
    if (!mem) {
        GlobalFree(h_mem);
        return false;
    }
    memcpy(mem, data, size);
    GlobalUnlock(h_mem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(h_mem, TRUE, &stream) != S_OK) {
        GlobalFree(h_mem);
        return false;
    }

    auto* image = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (!image || image->GetLastStatus() != Gdiplus::Ok) {
        delete image;
        return false;
    }

    g_hero_image = image;
    return true;
}

void paint_fallback(HDC hdc, const RECT& dest) {
    HBRUSH brush = CreateSolidBrush(RGB(42, 51, 64));
    FillRect(hdc, const_cast<RECT*>(&dest), brush);
    DeleteObject(brush);
}

} // namespace

void paint_embedded_hero_art(HDC hdc, const RECT& dest) {
    if (!load_hero_image() || !g_hero_image) {
        paint_fallback(hdc, dest);
        return;
    }

    const int dest_w = dest.right - dest.left;
    const int dest_h = dest.bottom - dest.top;
    if (dest_w <= 0 || dest_h <= 0) return;

    const UINT img_w = g_hero_image->GetWidth();
    const UINT img_h = g_hero_image->GetHeight();
    if (img_w == 0 || img_h == 0) {
        paint_fallback(hdc, dest);
        return;
    }

    const float scale_w = static_cast<float>(dest_w) / static_cast<float>(img_w);
    const float scale_h = static_cast<float>(dest_h) / static_cast<float>(img_h);
    const float scale = scale_w > scale_h ? scale_w : scale_h;

    const float src_w = static_cast<float>(dest_w) / scale;
    const float src_h = static_cast<float>(dest_h) / scale;
    const float src_x = (static_cast<float>(img_w) - src_w) / 2.f;
    const float src_y = (static_cast<float>(img_h) - src_h) / 2.f;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    const Gdiplus::Rect dest_rect(dest.left, dest.top, dest_w, dest_h);
    graphics.DrawImage(
        g_hero_image,
        dest_rect,
        static_cast<INT>(src_x),
        static_cast<INT>(src_y),
        static_cast<INT>(src_w),
        static_cast<INT>(src_h),
        Gdiplus::UnitPixel
    );
}

} // namespace ogg::ui

#endif
