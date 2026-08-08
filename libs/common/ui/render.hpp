#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include "colors.hpp"

namespace ogg::ui {

enum class ButtonStyle {
    Primary,
    Secondary,
    Danger,
};

struct RenderBrushes {
    ID2D1SolidColorBrush* background = nullptr;
    ID2D1SolidColorBrush* text = nullptr;
    ID2D1SolidColorBrush* progress_track = nullptr;
    ID2D1SolidColorBrush* primary = nullptr;
    ID2D1SolidColorBrush* secondary = nullptr;
    ID2D1SolidColorBrush* danger = nullptr;
    ID2D1SolidColorBrush* button_on_primary_text = nullptr;
    ID2D1SolidColorBrush* button_on_secondary_text = nullptr;
    ID2D1SolidColorBrush* button_on_danger_text = nullptr;
    ID2D1SolidColorBrush* chrome_muted = nullptr;
    ID2D1SolidColorBrush* chrome_hover_grey = nullptr;
};

class RenderContext {
public:
    RenderContext() = default;
    ~RenderContext();

    bool init();
    bool attach(HWND hwnd);
    void resize();
    void release_target();

    ID2D1HwndRenderTarget* target() const { return target_; }
    IDWriteTextFormat* text_format() const { return text_format_; }
    IDWriteTextFormat* ellipsis_text_format() const { return ellipsis_text_format_; }
    IDWriteTextFormat* button_text_format() const { return button_text_format_; }
    IDWriteTextFormat* percent_text_format() const { return percent_text_format_; }
    IDWriteTextFormat* close_text_format() const { return close_text_format_; }
    ShellTheme theme() const { return theme_; }

    void set_theme(ShellTheme theme);

    bool set_status_font(const wchar_t* family, float size_pt);

    float measure_text_width(const wchar_t* text, UINT32 len, IDWriteTextFormat* format) const;

    ID2D1SolidColorBrush* brush_for_button_style(ButtonStyle style) const;
    ID2D1SolidColorBrush* brush_for_button_text(ButtonStyle style) const;
    const RenderBrushes& brushes() const { return brushes_; }

private:
    void release_brushes();
    bool create_brushes();
    bool create_text_formats();

    ID2D1Factory* factory_ = nullptr;
    IDWriteFactory* write_factory_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
    IDWriteTextFormat* ellipsis_text_format_ = nullptr;
    IDWriteTextFormat* button_text_format_ = nullptr;
    IDWriteTextFormat* percent_text_format_ = nullptr;
    IDWriteTextFormat* close_text_format_ = nullptr;
    RenderBrushes brushes_{};
    ShellTheme theme_ = ShellTheme::Dark;
    HWND hwnd_ = nullptr;
};

} // namespace ogg::ui

#endif
