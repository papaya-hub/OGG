#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
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

static fs::path find_local_client_match(const fs::path& exe_dir, const std::string& remote_name) {
    const std::string remote_base = ogg::http_client::basename(remote_name);
    for (const auto& entry : fs::directory_iterator(exe_dir)) {
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

static void status(ogg::launcher::LauncherUi& ui, int percent, const std::string& message) {
    ui.set_progress(percent);
    ui.log(message);
    ui.pump();
}

static void pump_for_ms(ogg::launcher::LauncherUi& ui, DWORD duration_ms) {
    const DWORD start = GetTickCount();
    for (;;) {
        ui.pump();
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= duration_ms) break;
        Sleep(16);
    }
}

static bool launch_client(const fs::path& client_path, const std::string& client_url, ogg::launcher::LauncherUi& ui) {
    if (client_url.empty()) {
        status(ui, -1, "No client URL.");
        return false;
    }

    status(ui, kProgressDone, "Launching...");
    pump_for_ms(ui, kLaunchDelayMs);

    const std::wstring url_wide = ogg::http_client::to_wide(client_url);
    std::wstring command_line = L"\"" + client_path.wstring() + L"\" \"" + url_wide + L"\"";
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

static WorkflowResult prepare_client(ogg::launcher::LauncherUi& ui, fs::path& client_path, std::string& client_url) {
    const HostEntry hosts[] = {
        { L"localhost", 8123 },
        { L"ogg.sendermesh.com", 8123 },
        { L"in.msheriff.com", 8123 },
    };

    const fs::path exe_dir = get_exe_directory();
    ui.show_error_actions(false);
    ui.set_progress(0);
    ui.pump();

    std::string remote_name;
    std::wstring active_host;
    std::uint16_t active_port = 8123;
    client_url.clear();

    const int host_count = static_cast<int>(sizeof(hosts) / sizeof(hosts[0]));
    for (int i = 0; i < host_count; ++i) {
        const auto& entry = hosts[i];
        const int try_progress = (i + 1) * kProgressPerServerTry;

        status(ui, try_progress, "Connecting...");

        int status_code = 0;
        std::vector<std::uint8_t> body;
        if (!ogg::http_client::http_get(entry.host, entry.port, L"/api/ogg/patch", status_code, body)) {
            status(ui, try_progress, "Connection failed.");
            continue;
        }
        if (status_code != 200) {
            status(ui, try_progress, "HTTP " + std::to_string(status_code));
            continue;
        }

        remote_name = ogg::http_client::trim(ogg::http_client::body_as_string(body));
        if (remote_name.empty()) {
            status(ui, try_progress, "Empty patch response.");
            continue;
        }

        int client_status = 0;
        std::vector<std::uint8_t> client_body;
        if (!ogg::http_client::http_get(entry.host, entry.port, L"/client", client_status, client_body) ||
            client_status != 200) {
            status(ui, try_progress, "Client page unavailable.");
            continue;
        }

        active_host = entry.host;
        active_port = entry.port;
        client_url = ogg::http_client::build_http_url(active_host, active_port, "/client");
        status(ui, kProgressPatchReady, "Patch found.");
        break;
    }

    if (remote_name.empty() || client_url.empty()) {
        status(ui, -1, "No patch server reachable.");
        return WorkflowResult::NetworkError;
    }

    const std::string remote_base = ogg::http_client::basename(remote_name);
    client_path = find_local_client_match(exe_dir, remote_base);
    if (client_path.empty()) {
        client_path = exe_dir / remote_base;
        const std::wstring download_path = L"/public_html/" + ogg::http_client::to_wide(remote_base);
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
    } else {
        status(ui, kProgressReadyToLaunch, "Client up to date.");
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
        std::string client_url;
        WorkflowResult workflow = WorkflowResult::NetworkError;
        std::atomic<bool> worker_done{false};

        std::thread worker([&] {
            workflow = prepare_client(*ui, client_path, client_url);
            worker_done.store(true, std::memory_order_release);
        });

        while (!worker_done.load(std::memory_order_acquire)) {
            ui->pump();
        }
        worker.join();

        if (workflow == WorkflowResult::Ready) {
            if (launch_client(client_path, client_url, *ui)) {
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
            std::string client_url;
            const WorkflowResult workflow = prepare_client(*ui, client_path, client_url);

            if (workflow == WorkflowResult::Ready) {
                if (launch_client(client_path, client_url, *ui)) {
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
