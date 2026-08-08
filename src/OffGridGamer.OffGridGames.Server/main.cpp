#include <cstdio>
#include <cstring>
#include <thread>
#include "controllers.hpp"

#if defined(_WIN32)
    #include "win_net.hpp"
#else
    #include "nix_net.hpp"
#endif

#include "http_server.hpp"

namespace controllers = ogg::controllers;

int main(int argc, char* argv[]) {
    // 1. Check CLI Flags (e.g., ogg.server.exe -s reload)
    if (argc >= 3 && std::strcmp(argv[1], "-s") == 0) {
        const char* signal_cmd = argv[2];

        if (std::strcmp(signal_cmd, "reload") == 0) {
            std::puts("[CLI] Requesting server reload...");
            if (ogg::net::send_ipc_command("RELOAD")) {
                std::puts("[CLI] Reload signal sent successfully.");
            } else {
                std::puts("[CLI] Error: Could not connect to running OGG.Server instance.");
                return 1;
            }
            return 0; // Exit CLI command process
        } else {
            std::printf("[CLI] Unknown signal: %s\n", signal_cmd);
            return 1;
        }
    }

    // 2. Start Primary Server Process
    ogg::net::PlatformContext net_env;

    // Load web assets on initial boot
    controllers::load_web_assets();

    constexpr unsigned short HTTP_PORT = 8123;
    constexpr unsigned short TCP_PORT  = 8124;
    constexpr unsigned short UDP_PORT  = 8125;

    ogg::net::Socket http_fd = ogg::net::bind_and_listen(HTTP_PORT);
    ogg::net::Socket tcp_fd  = ogg::net::bind_and_listen(TCP_PORT);
    ogg::net::Socket udp_fd  = ogg::net::bind_and_listen_udp(UDP_PORT);

    if (http_fd == ogg::net::InvalidSocket ||
        tcp_fd == ogg::net::InvalidSocket ||
        udp_fd == ogg::net::InvalidSocket) {
        std::puts("Failed to bind server ports.");
        return 1;
    }

    // 3. Launch IPC Signal Listener Thread
    std::thread ipc_thread([]() {
        ogg::net::run_ipc_listener([](const char* cmd) {
            if (std::strcmp(cmd, "RELOAD") == 0) {
                std::puts("[SERVER] Received RELOAD signal. Reloading web assets and configs...");
                controllers::load_web_assets();
            }
        });
    });
    ipc_thread.detach();

    std::printf("[HTTP] Web Server listening on port %d\n", HTTP_PORT);
    std::printf("[TCP]  Game Server listening on port %d\n", TCP_PORT);
    std::printf("[UDP]  Sync Server listening on port %d\n", UDP_PORT);

    // 4. Launch Service Threads
    // Run HTTP Listener on dedicated thread
    std::thread http_thread([http_fd]() {
        ogg::net::run_http_server(
            http_fd,
            +[](const ogg::net::HttpRequest& req) {
                return controllers::handle_http_request(req);
            }
        );
    });

    std::thread tcp_thread([tcp_fd]() {
        ogg::net::run_iocp_event_loop(tcp_fd);
    });
    tcp_thread.detach();

    std::thread udp_thread([udp_fd]() {
        ogg::net::run_udp_receiver(udp_fd, controllers::handle_udp_datagram);
    });
    udp_thread.detach();

    http_thread.join();

    return 0;
}
