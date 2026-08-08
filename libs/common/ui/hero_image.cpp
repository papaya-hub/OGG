#if defined(_WIN32)

#include "hero_image.hpp"

#include <cstring>

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace ogg::ui {

namespace {

constexpr int kHeroArtResourceId = 102;

ULONG_PTR g_gdiplus_token = 0;
Gdiplus::Bitmap* g_hero_bitmap = nullptr;
bool g_gdiplus_started = false;

bool ensure_gdiplus() {
    if (g_gdiplus_started) return true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr) != Gdiplus::Ok) return false;
    g_gdiplus_started = true;
    return true;
}

Gdiplus::Bitmap* as_decoded_bitmap(Gdiplus::Image* image) {
    if (!image) return nullptr;
    const UINT w = image->GetWidth();
    const UINT h = image->GetHeight();
    if (w == 0 || h == 0) return nullptr;

    auto* bitmap = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }

    Gdiplus::Graphics surface(bitmap);
    surface.DrawImage(image, 0, 0, w, h);
    if (surface.GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }

    return bitmap;
}

bool load_hero_image() {
    if (g_hero_bitmap) return true;
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

    g_hero_bitmap = as_decoded_bitmap(image);
    delete image;
    return g_hero_bitmap != nullptr;
}

void paint_fallback(HDC hdc, const RECT& dest) {
    HBRUSH brush = CreateSolidBrush(RGB(42, 51, 64));
    FillRect(hdc, const_cast<RECT*>(&dest), brush);
    DeleteObject(brush);
}

} // namespace

void paint_embedded_hero_art(HDC hdc, const RECT& dest) {
    if (!load_hero_image() || !g_hero_bitmap) {
        paint_fallback(hdc, dest);
        return;
    }

    const int dest_w = dest.right - dest.left;
    const int dest_h = dest.bottom - dest.top;
    if (dest_w <= 0 || dest_h <= 0) return;

    const UINT img_w = g_hero_bitmap->GetWidth();
    const UINT img_h = g_hero_bitmap->GetHeight();
    if (img_w == 0 || img_h == 0) {
        paint_fallback(hdc, dest);
        return;
    }

    const Gdiplus::REAL dest_w_f = static_cast<Gdiplus::REAL>(dest_w);
    const Gdiplus::REAL dest_h_f = static_cast<Gdiplus::REAL>(dest_h);
    const Gdiplus::REAL img_w_f = static_cast<Gdiplus::REAL>(img_w);
    const Gdiplus::REAL img_h_f = static_cast<Gdiplus::REAL>(img_h);

    const Gdiplus::REAL scale_w = dest_w_f / img_w_f;
    const Gdiplus::REAL scale_h = dest_h_f / img_h_f;
    const Gdiplus::REAL scale = scale_w > scale_h ? scale_w : scale_h;

    const Gdiplus::REAL src_w = dest_w_f / scale;
    const Gdiplus::REAL src_h = dest_h_f / scale;
    const Gdiplus::REAL src_x = (img_w_f - src_w) / 2.f;
    const Gdiplus::REAL src_y = (img_h_f - src_h) / 2.f;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    const Gdiplus::RectF dest_rect(
        static_cast<Gdiplus::REAL>(dest.left),
        static_cast<Gdiplus::REAL>(dest.top),
        dest_w_f,
        dest_h_f
    );
    graphics.DrawImage(
        g_hero_bitmap,
        dest_rect,
        src_x,
        src_y,
        src_w,
        src_h,
        Gdiplus::UnitPixel
    );
}

} // namespace ogg::ui

#endif
