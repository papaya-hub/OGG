#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>
#include <string>

#include "client_ui.hpp"
#include "ui/shell_window.hpp"
#include "ui/webview_host.hpp"
#include "ui/window.hpp"
#include "version.hpp"
#include "http_client.hpp"

namespace ogg::client {

int run_gui_with_url(const std::wstring& url) {
    if (url.empty()) return 1;

    auto window = std::make_unique<ogg::ui::ShellWindow>();

    if (!window->create(
            L"OGG.Client.Window",
            L"OGG Client",
            ogg::ui::client_window_policy(),
            ogg::ui::ShellTheme::Light,
            true)) {
        MessageBoxW(nullptr, L"Failed to create client window.", L"OGG Client", MB_ICONERROR);
        return 1;
    }

    window->view().minimal_chrome = true;
    window->view().progress = -1;
    window->show();

    HWND hwnd = window->hwnd();
    if (!ogg::ui::embed_webview(hwnd, url, [&window]() {
            window->layout_client_shell();
            window->bring_client_overlays_to_front();
            window->fade_in_now();
        })) {
        MessageBoxW(
            nullptr,
            L"Failed to start WebView2.\n\n"
            L"Install the WebView2 Runtime (Evergreen) from:\n"
            L"https://developer.microsoft.com/microsoft-edge/webview2/",
            L"OGG Client",
            MB_ICONERROR
        );
        window->close();
        return 1;
    }
    window->ensure_client_chrome_overlay();
    const std::wstring version_label =
        L"v" + ogg::http_client::to_wide(std::string(ogg::VERSION));
    window->ensure_client_version_overlay(version_label);
    window->layout_client_shell();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ogg::ui::destroy_embedded_webview();
    window->close();
    return static_cast<int>(msg.wParam);
}

} // namespace ogg::client
