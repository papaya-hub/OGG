#if !defined(_WIN32)

#if defined(__linux__)

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "http_server.hpp"
#include "http_wire.hpp"
#include "nix_net.hpp"

namespace ogg::net {

namespace {

enum class IoOp {
    Recv,
    Send,
};

struct HttpConnection {
    uint64_t conn_id = 0;
    Socket socket = InvalidSocket;
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
    uint64_t header_idle_deadline_ms = 0;
};

std::atomic<int> g_active_connections{0};
int g_max_connections = kDefaultMaxHttpConnections;
std::atomic<uint64_t> g_next_conn_id{1};
std::mutex g_conn_map_mutex;
std::unordered_map<uint64_t, HttpConnection*> g_conn_map;
std::mutex g_header_wait_mutex;
std::vector<uint64_t> g_header_wait_ids;

uint64_t monotonic_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + static_cast<uint64_t>(ts.tv_nsec / 1000000);
}

void register_connection(HttpConnection* conn) {
    conn->conn_id = g_next_conn_id.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(g_conn_map_mutex);
    g_conn_map[conn->conn_id] = conn;
}

HttpConnection* find_connection(uint64_t conn_id) {
    std::lock_guard lock(g_conn_map_mutex);
    const auto it = g_conn_map.find(conn_id);
    return it != g_conn_map.end() ? it->second : nullptr;
}

void unregister_connection(HttpConnection* conn) {
    std::lock_guard lock(g_conn_map_mutex);
    g_conn_map.erase(conn->conn_id);
}

void unregister_header_wait(uint64_t conn_id) {
    std::lock_guard lock(g_header_wait_mutex);
    const auto it = std::find(g_header_wait_ids.begin(), g_header_wait_ids.end(), conn_id);
    if (it != g_header_wait_ids.end()) {
        g_header_wait_ids.erase(it);
    }
}

void register_header_wait(HttpConnection* conn) {
    std::lock_guard lock(g_header_wait_mutex);
    g_header_wait_ids.push_back(conn->conn_id);
}

void touch_header_idle_deadline(HttpConnection* conn) {
    conn->header_idle_deadline_ms = monotonic_ms() + static_cast<uint64_t>(conn->header_timeout_ms);
}

void destroy_connection(HttpConnection* conn, int epoll_fd) {
    if (!conn) return;

    bool expected = false;
    if (!conn->closed.compare_exchange_strong(expected, true)) {
        return;
    }

    unregister_header_wait(conn->conn_id);
    unregister_connection(conn);

    if (conn->socket != InvalidSocket) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->socket, nullptr);
        close_socket(conn->socket);
        conn->socket = InvalidSocket;
    }
    g_active_connections.fetch_sub(1, std::memory_order_relaxed);
    delete conn;
}

void update_epoll_interest(HttpConnection* conn, int epoll_fd) {
    if (conn->socket == InvalidSocket || conn->closed.load()) {
        return;
    }

    uint32_t events = EPOLLET | EPOLLRDHUP;
    if (!conn->headers_done) {
        events |= EPOLLIN;
    }
    if (!conn->send_buf.empty() && conn->send_offset < conn->send_buf.size()) {
        events |= EPOLLOUT;
    } else if (conn->file) {
        events |= EPOLLOUT;
    }

    epoll_event ev{};
    ev.events = events;
    ev.data.u64 = conn->conn_id;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->socket, &ev);
}

bool begin_file_chunk_send(HttpConnection* conn, int epoll_fd) {
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
    conn->op = IoOp::Send;
    update_epoll_interest(conn, epoll_fd);
    return true;
}

void begin_inline_send(HttpConnection* conn, int epoll_fd, const HttpResponse& response) {
    char response_buf[4096];
    const int len = format_inline_http_response(response, response_buf, sizeof(response_buf));
    if (len <= 0) {
        destroy_connection(conn, epoll_fd);
        return;
    }

    conn->send_buf.assign(response_buf, static_cast<std::size_t>(len));
    conn->send_offset = 0;
    conn->op = IoOp::Send;
    update_epoll_interest(conn, epoll_fd);
}

void begin_file_send(HttpConnection* conn, int epoll_fd, const HttpResponse& response) {
    conn->file = std::make_unique<std::ifstream>(response.file_path, std::ios::binary);
    if (!conn->file->is_open()) {
        const char* not_found =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
        conn->send_buf = not_found;
        conn->send_offset = 0;
        conn->op = IoOp::Send;
        update_epoll_interest(conn, epoll_fd);
        return;
    }

    conn->file->seekg(0, std::ios::end);
    conn->file_size = static_cast<std::size_t>(conn->file->tellg());
    conn->file->seekg(0, std::ios::beg);

    char header_buf[512];
    const int header_len = format_file_http_header(response, conn->file_size, header_buf, sizeof(header_buf));
    if (header_len <= 0) {
        destroy_connection(conn, epoll_fd);
        return;
    }

    conn->send_buf.assign(header_buf, static_cast<std::size_t>(header_len));
    conn->send_offset = 0;
    conn->op = IoOp::Send;
    update_epoll_interest(conn, epoll_fd);
}

