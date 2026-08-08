#pragma once

#if defined(_WIN32)

#include <windows.h>

namespace ogg::ui {

// Paints embedded hero JPEG with cover crop (no stretch) into dest.
void paint_embedded_hero_art(HDC hdc, const RECT& dest);

} // namespace ogg::ui

#endif
