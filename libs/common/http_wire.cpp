#include "http_wire.hpp"

#include "http_websocket.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <filesystem>

#if defined(_WIN32)
    #include "win_net.hpp"
    #include <winsock2.h>
#else
    #include "nix_net.hpp"
    #include <sys/socket.h>
#endif

namespace ogg::net {

void set_client_socket_timeout(Socket client_fd) {
#if defined(_WIN32)
    const DWORD timeout_ms = static_cast<DWORD>(kClientSocketTimeoutMs);
    // Applied after headers; does not affect overlapped WSARecv/WSASend on IOCP.
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval timeout{};
    timeout.tv_sec = kClientSocketTimeoutMs / 1000;
    timeout.tv_usec = (kClientSocketTimeoutMs % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

void set_header_read_timeout(Socket client_fd, int timeout_ms) {
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

bool read_http_request(Socket client_fd, std::string& request_out) {
    request_out.clear();
    char buffer[4096];

    while (request_out.size() < kMaxHttpRequestBytes) {
        const int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            return !request_out.empty();
        }

        request_out.append(buffer, static_cast<std::size_t>(bytes));
        if (request_out.find("\r\n\r\n") != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string_view extract_request_path(const std::string& request) {
    const std::size_t first_space = request.find(' ');
    if (first_space == std::string::npos) {
        return "/";
    }

    const std::size_t second_space = request.find(' ', first_space + 1);
    if (second_space == std::string::npos || second_space <= first_space + 1) {
        return "/";
    }

    return std::string_view(request).substr(first_space + 1, second_space - first_space - 1);
}

int format_inline_http_response(const HttpResponse& response, char* out, std::size_t out_size) {
    return std::snprintf(
        out,
        out_size,
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
}

int format_file_http_header(
    const HttpResponse& response,
    std::size_t file_size,
    char* out,
    std::size_t out_size
) {
    return std::snprintf(
        out,
        out_size,
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %.*s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        response.status_code,
        static_cast<int>(response.content_type.length()), response.content_type.data(),
        file_size
    );
}

void send_http_response(Socket client_fd, const HttpResponse& response) {
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
        const int header_len = format_file_http_header(response, file_size, header_buf, sizeof(header_buf));
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
    const int len = format_inline_http_response(response, response_buf, sizeof(response_buf));
    send(client_fd, response_buf, len, 0);
}

void handle_http_connection(Socket client_fd, HttpHandler handler) {
    set_header_read_timeout(client_fd);

    std::string request;
    if (!read_http_request(client_fd, request)) {
        close_socket(client_fd);
        return;
    }

    set_client_socket_timeout(client_fd);

    const std::string_view path = extract_request_path(request);
    if (is_websocket_upgrade(request, path)) {
        if (send_websocket_accept(client_fd, request)) {
            run_websocket_session(client_fd);
        }
        close_socket(client_fd);
        return;
    }

    const HttpRequest http_request{
        .method = "GET",
        .path = path,
        .body = ""
    };

    const HttpResponse response = handler(http_request);
    send_http_response(client_fd, response);
    close_socket(client_fd);
}

} // namespace ogg::net
