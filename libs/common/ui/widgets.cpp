#if defined(_WIN32)

#include "widgets.hpp"
#include "colors.hpp"
#include "window.hpp"

namespace ogg::ui {

D2D1_RECT_F title_close_rect(float width) {
    return D2D1::RectF(
        width - kTitleCloseBtnW,
        kChromeBtnYOffset,
        width,
        kTitleBarHeight + kChromeBtnYOffset
    );
}

D2D1_RECT_F chrome_overlay_minimize_rect() {
    return D2D1::RectF(0.f, kChromeBtnYOffset, kTitleCloseBtnW, kTitleBarHeight + kChromeBtnYOffset);
}

D2D1_RECT_F chrome_overlay_close_rect() {
    return D2D1::RectF(
        kTitleCloseBtnW,
        kChromeBtnYOffset,
        kTitleCloseBtnW * 2.f,
        kTitleBarHeight + kChromeBtnYOffset
    );
}

D2D1_RECT_F client_chrome_minimize_rect(float chrome_width) {
    (void)chrome_width;
    const float y = (kClientDragBandHeight - kClientChromeHeight) * 0.5f;
    return D2D1::RectF(
        0.f,
        y,
        kTitleCloseBtnW,
        y + kClientChromeHeight
    );
}

D2D1_RECT_F client_chrome_close_rect(float chrome_width) {
    (void)chrome_width;
    const float y = (kClientDragBandHeight - kClientChromeHeight) * 0.5f;
    return D2D1::RectF(
        kTitleCloseBtnW,
        y,
        kTitleCloseBtnW * 2.f,
        y + kClientChromeHeight
    );
}

D2D1_RECT_F action_button_rect(float width, float height, int index, int button_count) {
    const float total_w = static_cast<float>(button_count) * kActionButtonW +
        static_cast<float>(button_count - 1) * kActionButtonGap;
    const float start_x = (width - total_w) / 2.f;
    const float top = height - kActionButtonH - 14.f;
    const float left = start_x + static_cast<float>(index) * (kActionButtonW + kActionButtonGap);
    return D2D1::RectF(left, top, left + kActionButtonW, top + kActionButtonH);
}

bool point_in_rect(const POINT& pt, const D2D1_RECT_F& rect) {
    return static_cast<float>(pt.x) >= rect.left &&
           static_cast<float>(pt.x) <= rect.right &&
           static_cast<float>(pt.y) >= rect.top &&
           static_cast<float>(pt.y) <= rect.bottom;
}

int hit_server_badge_index(const ShellView& view, float width, POINT pt) {
    if (view.server_badge_labels.empty() || view.minimal_chrome) return -1;
    if (static_cast<float>(pt.y) >= kTitleBarHeight + kChromeBtnYOffset) return -1;

    constexpr float kDotRadius = 4.f;
    constexpr float kBadgeGap = 8.f;
    constexpr float kDotTextGap = 5.f;
    float cursor_x = width - kTitleCloseBtnW - 6.f;

    for (int i = static_cast<int>(view.server_badge_labels.size()) - 1; i >= 0; --i) {
        const std::wstring& label = view.server_badge_labels[static_cast<size_t>(i)];
        const float text_w = static_cast<float>(label.size()) * 6.4f + 24.f;
        const float badge_w = text_w + kDotRadius * 2.f + kDotTextGap + kBadgeGap;
        cursor_x -= badge_w;

        const D2D1_RECT_F badge_rc{
            cursor_x,
            0.f,
            cursor_x + badge_w - kBadgeGap,
            kTitleBarHeight + kChromeBtnYOffset,
        };
        if (point_in_rect(pt, badge_rc)) return i;
    }
    return -1;
}

namespace {

constexpr float kEllipsisLabelGap = 4.f;

} // namespace

void paint_shell(ID2D1HwndRenderTarget* target, const RenderContext& ctx, const ShellView& view, float width, float height) {
    const auto& brushes = ctx.brushes();
    const ShellTheme theme = ctx.theme();

    target->Clear(background_color(theme));

    D2D1_ROUNDED_RECT panel{
        D2D1::RectF(0.f, 0.f, width, height),
        kCornerRadius,
        kCornerRadius,
    };
    target->FillRoundedRectangle(panel, brushes.background);

    if (!view.minimal_chrome) {
        const D2D1_RECT_F close_rect = title_close_rect(width);
        if (view.hover_title_close) {
            target->FillRectangle(close_rect, brushes.danger);
            target->DrawText(
                L"\u00D7",
                1,
                ctx.close_text_format(),
                close_rect,
                brushes.button_on_danger_text
            );
        } else {
            target->DrawText(
                L"\u00D7",
                1,
                ctx.close_text_format(),
                close_rect,
                brushes.chrome_muted
            );
        }
    }

    if (!view.server_badge_labels.empty() && !view.minimal_chrome) {
        ID2D1SolidColorBrush* bright_green = nullptr;
        ID2D1SolidColorBrush* dull_green = nullptr;
        ID2D1SolidColorBrush* bright_orange = nullptr;
        ID2D1SolidColorBrush* dull_orange = nullptr;
        ID2D1SolidColorBrush* red = nullptr;
        target->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.92f, 0.42f, 1.f), &bright_green);
        target->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.55f, 0.24f, 1.f), &dull_green);
        target->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.72f, 0.22f, 1.f), &bright_orange);
        target->CreateSolidColorBrush(D2D1::ColorF(0.78f, 0.48f, 0.12f, 1.f), &dull_orange);
        target->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.f), &red);

        constexpr float kDotRadius = 4.f;
        constexpr float kBadgeGap = 8.f;
        constexpr float kDotTextGap = 5.f;
        float cursor_x = width - kTitleCloseBtnW - 6.f;

        IDWriteTextFormat* badge_format = ctx.button_text_format();
        for (int i = static_cast<int>(view.server_badge_labels.size()) - 1; i >= 0; --i) {
            const std::wstring& label = view.server_badge_labels[static_cast<size_t>(i)];
            const bool up = view.server_badge_up[static_cast<size_t>(i)];
            const bool polling = view.server_badge_polling[static_cast<size_t>(i)];
            const bool checking = view.server_badge_checking[static_cast<size_t>(i)];
            const bool local = view.server_badge_local[static_cast<size_t>(i)];
            const float text_w = static_cast<float>(label.size()) * 6.4f + 24.f;
            cursor_x -= text_w + kDotRadius * 2.f + kDotTextGap + kBadgeGap;

            const float dot_x = cursor_x + kDotRadius;
            const float dot_y = 14.f;
            D2D1_ELLIPSE dot{ D2D1::Point2F(dot_x, dot_y), kDotRadius, kDotRadius };
            ID2D1SolidColorBrush* dot_brush = red;
            if (checking) {
                dot_brush = view.server_badge_blink_bright ? bright_orange : dull_orange;
            } else if (up) {
                if (local) {
                    dot_brush = polling ? dull_green
                                        : (view.server_badge_blink_bright ? bright_green : dull_green);
                } else {
                    dot_brush = bright_green;
                }
            }
            target->FillEllipse(dot, dot_brush);

            const D2D1_RECT_F text_rc{
                cursor_x + kDotRadius * 2.f + kDotTextGap,
                4.f,
                cursor_x + kDotRadius * 2.f + kDotTextGap + text_w,
                24.f,
            };
            target->DrawText(
                label.c_str(),
                static_cast<UINT32>(label.size()),
                badge_format,
                text_rc,
                brushes.chrome_muted
            );
        }

        if (bright_green) bright_green->Release();
        if (dull_green) dull_green->Release();
        if (bright_orange) bright_orange->Release();
        if (dull_orange) dull_orange->Release();
        if (red) red->Release();
    }

    if (!view.minimal_chrome) {
        const float text_bottom = view.show_buttons ? height - 58.f : height - 52.f;
        D2D1_RECT_F text_rect{
            24.f,
            kTitleBarHeight + 4.f,
            width - 24.f,
            text_bottom,
        };

        if (view.status_ellipsis > 0 && ctx.ellipsis_text_format()) {
            const wchar_t dots_buf[] = L"...";
            const UINT32 dots_len = static_cast<UINT32>(view.status_ellipsis);

            const float main_w = ctx.measure_text_width(
                view.status_text.c_str(),
                static_cast<UINT32>(view.status_text.size()),
                ctx.text_format());
            const float slot_w = ctx.measure_text_width(dots_buf, 3, ctx.ellipsis_text_format());
            const float total_w = main_w + kEllipsisLabelGap + slot_w;
            const float start_x = (text_rect.left + text_rect.right - total_w) / 2.f;

            const D2D1_RECT_F main_rect{
                start_x,
                text_rect.top,
                start_x + main_w,
                text_rect.bottom,
            };
            const D2D1_RECT_F dots_rect{
                start_x + main_w + kEllipsisLabelGap,
                text_rect.top,
                start_x + main_w + kEllipsisLabelGap + slot_w,
                text_rect.bottom,
            };

            target->DrawText(
                view.status_text.c_str(),
                static_cast<UINT32>(view.status_text.size()),
                ctx.text_format(),
                main_rect,
                brushes.text
            );
            target->DrawText(
                dots_buf,
                dots_len,
                ctx.ellipsis_text_format(),
                dots_rect,
                brushes.text
            );
        } else {
            target->DrawText(
                view.status_text.c_str(),
                static_cast<UINT32>(view.status_text.size()),
                ctx.text_format(),
                text_rect,
                brushes.text
            );
        }

        if (view.progress >= 0) {
            const float bar_bottom = view.show_buttons ? height - 52.f : height - 20.f;
            const float bar_top = bar_bottom - kProgressBarHeight;
            const float bar_left = 32.f;
            const float bar_right = width - 32.f;
            const float bar_width = bar_right - bar_left;

            wchar_t percent_text[16]{};
            swprintf(percent_text, 16, L"%d%%", view.progress);
            D2D1_RECT_F percent_rect{
                bar_left,
                bar_top - kPercentLabelGap - kPercentLabelHeight,
                bar_right,
                bar_top - kPercentLabelGap,
            };
            target->DrawText(
                percent_text,
                static_cast<UINT32>(wcslen(percent_text)),
                ctx.percent_text_format(),
                percent_rect,
                brushes.text
            );

            D2D1_ROUNDED_RECT track{
                D2D1::RectF(bar_left, bar_top, bar_right, bar_bottom),
                kProgressBarCornerRadius,
                kProgressBarCornerRadius,
            };
            target->FillRoundedRectangle(track, brushes.progress_track);

            const float fill_right = bar_left + bar_width * (static_cast<float>(view.progress) / 100.f);
            if (fill_right > bar_left) {
                D2D1_ROUNDED_RECT fill{
                    D2D1::RectF(bar_left, bar_top, fill_right, bar_bottom),
                    kProgressBarCornerRadius,
                    kProgressBarCornerRadius,
                };
                target->FillRoundedRectangle(fill, brushes.primary);
            }
        }

        if (view.show_buttons && view.button_count > 0) {
            for (int i = 0; i < view.button_count; ++i) {
                const D2D1_RECT_F btn_rect = action_button_rect(width, height, i, view.button_count);
                D2D1_ROUNDED_RECT round_rect{
                    btn_rect,
                    kActionButtonRadius,
                    kActionButtonRadius,
                };
                const ButtonStyle style = view.buttons[i].style;
                target->FillRoundedRectangle(round_rect, ctx.brush_for_button_style(style));
                target->DrawText(
                    view.buttons[i].label.c_str(),
                    static_cast<UINT32>(view.buttons[i].label.size()),
                    ctx.button_text_format(),
                    btn_rect,
                    ctx.brush_for_button_text(style)
                );
            }
        }
    }
}

} // namespace ogg::ui

#endif
