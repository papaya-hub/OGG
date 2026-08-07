#include <cstdio>
#include <thread>
#include <cstring>
#include "controllers.hpp"

#if defined(_WIN32)
    #include "win32_net.hpp"
#else
    #include "nix_net.hpp"
#endif

namespace controllers = ogg::controllers;

void run_http_server(ogg::net::Socket server_fd) {
    std::printf("[HTTP] Web Server running on port 8123...\n");

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        ogg::net::Socket client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd != ogg::net::InvalidSocket) {
            char buf[1024] = {0};
            int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

            if (bytes > 0) {
                // Parse simple HTTP Request Path (e.g. "GET / HTTP/1.1")
                std::string_view req_str(buf, bytes);
                std::string_view path = "/";

                size_t first_space = req_str.find(' ');
                if (first_space != std::string_view::npos) {
                    size_t second_space = req_str.find(' ', first_space + 1);
                    if (second_space != std::string_view::npos) {
                        path = req_str.substr(first_space + 1, second_space - first_space - 1);
                    }
                }

                ogg::net::HttpRequest request{
                    .method = "GET",
                    .path = path,
                    .body = ""
                };

                // Dispatch to HTTP Controller
                ogg::net::HttpResponse response = controllers::handle_http_request(request);

                // Format & Send HTTP Response
                char response_buf[4096];
                int len = std::snprintf(
                    response_buf, sizeof(response_buf),
                    "HTTP/1.1 %d OK\r\n"
                    "Content-Type: %.*s\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n\r\n"
                    "%.*s",
                    response.status_code,
                    static_cast<int>(response.content_type.length()), response.content_type.data(),
                    response.body.length(),
                    static_cast<int>(response.body.length()), response.body.data()
                );

                send(client_fd, response_buf, len, 0);
            }

            ogg::net::close_socket(client_fd);
        }
    }
}
