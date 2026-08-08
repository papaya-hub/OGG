#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <string>

namespace ogg::ui {

namespace detail {
struct XmlUiImpl;
}

// Native XML layout host. Tags are Title-cased; attributes are lowercased.
class XmlUiHost {
public:
    using ButtonHandler = std::function<void(const std::wstring& id)>;

    XmlUiHost() = default;
    ~XmlUiHost();

    bool create(HWND parent, const char* xml);
    void destroy();
    void layout(int x, int y, int width, int height);
    void set_button_handler(ButtonHandler handler) { button_handler_ = std::move(handler); }

    HWND hwnd() const { return hwnd_; }

private:
    detail::XmlUiImpl* impl_ = nullptr;
    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    ButtonHandler button_handler_;
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
};

} // namespace ogg::ui

#endif
