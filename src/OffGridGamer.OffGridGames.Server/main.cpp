#include <cstdio>

#if defined(_WIN32)
    #include "win32_net.hpp"
#else
    #include "nix_net.hpp"
#endif

int main() {
    ogg::net::PlatformContext net_env;
    constexpr unsigned short PORT = 8123;

    ogg::net::Socket server_fd = ogg::net::bind_and_listen(PORT);
    if (server_fd == ogg::net::InvalidSocket) {
        return 1;
    }

    std::printf("Server listening on port %d...\n", PORT);

    // Connection Accept Loop
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        ogg::net::Socket client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd != ogg::net::InvalidSocket) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
            // std::printf("Client connected from %s\n", ip_str);

            char recv_buf[1024];
            int bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);

            if (bytes_received > 0) {
                const char response[] =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 10\r\n"
                    "Connection: close\r\n\r\n"
                    "OGG.Server";

                send(client_fd, response, static_cast<int>(sizeof(response) - 1), 0);
            }

            ogg::net::close_socket(client_fd);
        }
    }

    ogg::net::close_socket(server_fd);
    return 0;
}
