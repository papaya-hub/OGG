#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "client_ui.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2 || argv[1][0] == L'\0') {
        if (argv) LocalFree(argv);
        return 1;
    }

    const std::wstring url = argv[1];
    LocalFree(argv);
    return ogg::client::run_gui_with_url(url);
}