void dispatch_response(HttpConnection* conn, int epoll_fd, const HttpResponse& response) {
    if (!response.file_path.empty() && std::filesystem::is_regular_file(response.file_path)) {
        begin_file_send(conn, epoll_fd, response);
        return;
    }

    begin_inline_send(conn, epoll_fd, response);
}

void on_headers_complete(HttpConnection* conn, int epoll_fd) {
    conn->headers_done = true;
    unregister_header_wait(conn->conn_id);
    set_client_socket_timeout(conn->socket);

    const std::string_view path = extract_request_path(conn->request);
    const HttpRequest http_request{
        .method = "GET",
        .path = path,
        .body = ""
    };

    const HttpResponse response = conn->handler(http_request);
    dispatch_response(conn, epoll_fd, response);
}

bool try_recv(HttpConnection* conn, int epoll_fd) {
    if (conn->closed.load() || conn->headers_done) {
        return true;
    }

    for (;;) {
        const ssize_t bytes = recv(conn->socket, conn->recv_buf, sizeof(conn->recv_buf), 0);
        if (bytes > 0) {
            conn->request.append(conn->recv_buf, static_cast<std::size_t>(bytes));
            if (conn->request.size() > kMaxHttpRequestBytes) {
                destroy_connection(conn, epoll_fd);
                return false;
            }

            touch_header_idle_deadline(conn);

            if (conn->request.find("\r\n\r\n") != std::string::npos) {
                on_headers_complete(conn, epoll_fd);
                return !conn->closed.load();
            }
            continue;
        }

        if (bytes == 0) {
            destroy_connection(conn, epoll_fd);
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        destroy_connection(conn, epoll_fd);
        return false;
    }
}

bool try_send(HttpConnection* conn, int epoll_fd) {
    if (conn->closed.load()) {
        return false;
    }

    for (;;) {
        if (conn->send_offset >= conn->send_buf.size()) {
            conn->send_buf.clear();
            conn->send_offset = 0;

            if (conn->file) {
                if (!begin_file_chunk_send(conn, epoll_fd)) {
                    destroy_connection(conn, epoll_fd);
                }
                return !conn->closed.load();
            }

            destroy_connection(conn, epoll_fd);
            return false;
        }

        const std::size_t remaining = conn->send_buf.size() - conn->send_offset;
        const ssize_t bytes = send(
            conn->socket,
            conn->send_buf.data() + conn->send_offset,
            remaining,
            MSG_NOSIGNAL
        );

        if (bytes > 0) {
            conn->send_offset += static_cast<std::size_t>(bytes);
            continue;
        }

        if (bytes == 0) {
            destroy_connection(conn, epoll_fd);
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            update_epoll_interest(conn, epoll_fd);
            return true;
        }

        destroy_connection(conn, epoll_fd);
        return false;
    }
}

void handle_client_event(HttpConnection* conn, int epoll_fd, uint32_t events) {
    if (!conn || conn->closed.load()) {
        return;
    }

    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        destroy_connection(conn, epoll_fd);
        return;
    }

    if (events & EPOLLIN) {
        if (!try_recv(conn, epoll_fd)) {
            return;
        }
    }

    if (events & EPOLLOUT) {
        try_send(conn, epoll_fd);
    }
}

bool begin_connection(
    Socket client_fd,
    int epoll_fd,
    HttpHandler handler,
    int header_timeout_ms,
    std::size_t file_chunk_bytes
) {
    if (g_active_connections.load(std::memory_order_relaxed) >= g_max_connections) {
        close_socket(client_fd);
        return false;
    }

    if (set_non_blocking(client_fd) != 0) {
        close_socket(client_fd);
        return false;
    }

    auto* conn = new HttpConnection();
    conn->socket = client_fd;
    conn->handler = handler;
    conn->header_timeout_ms = header_timeout_ms;
    conn->file_chunk.resize(file_chunk_bytes);
    register_connection(conn);

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.u64 = conn->conn_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        unregister_connection(conn);
        delete conn;
        close_socket(client_fd);
        return false;
    }

    set_header_read_timeout(client_fd, header_timeout_ms);
    touch_header_idle_deadline(conn);
    register_header_wait(conn);
    g_active_connections.fetch_add(1, std::memory_order_relaxed);

    if (try_recv(conn, epoll_fd) && !conn->send_buf.empty()) {
        try_send(conn, epoll_fd);
    }

    return true;
}

void accept_connections(
    Socket listen_fd,
    int epoll_fd,
    HttpHandler handler,
    int header_timeout_ms,
    std::size_t file_chunk_bytes
) {
    for (;;) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const Socket client_fd = accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (client_fd == InvalidSocket) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            continue;
        }

        if (!begin_connection(client_fd, epoll_fd, handler, header_timeout_ms, file_chunk_bytes)) {
            continue;
        }
    }
}

