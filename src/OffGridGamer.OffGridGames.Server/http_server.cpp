#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <filesystem>
#include "controllers.hpp"

#if defined(_WIN32)
    #include "win_net.hpp"
#else
    #include "nix_net.hpp"
#endif

namespace controllers = ogg::controllers;

static void send_http_response(ogg::net::Socket client_fd, const ogg::net::HttpResponse& response) {
    if (!response.file_path.empty() && std::filesystem::is_regular_file(response.file_path)) {
        std::ifstream file(response.file_path, std::ios::binary);
        if (!file.is_open()) {
            const char* not_found =
                "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
            send(client_fd, not_found, static_cast<int>(std::strlen(not_found)), 0);
            return;
        }

        file.seekg(0, std::ios::end);
        const auto file_size = static_cast<std::size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        char header_buf[512];
        const int header_len = std::snprintf(
            header_buf, sizeof(header_buf),
            "HTTP/1.1 %d OK\r\n"
            "Content-Type: %.*s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            response.status_code,
            static_cast<int>(response.content_type.length()), response.content_type.data(),
            file_size
        );
        send(client_fd, header_buf, header_len, 0);

        std::vector<char> chunk(8192);
        while (file) {
            file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto bytes = file.gcount();
            if (bytes > 0) {
                send(client_fd, chunk.data(), static_cast<int>(bytes), 0);
            }
        }
        return;
    }

    char response_buf[4096];
    const int len = std::snprintf(
        response_buf, sizeof(response_buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %.*s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n"
        "%.*s",
        response.status_code,
        response.status_code == 200 ? "OK" : "Not Found",
        static_cast<int>(response.content_type.length()), response.content_type.data(),
        response.body.length(),
        static_cast<int>(response.body.length()), response.body.data()
    );
    send(client_fd, response_buf, len, 0);
}

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
                std::string_view req_str(buf, static_cast<std::size_t>(bytes));
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

                ogg::net::HttpResponse response = controllers::handle_http_request(request);
                send_http_response(client_fd, response);
            }

            ogg::net::close_socket(client_fd);
        }
    }
}
