#pragma once

#if defined(_WIN32)

#include <string>
#include <vector>

#include <d2d1.h>

#include "render.hpp"

namespace ogg::ui {

constexpr float kTitleBarHeight = 28.f;
constexpr float kTitleCloseBtnW = 32.f;
constexpr float kTitleCloseBtnH = 24.f;
constexpr float kTitleCloseMargin = 6.f;
constexpr float kChromeBtnYOffset = -4.f;
constexpr float kVersionOverlayHeight = 18.f;
constexpr float kVersionOverlayMargin = 8.f;
constexpr float kVersionOverlayMinWidth = 96.f;
constexpr float kClientLoginPanelWidth = 320.f;
constexpr float kClientDragBandHeight = 40.f;
constexpr float kClientChromeHeight = kTitleCloseBtnH;
constexpr float kClientChromeBtnYOffset = kChromeBtnYOffset;
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
    int status_ellipsis = 0;
    bool show_buttons = false;
    bool minimal_chrome = false;
    bool client_drag_full_window = false;
    bool hover_title_close = false;
    ShellButton buttons[2]{};
    int button_count = 0;
    bool server_badge_blink_bright = false;
    std::vector<std::wstring> server_badge_labels;
    std::vector<bool> server_badge_up;
    std::vector<bool> server_badge_polling;
    std::vector<bool> server_badge_checking;
    std::vector<bool> server_badge_local;
};

int hit_server_badge_index(const ShellView& view, float width, POINT pt);

D2D1_RECT_F title_close_rect(float width);
D2D1_RECT_F chrome_overlay_minimize_rect();
D2D1_RECT_F chrome_overlay_close_rect();
D2D1_RECT_F client_chrome_minimize_rect(float chrome_width);
D2D1_RECT_F client_chrome_close_rect(float chrome_width);
D2D1_RECT_F action_button_rect(float width, float height, int index, int button_count);

bool point_in_rect(const POINT& pt, const D2D1_RECT_F& rect);

void paint_shell(ID2D1HwndRenderTarget* target, const RenderContext& ctx, const ShellView& view, float width, float height);

} // namespace ogg::ui

#endif
