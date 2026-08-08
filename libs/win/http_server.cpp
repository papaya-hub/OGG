#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "http_server.hpp"
#include "http_wire.hpp"
#include "win_net.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace ogg::net {

namespace {

enum class IoOp {
    Recv,
    Send,
};

struct HttpConnection {
    Socket socket = InvalidSocket;
    WSAOVERLAPPED overlapped{};
    WSABUF wsa_buf{};
    IoOp op = IoOp::Recv;
    std::atomic<bool> closed{false};
    bool headers_done = false;

    std::string request;
    char recv_buf[4096]{};

    std::string send_buf;
    std::size_t send_offset = 0;

    std::unique_ptr<std::ifstream> file;
    std::size_t file_size = 0;
    std::vector<char> file_chunk;

    HttpHandler handler{};
    int header_timeout_ms = kHeaderReadTimeoutMs;
    ULONGLONG header_idle_deadline_ticks = 0;
};

std::atomic<int> g_active_connections{0};
int g_max_connections = kDefaultMaxHttpConnections;
std::mutex g_header_wait_mutex;
std::vector<HttpConnection*> g_header_wait;

void unregister_header_wait(HttpConnection* conn) {
    std::lock_guard lock(g_header_wait_mutex);
    const auto it = std::find(g_header_wait.begin(), g_header_wait.end(), conn);
    if (it != g_header_wait.end()) {
        g_header_wait.erase(it);
    }
}

void register_header_wait(HttpConnection* conn) {
    std::lock_guard lock(g_header_wait_mutex);
    g_header_wait.push_back(conn);
}

void touch_header_idle_deadline(HttpConnection* conn) {
    conn->header_idle_deadline_ticks = GetTickCount64() + static_cast<ULONGLONG>(conn->header_timeout_ms);
}

void destroy_connection(HttpConnection* conn) {
    if (!conn) return;

    bool expected = false;
    if (!conn->closed.compare_exchange_strong(expected, true)) {
        return;
    }

    unregister_header_wait(conn);

    if (conn->socket != InvalidSocket) {
        close_socket(conn->socket);
        conn->socket = InvalidSocket;
    }
    g_active_connections.fetch_sub(1, std::memory_order_relaxed);
    delete conn;
}

bool post_recv(HttpConnection* conn) {
    conn->op = IoOp::Recv;
    ZeroMemory(&conn->overlapped, sizeof(conn->overlapped));
    conn->wsa_buf.buf = conn->recv_buf;
    conn->wsa_buf.len = static_cast<ULONG>(sizeof(conn->recv_buf));

    DWORD flags = 0;
    DWORD received = 0;
    if (WSARecv(
            conn->socket,
            &conn->wsa_buf,
            1,
            &received,
            &flags,
            &conn->overlapped,
            nullptr) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        return err == ERROR_IO_PENDING;
    }
    return true;
}

bool post_send(HttpConnection* conn) {
    if (conn->send_offset >= conn->send_buf.size()) {
        return false;
    }

    conn->op = IoOp::Send;
    ZeroMemory(&conn->overlapped, sizeof(conn->overlapped));
    conn->wsa_buf.buf = conn->send_buf.data() + conn->send_offset;
    conn->wsa_buf.len = static_cast<ULONG>(conn->send_buf.size() - conn->send_offset);

    DWORD sent = 0;
    if (WSASend(
            conn->socket,
            &conn->wsa_buf,
            1,
            &sent,
            0,
            &conn->overlapped,
            nullptr) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        return err == ERROR_IO_PENDING;
    }
    return true;
}

bool begin_file_chunk_send(HttpConnection* conn) {
    if (!conn->file || !conn->file->good()) {
        return false;
    }

    conn->file->read(conn->file_chunk.data(), static_cast<std::streamsize>(conn->file_chunk.size()));
    const auto bytes = conn->file->gcount();
    if (bytes <= 0) {
        return false;
    }

    conn->send_buf.assign(conn->file_chunk.data(), static_cast<std::size_t>(bytes));
    conn->send_offset = 0;
    return post_send(conn);
}

void begin_inline_send(HttpConnection* conn, const HttpResponse& response) {
    char response_buf[4096];
    const int len = format_inline_http_response(response, response_buf, sizeof(response_buf));
    if (len <= 0) {
        destroy_connection(conn);
        return;
    }

    conn->send_buf.assign(response_buf, static_cast<std::size_t>(len));
    conn->send_offset = 0;
    if (!post_send(conn)) {
        destroy_connection(conn);
    }
}

void begin_file_send(HttpConnection* conn, const HttpResponse& response) {
    conn->file = std::make_unique<std::ifstream>(response.file_path, std::ios::binary);
    if (!conn->file->is_open()) {
        const char* not_found =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
        conn->send_buf = not_found;
        conn->send_offset = 0;
        if (!post_send(conn)) {
            destroy_connection(conn);
        }
        return;
    }

    conn->file->seekg(0, std::ios::end);
    conn->file_size = static_cast<std::size_t>(conn->file->tellg());
    conn->file->seekg(0, std::ios::beg);

    char header_buf[512];
    const int header_len = format_file_http_header(response, conn->file_size, header_buf, sizeof(header_buf));
    if (header_len <= 0) {
        destroy_connection(conn);
        return;
    }

    conn->send_buf.assign(header_buf, static_cast<std::size_t>(header_len));
    conn->send_offset = 0;
    if (!post_send(conn)) {
        destroy_connection(conn);
    }
}

void dispatch_response(HttpConnection* conn, const HttpResponse& response) {
    if (!response.file_path.empty() && std::filesystem::is_regular_file(response.file_path)) {
        begin_file_send(conn, response);
        return;
    }

    begin_inline_send(conn, response);
}

void on_headers_complete(HttpConnection* conn) {
    conn->headers_done = true;
    unregister_header_wait(conn);
    set_client_socket_timeout(conn->socket);

    const std::string_view path = extract_request_path(conn->request);
    const HttpRequest http_request{
        .method = "GET",
        .path = path,
        .body = ""
    };

    const HttpResponse response = conn->handler(http_request);
    dispatch_response(conn, response);
}

void on_recv_complete(HttpConnection* conn, DWORD bytes) {
    if (bytes == 0) {
        destroy_connection(conn);
        return;
    }

    conn->request.append(conn->recv_buf, bytes);
    if (conn->request.size() > kMaxHttpRequestBytes) {
        destroy_connection(conn);
        return;
    }

    touch_header_idle_deadline(conn);

    if (conn->request.find("\r\n\r\n") != std::string::npos) {
        on_headers_complete(conn);
        return;
    }

    if (!post_recv(conn)) {
        destroy_connection(conn);
    }
}

void on_send_complete(HttpConnection* conn, DWORD bytes) {
    if (bytes == 0) {
        destroy_connection(conn);
        return;
    }

    conn->send_offset += static_cast<std::size_t>(bytes);

    if (conn->send_offset < conn->send_buf.size()) {
        if (!post_send(conn)) {
            destroy_connection(conn);
        }
        return;
    }

    conn->send_buf.clear();
    conn->send_offset = 0;

    if (conn->file) {
        if (!begin_file_chunk_send(conn)) {
            destroy_connection(conn);
        }
        return;
    }

    destroy_connection(conn);
}

void on_io_complete(HttpConnection* conn, DWORD bytes, BOOL ok) {
    if (!conn) return;

    if (!ok || bytes == 0) {
        destroy_connection(conn);
        return;
    }

    if (conn->op == IoOp::Recv) {
        on_recv_complete(conn, bytes);
    } else {
        on_send_complete(conn, bytes);
    }
}

bool begin_connection(Socket client_fd, HANDLE iocp, HttpHandler handler, int header_timeout_ms, std::size_t file_chunk_bytes) {
    if (g_active_connections.load(std::memory_order_relaxed) >= g_max_connections) {
        close_socket(client_fd);
        return false;
    }

    auto* conn = new HttpConnection();
    conn->socket = client_fd;
    conn->handler = handler;
    conn->header_timeout_ms = header_timeout_ms;
    conn->file_chunk.resize(file_chunk_bytes);

    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_fd), iocp, reinterpret_cast<ULONG_PTR>(conn), 0)) {
        delete conn;
        close_socket(client_fd);
        return false;
    }

    set_header_read_timeout(client_fd, header_timeout_ms);
    touch_header_idle_deadline(conn);
    register_header_wait(conn);
    g_active_connections.fetch_add(1, std::memory_order_relaxed);

    if (!post_recv(conn)) {
        destroy_connection(conn);
        return false;
    }

    return true;
}

} // namespace

