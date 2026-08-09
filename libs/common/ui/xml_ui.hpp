#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace ogg::ui {

struct InputInsets {
    int left = 0;
    int right = 0;
    int top = 4;
    int bottom = 0;
};

struct UiTypography {
    std::wstring font_family = L"Segoe UI";
    float label_font_size = 12.f;
    float input_font_size = 14.f;
    float button_font_size = 13.f;
};

namespace detail {
struct XmlUiImpl;
}

// Native XML layout host. Tags are Title-cased; attributes are lowercased.
class XmlUiHost {
public:
    using ButtonHandlerFn = void (*)(void* ctx, const std::wstring& id);

    XmlUiHost() = default;
    ~XmlUiHost();

    bool create(HWND parent, const char* xml);
    bool reload_xml(const char* xml);
    void destroy();
    void layout(int x, int y, int width, int height);
    void set_button_handler(ButtonHandlerFn fn, void* ctx = nullptr) {
        button_handler_fn_ = fn;
        button_handler_ctx_ = ctx;
    }
    void set_image_handler(ButtonHandlerFn fn, void* ctx = nullptr) {
        image_handler_fn_ = fn;
        image_handler_ctx_ = ctx;
    }
    using SliderHandlerFn = void (*)(void* ctx, const std::wstring& id, int value);
    void set_slider_handler(SliderHandlerFn fn, void* ctx = nullptr) {
        slider_handler_fn_ = fn;
        slider_handler_ctx_ = ctx;
    }
    void set_input_insets(InputInsets insets) { input_insets_ = insets; }
    void refresh_input_insets();
    void set_typography(UiTypography typography);
    void set_control_width(float width) { control_width_ = width; }
    void set_label_control_gap(float gap) { label_control_gap_ = gap; }
    void set_scroll_wheel_step(float step);
    void set_button_width(float width) { button_width_ = width; }
    void set_small_button_width(float width) { small_button_width_ = width; }
    using SelectHandlerFn = void (*)(void* ctx, const std::wstring& id, const std::wstring& value);
    void set_select_handler(SelectHandlerFn fn, void* ctx = nullptr) {
        select_handler_fn_ = fn;
        select_handler_ctx_ = ctx;
    }
    void set_radio_handler(SelectHandlerFn fn, void* ctx = nullptr) {
        radio_handler_fn_ = fn;
        radio_handler_ctx_ = ctx;
    }
    void set_range_handler(SliderHandlerFn fn, void* ctx = nullptr) {
        range_handler_fn_ = fn;
        range_handler_ctx_ = ctx;
    }

    std::wstring get_input_text(const std::wstring& id) const;
    void set_input_text(const std::wstring& id, const std::wstring& text);
    int get_slider_value(const std::wstring& id) const;
    void set_slider_value(const std::wstring& id, int value);
    int get_range_value(const std::wstring& id) const;
    void set_range_value(const std::wstring& id, int value);
    void set_text(const std::wstring& id, const std::wstring& text);
    void set_button_selected(const std::wstring& id, bool selected);
    void set_button_enabled(const std::wstring& id, bool enabled);
    void set_busy(bool busy, const std::wstring& near_button_id = L"");
    bool is_busy() const { return busy_; }
    void set_image_selected(const std::wstring& id);
    std::wstring get_select_value(const std::wstring& id) const;
    void set_select_value(const std::wstring& id, const std::wstring& value);
    void set_select_options(const std::wstring& id, const std::vector<std::wstring>& options, const std::wstring& selected = L"");
    std::wstring get_radio_value(const std::wstring& id) const;
    void set_radio_value(const std::wstring& id, const std::wstring& value);

    HWND hwnd() const { return hwnd_; }

private:
    detail::XmlUiImpl* impl_ = nullptr;
    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    ButtonHandlerFn button_handler_fn_ = nullptr;
    void* button_handler_ctx_ = nullptr;
    ButtonHandlerFn image_handler_fn_ = nullptr;
    void* image_handler_ctx_ = nullptr;
    SliderHandlerFn slider_handler_fn_ = nullptr;
    void* slider_handler_ctx_ = nullptr;
    SelectHandlerFn select_handler_fn_ = nullptr;
    void* select_handler_ctx_ = nullptr;
    SelectHandlerFn radio_handler_fn_ = nullptr;
    void* radio_handler_ctx_ = nullptr;
    SliderHandlerFn range_handler_fn_ = nullptr;
    void* range_handler_ctx_ = nullptr;
    InputInsets input_insets_{};
    UiTypography typography_{};
    float control_width_ = 320.f;
    float label_control_gap_ = 6.f;
    float scroll_wheel_step_ = 25.f;
    float button_width_ = 140.f;
    float small_button_width_ = 80.f;
    bool busy_ = false;
    float busy_angle_ = 0.f;
    std::wstring busy_near_button_id_;
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
};

} // namespace ogg::ui

#endif
