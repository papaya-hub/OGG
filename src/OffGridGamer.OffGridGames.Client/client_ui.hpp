#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <functional>
#include <string>

namespace ogg::client {

int run_gui_with_url(const std::wstring& url);

} // namespace ogg::client

#endif