void run_http_server(Socket listen_fd, HttpHandler handler, const HttpServerConfig& config) {
    const int worker_count = config.worker_threads > 0 ? config.worker_threads : kDefaultHttpWorkerThreads;
    g_max_connections = config.max_connections > 0 ? config.max_connections : kDefaultMaxHttpConnections;
    const int header_timeout_ms = config.header_timeout_ms > 0 ? config.header_timeout_ms : kHeaderReadTimeoutMs;
    const std::size_t file_chunk_bytes =
        config.file_chunk_bytes > 0 ? config.file_chunk_bytes : kDefaultFileChunkBytes;

    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>(worker_count));
    if (!iocp) {
        std::puts("[HTTP] Failed to create IOCP.");
        return;
    }

    std::printf(
        "[HTTP] Async IOCP (WSARecv/WSASend) listen=%llu workers=%d max_conn=%d header_idle=%dms socket=%dms chunk=%zu\n",
        static_cast<unsigned long long>(listen_fd),
        worker_count,
        g_max_connections,
        header_timeout_ms,
        kClientSocketTimeoutMs,
        file_chunk_bytes
    );

    std::thread header_sweeper([header_timeout_ms]() {
        const DWORD sweep_ms = static_cast<DWORD>(header_timeout_ms < 250 ? header_timeout_ms : 250);
        for (;;) {
            Sleep(sweep_ms);
            const ULONGLONG now = GetTickCount64();
            std::vector<HttpConnection*> expired;
            {
                std::lock_guard lock(g_header_wait_mutex);
                for (HttpConnection* conn : g_header_wait) {
                    if (!conn->headers_done && now >= conn->header_idle_deadline_ticks) {
                        expired.push_back(conn);
                    }
                }
            }
            for (HttpConnection* conn : expired) {
                destroy_connection(conn);
            }
        }
    });
    header_sweeper.detach();

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([iocp]() {
            for (;;) {
                DWORD bytes = 0;
                ULONG_PTR completion_key = 0;
                LPOVERLAPPED overlapped = nullptr;

                const BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &completion_key, &overlapped, INFINITE);
                auto* conn = reinterpret_cast<HttpConnection*>(completion_key);
                if (!conn || !overlapped) {
                    continue;
                }

                on_io_complete(conn, bytes, ok);
            }
        });
    }

    for (;;) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        const Socket client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == InvalidSocket) {
            continue;
        }

        if (!begin_connection(client_fd, iocp, handler, header_timeout_ms, file_chunk_bytes)) {
            continue;
        }
    }
}

} // namespace ogg::net

#endif
