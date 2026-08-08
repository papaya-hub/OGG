#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

#include "http_client.hpp"
#include "launcher_ui.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kProgressPerServerTry = 5;
constexpr int kProgressPatchReady = 25;
constexpr int kProgressDownloadSpan = 50;
constexpr int kProgressReadyToLaunch = 75;
constexpr int kProgressDone = 100;
constexpr DWORD kLaunchDelayMs = 300;
constexpr DWORD kStateTransitionDelayMs = 200;
constexpr DWORD kDotIntervalMs = 500;

} // namespace

enum class WorkflowResult {
    Ready,
    NetworkError,
    LaunchFailed,
};

static fs::path get_exe_directory() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

static fs::path get_client_data_directory() {
    wchar_t local_app_data[MAX_PATH] = {0};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return get_exe_directory() / "OffGridGames";
    }
    return fs::path(local_app_data) / "OffGridGames";
}

static bool ensure_client_data_directory(const fs::path& client_dir) {
    std::error_code ec;
    fs::create_directories(client_dir, ec);
    return !ec;
}

static fs::path find_local_client_match(const fs::path& client_dir, const std::string& remote_name) {
    const std::string remote_base = ogg::http_client::basename(remote_name);
    for (const auto& entry : fs::directory_iterator(client_dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string local_name = entry.path().filename().string();
        if (local_name.starts_with("ogg.client.") && local_name.ends_with(".exe")) {
            if (local_name == remote_base) {
                return entry.path();
            }
        }
    }
    return {};
}

static fs::path resolve_client_path(const fs::path& client_dir, const std::string& remote_base) {
    const fs::path adjacent = get_exe_directory() / remote_base;
    if (fs::is_regular_file(adjacent)) {
        return adjacent;
    }

    const fs::path cached = find_local_client_match(client_dir, remote_base);
    if (!cached.empty()) {
        return cached;
    }

    return client_dir / remote_base;
}

static void status(ogg::launcher::LauncherUi& ui, int percent, const std::string& message) {
    ui.set_ellipsis_dots(0);
    ui.set_progress(percent);
    ui.log(message);
    ui.pump();
}

static void hold_ui(ogg::launcher::LauncherUi& ui, DWORD duration_ms = kStateTransitionDelayMs) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < duration_ms) {
        ui.pump();
        Sleep(16);
    }
}

static void animate_status(
    ogg::launcher::LauncherUi& ui,
    int percent,
    const std::string& prefix,
    const std::function<bool()>& continue_animating
) {
    int dot_count = 1;
    ui.set_progress(percent);
    ui.log(prefix);
    ui.set_ellipsis_dots(dot_count);
    DWORD last_tick = GetTickCount();

    while (continue_animating()) {
        ui.pump();
        const DWORD now = GetTickCount();
        if (now - last_tick >= kDotIntervalMs) {
            last_tick = now;
            dot_count = (dot_count % 3) + 1;
            ui.set_progress(percent);
            ui.set_ellipsis_dots(dot_count);
        }
        Sleep(16);
    }

    ui.set_ellipsis_dots(0);
}

static bool probe_host_with_connecting_animation(
    ogg::launcher::LauncherUi& ui,
    int percent,
    const wchar_t* host,
    std::uint16_t port,
    int& patch_status,
    std::vector<std::uint8_t>& patch_body
) {
    std::atomic<bool> done{false};
    bool patch_ok = false;

    std::thread patch_worker([&] {
        patch_ok = ogg::http_client::http_get(host, port, L"/api/ogg/patch", patch_status, patch_body);
        done.store(true, std::memory_order_release);
    });

    animate_status(ui, percent, "Connecting", [&] {
        return !done.load(std::memory_order_acquire);
    });

    patch_worker.join();
    return patch_ok;
}

static void launch_delay_with_animation(ogg::launcher::LauncherUi& ui) {
    const DWORD start = GetTickCount();
    animate_status(ui, kProgressDone, "Launching", [&] {
        return GetTickCount() - start < kLaunchDelayMs;
    });
}

