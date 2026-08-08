#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "launcher_ui.hpp"

#include <cstdio>
#include <string>

namespace ogg::launcher {

class CliUi final : public LauncherUi {
public:
    void log(const std::string& message) override {
        message_ = "[Launcher] " + message;
        ellipsis_ = 0;
        render_status_line();
    }

    void set_progress(int percent) override {
        progress_ = percent;
        render_status_line();
    }

    void set_ellipsis_dots(int count) override {
        ellipsis_ = count < 0 ? 0 : (count > 3 ? 3 : count);
        render_status_line();
    }

    void pump() override {}

    void close() override {
        if (!message_.empty()) {
            std::printf("\n");
            std::fflush(stdout);
        }
    }

    void run() override {}

    void show_error_actions(bool) override {}

    GuiUserAction wait_for_user_action() override {
        return GuiUserAction::Close;
    }

private:
    void render_status_line() {
        std::string line = message_;
        if (ellipsis_ > 0) {
            line += std::string(static_cast<std::size_t>(ellipsis_), '.');
            line += std::string(static_cast<std::size_t>(3 - ellipsis_), ' ');
        }
        if (progress_ >= 0) {
            line += " (" + std::to_string(progress_) + "%)";
        }

        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD console_mode = 0;
        if (out != nullptr && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &console_mode)) {
            DWORD written = 0;
            const std::string padded = "\r" + line + "   ";
            WriteConsoleA(out, padded.c_str(), static_cast<DWORD>(padded.size()), &written, nullptr);
        } else {
            std::fprintf(stdout, "%s\n", line.c_str());
            std::fflush(stdout);
        }
    }

    std::string message_;
    int progress_ = -1;
    int ellipsis_ = 0;
};

std::unique_ptr<LauncherUi> create_cli_ui() {
    return std::make_unique<CliUi>();
}

bool is_cli_mode() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool cli = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"-cli") == 0 || _wcsicmp(argv[i], L"--cli") == 0) {
            cli = true;
            break;
        }
    }

    LocalFree(argv);
    return cli;
}

} // namespace ogg::launcher
