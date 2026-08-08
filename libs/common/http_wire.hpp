#pragma once

#include "controllers.hpp"

#if defined(_WIN32)
    #include "win_net.hpp"
#else
    #include "nix_net.hpp"
#endif

#include <string>

namespace ogg::net {

constexpr std::size_t kMaxHttpRequestBytes = 16384;
// Full-socket recv/send timeout after HTTP headers are complete (large file downloads).
constexpr int kClientSocketTimeoutMs = 30000;
// Header phase only: drop idle sockets that send no bytes within this window.
constexpr int kHeaderReadTimeoutMs = 2000;
constexpr std::size_t kDefaultFileChunkBytes = 65536;

void set_client_socket_timeout(Socket client_fd);
void set_header_read_timeout(Socket client_fd, int timeout_ms = kHeaderReadTimeoutMs);

bool read_http_request(Socket client_fd, std::string& request_out);

std::string_view extract_request_path(const std::string& request);

int format_inline_http_response(const HttpResponse& response, char* out, std::size_t out_size);

int format_file_http_header(
    const HttpResponse& response,
    std::size_t file_size,
    char* out,
    std::size_t out_size
);

void send_http_response(Socket client_fd, const HttpResponse& response);

void handle_http_connection(Socket client_fd, HttpHandler handler);

} // namespace ogg::net
