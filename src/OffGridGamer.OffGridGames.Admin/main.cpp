#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "admin_ui.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return ogg::admin::run_gui();
}