static bool launch_client(const fs::path& client_path, ogg::launcher::LauncherUi& ui) {
    if (!fs::is_regular_file(client_path)) {
        status(ui, -1, "Client executable not found.");
        return false;
    }

    hold_ui(ui);
    launch_delay_with_animation(ui);

    std::wstring command_line = L"\"" + client_path.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::vector<wchar_t> command(command_line.begin(), command_line.end());
    command.push_back(L'\0');

    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        status(ui, -1, "Failed to start client (error " + std::to_string(GetLastError()) + ")");
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

struct HostEntry {
    const wchar_t* host;
    std::uint16_t port;
};

static WorkflowResult prepare_client(ogg::launcher::LauncherUi& ui, fs::path& client_path) {
    const HostEntry hosts[] = {
        { L"127.0.0.1", 8123 },
        { L"localhost", 8123 },
        { L"ogg.sendermesh.com", 8123 },
        { L"in.msheriff.com", 8123 },
    };

    const fs::path client_dir = get_client_data_directory();
    if (!ensure_client_data_directory(client_dir)) {
        status(ui, -1, "Could not create client data directory.");
        return WorkflowResult::NetworkError;
    }
    ui.show_error_actions(false);
    ui.set_progress(0);
    ui.pump();

    std::string remote_name;
    std::wstring active_host;
    std::uint16_t active_port = 8123;
    std::string last_error = "No patch server reachable.";

    const int host_count = static_cast<int>(sizeof(hosts) / sizeof(hosts[0]));
    for (int i = 0; i < host_count; ++i) {
        const auto& entry = hosts[i];
        const int try_progress = (i + 1) * kProgressPerServerTry;

        int patch_status = 0;
        std::vector<std::uint8_t> patch_body;
        if (!probe_host_with_connecting_animation(
                ui, try_progress, entry.host, entry.port, patch_status, patch_body)) {
            last_error = "Connection failed.";
            status(ui, try_progress, last_error);
            continue;
        }
        if (patch_status != 200) {
            last_error = "HTTP " + std::to_string(patch_status);
            status(ui, try_progress, last_error);
            continue;
        }

        remote_name = ogg::http_client::trim(ogg::http_client::body_as_string(patch_body));
        if (remote_name.empty()) {
            last_error = "Empty patch response.";
            status(ui, try_progress, last_error);
            continue;
        }

        active_host = entry.host;
        active_port = entry.port;
        hold_ui(ui);
        status(ui, kProgressPatchReady, "Patch found.");
        hold_ui(ui);
        break;
    }

    if (remote_name.empty()) {
        status(ui, -1, last_error);
        return WorkflowResult::NetworkError;
    }

    const std::string remote_base = ogg::http_client::basename(remote_name);
    client_path = resolve_client_path(client_dir, remote_base);
    const bool has_local = fs::is_regular_file(client_path);
    const bool using_adjacent = has_local && fs::equivalent(client_path, get_exe_directory() / remote_base);

    if (!has_local || using_adjacent) {
        if (using_adjacent) {
            status(ui, kProgressReadyToLaunch, "Using local client.");
            hold_ui(ui);
            return WorkflowResult::Ready;
        }

        client_path = client_dir / remote_base;
        const std::wstring download_path = L"/" + ogg::http_client::to_wide(remote_base);
        status(ui, kProgressPatchReady, "Downloading client...");

        const bool downloaded = ogg::http_client::download_file(
            active_host,
            active_port,
            download_path,
            client_path,
            [&](std::size_t received, std::size_t total) {
                if (total > 0) {
                    const int pct = kProgressPatchReady +
                        static_cast<int>((received * kProgressDownloadSpan) / total);
                    ui.set_progress(pct);
                }
                ui.pump();
            }
        );

        if (!downloaded) {
            status(ui, -1, "Download failed.");
            return WorkflowResult::NetworkError;
        }

        status(ui, kProgressReadyToLaunch, "Download complete.");
        hold_ui(ui);
    } else {
        status(ui, kProgressReadyToLaunch, "Client up to date.");
        hold_ui(ui);
    }

    return WorkflowResult::Ready;
}

static int finish(std::unique_ptr<ogg::launcher::LauncherUi>& ui, int code) {
    ui->close();
    return code;
}

static int launcher_main_gui(std::unique_ptr<ogg::launcher::LauncherUi>& ui) {
    status(*ui, 0, std::string("OGG Launcher ") + ogg::VERSION);

    while (true) {
        fs::path client_path;
        WorkflowResult workflow = WorkflowResult::NetworkError;
        std::atomic<bool> worker_done{false};

        std::thread worker([&] {
            workflow = prepare_client(*ui, client_path);
            worker_done.store(true, std::memory_order_release);
        });

        while (!worker_done.load(std::memory_order_acquire)) {
            ui->pump();
        }
        worker.join();

        if (workflow == WorkflowResult::Ready) {
            if (launch_client(client_path, *ui)) {
                ui->pump();
                return finish(ui, 0);
            }
            ui->set_progress(-1);
            ui->pump();
        }

        ui->show_error_actions(true);
        const ogg::launcher::GuiUserAction action = ui->wait_for_user_action();
        ui->show_error_actions(false);

        if (action != ogg::launcher::GuiUserAction::Retry) {
            return finish(ui, 1);
        }
    }
}

static int launcher_main() {
    const bool cli_mode = ogg::launcher::is_cli_mode();
    if (cli_mode) {
        ogg::http_client::attach_stdio_console();
    } else {
        FreeConsole();
    }

    std::unique_ptr<ogg::launcher::LauncherUi> ui =
        cli_mode ? ogg::launcher::create_cli_ui() : ogg::launcher::create_gui_ui();
    if (!ui) return 1;

    if (cli_mode) {
        status(*ui, 0, std::string("OGG Launcher ") + ogg::VERSION);

        while (true) {
            fs::path client_path;
            const WorkflowResult workflow = prepare_client(*ui, client_path);

            if (workflow == WorkflowResult::Ready) {
                if (launch_client(client_path, *ui)) {
                    ui->pump();
                    return finish(ui, 0);
                }
                ui->set_progress(-1);
                ui->pump();
            }

            ui->show_error_actions(true);
            const ogg::launcher::GuiUserAction action = ui->wait_for_user_action();
            ui->show_error_actions(false);

            if (action != ogg::launcher::GuiUserAction::Retry) {
                return finish(ui, 1);
            }
        }
    }

    return launcher_main_gui(ui);
}

int main(int, char**) {
    return launcher_main();
}