void epoll_worker(
    int epoll_fd,
    Socket listen_fd,
    HttpHandler handler,
    int header_timeout_ms,
    std::size_t file_chunk_bytes
) {
    constexpr int kMaxEvents = 256;
    epoll_event events[kMaxEvents];

    for (;;) {
        const int ready = epoll_wait(epoll_fd, events, kMaxEvents, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < ready; ++i) {
            if (events[i].data.fd == listen_fd) {
                accept_connections(
                    listen_fd,
                    epoll_fd,
                    handler,
                    header_timeout_ms,
                    file_chunk_bytes
                );
            } else {
                HttpConnection* conn = find_connection(events[i].data.u64);
                if (!conn) {
                    continue;
                }
                handle_client_event(conn, epoll_fd, events[i].events);
            }
        }
    }
}

} // namespace

void run_http_server(Socket listen_fd, HttpHandler handler, const HttpServerConfig& config) {
    const int worker_count = config.worker_threads > 0 ? config.worker_threads : kDefaultHttpWorkerThreads;
    g_max_connections = config.max_connections > 0 ? config.max_connections : kDefaultMaxHttpConnections;
    const int header_timeout_ms = config.header_timeout_ms > 0 ? config.header_timeout_ms : kHeaderReadTimeoutMs;
    const std::size_t file_chunk_bytes =
        config.file_chunk_bytes > 0 ? config.file_chunk_bytes : kDefaultFileChunkBytes;

    const int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::puts("[HTTP] Failed to create epoll instance.");
        return;
    }

    epoll_event listen_ev{};
    listen_ev.events = EPOLLIN;
    listen_ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_ev) == -1) {
        std::puts("[HTTP] Failed to add listen socket to epoll.");
        close(epoll_fd);
        return;
    }

    std::printf(
        "[HTTP] Async epoll (non-blocking recv/send) listen=%d workers=%d max_conn=%d header_idle=%dms socket=%dms chunk=%zu\n",
        listen_fd,
        worker_count,
        g_max_connections,
        header_timeout_ms,
        kClientSocketTimeoutMs,
        file_chunk_bytes
    );

    std::thread header_sweeper([epoll_fd, header_timeout_ms]() {
        const unsigned sweep_ms = static_cast<unsigned>(header_timeout_ms < 250 ? header_timeout_ms : 250);
        for (;;) {
            usleep(sweep_ms * 1000);
            const uint64_t now = monotonic_ms();
            std::vector<uint64_t> expired_ids;
            {
                std::lock_guard lock(g_header_wait_mutex);
                for (uint64_t conn_id : g_header_wait_ids) {
                    HttpConnection* conn = find_connection(conn_id);
                    if (conn && !conn->headers_done && now >= conn->header_idle_deadline_ms) {
                        expired_ids.push_back(conn_id);
                    }
                }
            }
            for (uint64_t conn_id : expired_ids) {
                HttpConnection* conn = find_connection(conn_id);
                if (conn) {
                    destroy_connection(conn, epoll_fd);
                }
            }
        }
    });
    header_sweeper.detach();

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([epoll_fd, listen_fd, handler, header_timeout_ms, file_chunk_bytes]() {
            epoll_worker(epoll_fd, listen_fd, handler, header_timeout_ms, file_chunk_bytes);
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }
}

} // namespace ogg::net

#else // !__linux__

#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/socket.h>

#include "http_server.hpp"
#include "http_wire.hpp"
#include "nix_net.hpp"

namespace ogg::net {

namespace {

class WorkQueue {
public:
    void push(Socket client_fd) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(client_fd);
        }
        cv_.notify_one();
    }

    Socket pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        const Socket client_fd = queue_.front();
        queue_.pop_front();
        return client_fd;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Socket> queue_;
};

std::atomic<int> g_active_connections{0};

} // namespace

void run_http_server(Socket listen_fd, HttpHandler handler, const HttpServerConfig& config) {
    const int worker_count = config.worker_threads > 0 ? config.worker_threads : kDefaultHttpWorkerThreads;
    const int max_connections = config.max_connections > 0 ? config.max_connections : kDefaultMaxHttpConnections;
    const int header_timeout_ms = config.header_timeout_ms > 0 ? config.header_timeout_ms : kHeaderReadTimeoutMs;
    WorkQueue queue;

    std::printf(
        "[HTTP] POSIX worker pool listen=%d workers=%d max_conn=%d header_idle=%dms socket=%dms\n",
        listen_fd,
        worker_count,
        max_connections,
        header_timeout_ms,
        kClientSocketTimeoutMs
    );

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([&queue, handler]() {
            for (;;) {
                const Socket client_fd = queue.pop();
                handle_http_connection(client_fd, handler);
                g_active_connections.fetch_sub(1, std::memory_order_relaxed);
            }
        });
    }

    for (;;) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const Socket client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == InvalidSocket) {
            continue;
        }

        const int active = g_active_connections.fetch_add(1, std::memory_order_relaxed) + 1;
        if (active > max_connections) {
            g_active_connections.fetch_sub(1, std::memory_order_relaxed);
            close_socket(client_fd);
            continue;
        }

        queue.push(client_fd);
    }
}

} // namespace ogg::net

#endif // __linux__

#endif // !_WIN32
