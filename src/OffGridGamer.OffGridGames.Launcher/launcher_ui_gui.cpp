#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <memory>
#include <string>

#include "launcher_ui.hpp"
#include "ui/shell_window.hpp"

namespace ogg::launcher {

namespace {

class GuiUi final : public LauncherUi {
public:
    explicit GuiUi(std::unique_ptr<ogg::ui::ShellWindow> window)
        : window_(std::move(window)) {}

    void log(const std::string& message) override {
        if (!window_) return;
        window_->post_status_utf8(message);
        pump();
    }

    void set_progress(int percent) override {
        if (!window_) return;
        window_->post_progress(percent);
        pump();
    }

    void pump() override {
        if (window_) window_->pump_messages();
    }

    void close() override {
        if (window_) window_->close();
    }

    void run() override {}

    void show_error_actions(bool visible) override {
        if (!window_) return;
        window_->post_show_buttons(visible);
        pump();
    }

    GuiUserAction wait_for_user_action() override {
        if (!window_) return GuiUserAction::Close;
        const ogg::ui::ShellUserAction action = window_->wait_for_action();
        if (action == ogg::ui::ShellUserAction::Button0) return GuiUserAction::Retry;
        return GuiUserAction::Close;
    }

private:
    std::unique_ptr<ogg::ui::ShellWindow> window_;
};

} // namespace

std::unique_ptr<LauncherUi> create_gui_ui() {
    auto window = std::make_unique<ogg::ui::ShellWindow>();

    const ogg::ui::ShellButton buttons[] = {
        { L"Retry", ogg::ui::ButtonStyle::Primary },
        { L"Close", ogg::ui::ButtonStyle::Secondary },
    };
    window->set_action_buttons(buttons, 2);

    if (!window->create(L"OGG.Launcher.Window", L"OGG Launcher", ogg::ui::launcher_window_policy())) {
        MessageBoxW(nullptr, L"Failed to create launcher window.", L"OGG Launcher", MB_ICONERROR);
        return nullptr;
    }

    window->set_status(L"OGG Launcher");
    window->set_progress(0);
    window->show();

    return std::make_unique<GuiUi>(std::move(window));
}

} // namespace ogg::launcher
