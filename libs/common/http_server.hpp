#pragma once

#include "controllers.hpp"
#include "http_wire.hpp"

#if defined(_WIN32)
    #include "win_net.hpp"
#else
    #include "nix_net.hpp"
#endif

namespace ogg::net {

#if defined(_WIN32)
constexpr int kDefaultHttpWorkerThreads = 64;
#else
constexpr int kDefaultHttpWorkerThreads = 256;
#endif
constexpr int kDefaultMaxHttpConnections = 12000;

// Header phase: drop sockets idle > header_timeout_ms (default 2s).
// After headers: kClientSocketTimeoutMs (30s) for recv/send on the full response.
struct HttpServerConfig {
    int worker_threads = kDefaultHttpWorkerThreads;
    int max_connections = kDefaultMaxHttpConnections;
    int header_timeout_ms = kHeaderReadTimeoutMs;
    std::size_t file_chunk_bytes = kDefaultFileChunkBytes;
};

void run_http_server(Socket listen_fd, HttpHandler handler, const HttpServerConfig& config = {});

} // namespace ogg::net
