#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "client_ui.hpp"
#include "ui/shell_window.hpp"
#include "ui/window.hpp"
#include "version.hpp"
#include "http_client.hpp"

#if defined(__has_include)
#if __has_include("input_insets.hpp")
#include "input_insets.hpp"
#define OGG_HAS_INPUT_INSETS 1
#endif
#if __has_include("ui_typography.hpp")
#include "ui_typography.hpp"
#define OGG_HAS_UI_TYPOGRAPHY 1
#endif
#endif

namespace ogg::client {

namespace {

ogg::ui::InputInsets load_login_input_insets() {
    ogg::ui::InputInsets insets{};
#if defined(OGG_HAS_INPUT_INSETS)
    insets.left = ogg::ui::theme::input_inset_left();
    insets.right = ogg::ui::theme::input_inset_right();
    insets.top = ogg::ui::theme::input_inset_top();
    insets.bottom = ogg::ui::theme::input_inset_bottom();
#endif
    return insets;
}

ogg::ui::UiTypography load_login_typography() {
    ogg::ui::UiTypography typography{};
#if defined(OGG_HAS_UI_TYPOGRAPHY)
    typography.font_family = ogg::ui::theme::ui_font_family();
    typography.label_font_size = ogg::ui::theme::label_font_size();
    typography.input_font_size = ogg::ui::theme::input_font_size();
    typography.button_font_size = ogg::ui::theme::button_font_size();
#endif
    return typography;
}

float load_login_control_width() {
#if defined(OGG_HAS_UI_TYPOGRAPHY)
    return static_cast<float>(ogg::ui::theme::ui_control_width());
#else
    return 160.f;
#endif
}

float load_login_label_control_gap() {
#if defined(OGG_HAS_UI_TYPOGRAPHY)
    return static_cast<float>(ogg::ui::theme::label_control_gap());
#else
    return 6.f;
#endif
}

float load_login_scroll_wheel_step() {
#if defined(OGG_HAS_UI_TYPOGRAPHY)
    return static_cast<float>(ogg::ui::theme::ui_scroll_step());
#else
    return 4.f;
#endif
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::string read_login_xml() {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return {};

    wchar_t exe_dir[MAX_PATH]{};
    wcsncpy_s(exe_dir, exe_path, _TRUNCATE);
    for (int i = static_cast<int>(wcslen(exe_dir)) - 1; i >= 0; --i) {
        if (exe_dir[i] == L'\\' || exe_dir[i] == L'/') {
            exe_dir[i + 1] = L'\0';
            break;
        }
    }

    const std::wstring xml_path = std::wstring(exe_dir) + L"client_login.xml";
    std::ifstream file(wide_to_utf8(xml_path), std::ios::binary);
    if (!file) {
        return R"(<Layout width="320">
  <Class rule="login-panel" width="320" padding="32,28" bg_color="#ffffff" />
  <Class rule="heading" color="#1a1a1a" font_size="20" font_weight="600" margin="0,0,4,0" />
  <Class rule="subtext" color="#5a5a5a" font_size="14" margin="0,0,16,0" />
  <Class rule="field" margin="0,0,12,0" />
  <Class rule="field-label" color="#333333" font_size="12" font_weight="600" margin="0,0,6,0" />
  <Class rule="input" height="36" padding="0,12" border_color="#c8c8c8" bg_color="#ffffff" font_size="14" />
  <Class rule="btn-primary" height="38" margin="8,0,0,0" bg_color="#59bfff" hover_bg_color="#4aaef0" color="#0d1117" hover_color="#0d1117" font_size="14" font_weight="600" radius="6" />
  <Div class="login-panel">
    <Text class="heading">Sign in</Text>
    <Text class="subtext">Welcome to OffGrid Games. Enter your credentials to continue.</Text>
    <Div class="field">
      <Label class="field-label">Email</Label>
      <Input id="email" class="input" placeholder="you@example.com" />
    </Div>
    <Div class="field">
      <Label class="field-label">Password</Label>
      <Input id="password" class="input" type="password" placeholder="Password" />
    </Div>
    <Button id="login" class="btn-primary">Log in</Button>
  </Div>
</Layout>)";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

int run_gui_with_url(const std::wstring& url) {
    (void)url;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_initialized = com_hr == S_OK || com_hr == S_FALSE;

    auto window = std::make_unique<ogg::ui::ShellWindow>();

    if (!window->create(
            L"OGG.Client.Window",
            L"OGG Client",
            ogg::ui::client_window_policy(),
            ogg::ui::ShellTheme::Light,
            false)) {
        MessageBoxW(nullptr, L"Failed to create client window.", L"OGG Client", MB_ICONERROR);
        return 1;
    }

    window->view().minimal_chrome = true;
    window->view().progress = -1;
    window->show();

    window->set_login_input_insets(load_login_input_insets());
    window->set_login_typography(load_login_typography());
    window->set_login_control_width(load_login_control_width());
    window->set_login_label_control_gap(load_login_label_control_gap());
    window->set_login_scroll_wheel_step(load_login_scroll_wheel_step());
    const std::string login_xml = read_login_xml();
    if (!window->ensure_client_login_panel(login_xml.c_str())) {
        MessageBoxW(nullptr, L"Failed to load native login UI.", L"OGG Client", MB_ICONERROR);
        window->close();
        return 1;
    }

    window->ensure_client_chrome_overlay();
    const std::wstring version_label =
        ogg::http_client::to_wide(std::string(ogg::VERSION));
    window->ensure_client_version_overlay(version_label);
    window->layout_client_shell();

    MSG msg{};
    const HWND shell_hwnd = window->hwnd();
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (shell_hwnd && IsDialogMessageW(shell_hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    window->close();
    if (com_initialized) CoUninitialize();
    return static_cast<int>(msg.wParam);
}

} // namespace ogg::client
