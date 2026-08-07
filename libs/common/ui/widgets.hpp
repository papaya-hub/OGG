#pragma once

#if defined(_WIN32)

#include <string>

#include <d2d1.h>

#include "render.hpp"

namespace ogg::ui {

constexpr float kTitleBarHeight = 28.f;
constexpr float kTitleCloseBtnW = 32.f;
constexpr float kTitleCloseBtnH = 24.f;
constexpr float kTitleCloseMargin = 6.f;
constexpr float kProgressBarHeight = 2.f;
constexpr float kProgressBarCornerRadius = 1.f;
constexpr float kPercentLabelHeight = 14.f;
constexpr float kPercentLabelGap = 5.f;
constexpr float kActionButtonW = 108.f;
constexpr float kActionButtonH = 30.f;
constexpr float kActionButtonGap = 14.f;
constexpr float kActionButtonRadius = 6.f;

struct ShellButton {
    std::wstring label;
    ButtonStyle style = ButtonStyle::Secondary;
};

struct ShellView {
    std::wstring status_text;
    int progress = -1;
    bool show_buttons = false;
    bool minimal_chrome = false;
    ShellButton buttons[2]{};
    int button_count = 0;
};

D2D1_RECT_F title_close_rect(float width);
D2D1_RECT_F client_title_close_rect(float width);
D2D1_RECT_F client_title_minimize_rect(float width);
D2D1_RECT_F action_button_rect(float width, float height, int index, int button_count);

bool point_in_rect(const POINT& pt, const D2D1_RECT_F& rect);

void paint_shell(ID2D1HwndRenderTarget* target, const RenderContext& ctx, const ShellView& view, float width, float height);

} // namespace ogg::ui

#endif
