#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <string>

namespace ogg::ui {

bool embed_webview(HWND parent, const std::wstring& url, std::function<void()> on_loaded);
void layout_embedded_webview(HWND parent);
void destroy_embedded_webview();

} // namespace ogg::ui

#endif
