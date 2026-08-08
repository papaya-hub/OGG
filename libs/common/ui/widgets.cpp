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

D2D1_RECT_F client_chrome_minimize_rect() {
    return D2D1::RectF(0.f, 0.f, kTitleCloseBtnW, kClientChromeHeight);
}

D2D1_RECT_F client_chrome_close_rect() {
    return D2D1::RectF(kTitleCloseBtnW, 0.f, kTitleCloseBtnW * 2.f, kClientChromeHeight);
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
