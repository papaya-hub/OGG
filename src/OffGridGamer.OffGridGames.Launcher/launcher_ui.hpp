#pragma once

#include <memory>
#include <string>

namespace ogg::launcher {

enum class GuiUserAction {
    None,
    Retry,
    Close,
};

class LauncherUi {
public:
    virtual ~LauncherUi() = default;
    virtual void log(const std::string& message) = 0;
    virtual void set_progress(int percent) = 0;
    virtual void set_ellipsis_dots(int count) = 0;
    virtual void pump() = 0;
    virtual void close() = 0;
    virtual void run() = 0;
    virtual void show_error_actions(bool visible) = 0;
    virtual GuiUserAction wait_for_user_action() = 0;
};

std::unique_ptr<LauncherUi> create_cli_ui();
std::unique_ptr<LauncherUi> create_gui_ui();

bool is_cli_mode();

} // namespace ogg::launcher
