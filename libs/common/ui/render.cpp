#if defined(_WIN32)

#include "render.hpp"
#include "colors.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace ogg::ui {

RenderContext::~RenderContext() {
    release_target();
    if (close_text_format_) close_text_format_->Release();
    if (percent_text_format_) percent_text_format_->Release();
    if (button_text_format_) button_text_format_->Release();
    if (text_format_) text_format_->Release();
    if (write_factory_) write_factory_->Release();
    if (factory_) factory_->Release();
}

bool RenderContext::init() {
    if (factory_) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) return false;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&write_factory_)))) {
        return false;
    }
    return create_text_formats();
}

bool RenderContext::attach(HWND hwnd) {
    if (!init() || !hwnd) return false;
    hwnd_ = hwnd;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_SIZE_U size{
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top),
    };

    if (target_) {
        target_->Resize(size);
        return true;
    }

    HRESULT hr = factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        &target_
    );
    if (FAILED(hr)) return false;

    return create_brushes();
}

void RenderContext::resize() {
    if (!target_ || !hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    target_->Resize(D2D1_SIZE_U{
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top),
    });
}

void RenderContext::release_target() {
    release_brushes();
    if (target_) {
        target_->Release();
        target_ = nullptr;
    }
    hwnd_ = nullptr;
}

void RenderContext::release_brushes() {
    if (brushes_.button_on_danger_text) brushes_.button_on_danger_text->Release();
    if (brushes_.button_on_secondary_text) brushes_.button_on_secondary_text->Release();
    if (brushes_.button_on_primary_text) brushes_.button_on_primary_text->Release();
    if (brushes_.danger) brushes_.danger->Release();
    if (brushes_.secondary) brushes_.secondary->Release();
    if (brushes_.primary) brushes_.primary->Release();
    if (brushes_.progress_track) brushes_.progress_track->Release();
    if (brushes_.text) brushes_.text->Release();
    if (brushes_.background) brushes_.background->Release();
    brushes_ = {};
}

bool RenderContext::create_brushes() {
    if (!target_) return false;
    release_brushes();
    target_->CreateSolidColorBrush(background_color(theme_), &brushes_.background);
    target_->CreateSolidColorBrush(text_color(theme_), &brushes_.text);
    target_->CreateSolidColorBrush(progress_track_color(theme_), &brushes_.progress_track);
    target_->CreateSolidColorBrush(primary_color(), &brushes_.primary);
    target_->CreateSolidColorBrush(secondary_color(), &brushes_.secondary);
    target_->CreateSolidColorBrush(danger_color(), &brushes_.danger);
    target_->CreateSolidColorBrush(button_on_primary_text_color(), &brushes_.button_on_primary_text);
    target_->CreateSolidColorBrush(button_on_secondary_text_color(), &brushes_.button_on_secondary_text);
    target_->CreateSolidColorBrush(button_on_danger_text_color(), &brushes_.button_on_danger_text);
    return brushes_.background && brushes_.text && brushes_.progress_track && brushes_.primary &&
           brushes_.secondary && brushes_.danger && brushes_.button_on_primary_text &&
           brushes_.button_on_secondary_text && brushes_.button_on_danger_text;
}

bool RenderContext::create_text_formats() {
    if (!write_factory_) return false;

    HRESULT hr = write_factory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.f,
        L"en-us",
        &text_format_
    );
    if (text_format_) {
        text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    hr = write_factory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.f,
        L"en-us",
        &button_text_format_
    );
    if (button_text_format_) {
        button_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        button_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    hr = write_factory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.f,
        L"en-us",
        &percent_text_format_
    );
    if (percent_text_format_) {
        percent_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        percent_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    }

    hr = write_factory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        26.f,
        L"en-us",
        &close_text_format_
    );
    if (close_text_format_) {
        close_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        close_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return text_format_ && button_text_format_ && percent_text_format_ && close_text_format_;
}

void RenderContext::set_theme(ShellTheme theme) {
    theme_ = theme;
    if (target_) create_brushes();
}

ID2D1SolidColorBrush* RenderContext::brush_for_button_style(ButtonStyle style) const {
    switch (style) {
    case ButtonStyle::Primary: return brushes_.primary;
    case ButtonStyle::Secondary: return brushes_.secondary;
    case ButtonStyle::Danger: return brushes_.danger;
    }
    return brushes_.secondary;
}

ID2D1SolidColorBrush* RenderContext::brush_for_button_text(ButtonStyle style) const {
    switch (style) {
    case ButtonStyle::Primary: return brushes_.button_on_primary_text;
    case ButtonStyle::Secondary: return brushes_.button_on_secondary_text;
    case ButtonStyle::Danger: return brushes_.button_on_danger_text;
    }
    return brushes_.button_on_secondary_text;
}

} // namespace ogg::ui

#endif
