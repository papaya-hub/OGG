#ifndef OGG_NIX_NET_HPP
#define OGG_NIX_NET_HPP

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <sys/un.h>

namespace ogg::net {

using Socket = int;
constexpr Socket InvalidSocket = -1;
constexpr int SocketError = -1;

struct PlatformContext {
    PlatformContext() = default;
    ~PlatformContext() = default;
    PlatformContext(const PlatformContext&) = delete;
    PlatformContext& operator=(const PlatformContext&) = delete;
};

inline void close_socket(Socket sock) {
    if (sock != InvalidSocket) {
        ::close(sock);
    }
}

inline int set_non_blocking(Socket sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

inline int set_reuse_addr(Socket sock) {
    int opt = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

inline Socket bind_and_listen(unsigned short port, int backlog = 128) {
    Socket server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == InvalidSocket) return InvalidSocket;

    set_reuse_addr(server_fd);
    set_non_blocking(server_fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SocketError) {
        close_socket(server_fd);
        return InvalidSocket;
    }

    if (listen(server_fd, backlog) == SocketError) {
        close_socket(server_fd);
        return InvalidSocket;
    }

    return server_fd;
}

// C10K epoll Event Loop
inline void run_epoll_event_loop(Socket server_fd) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::puts("Failed to create epoll instance.");
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        std::puts("Failed to add server_fd to epoll.");
        close(epoll_fd);
        return;
    }

    constexpr int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];

    const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 10\r\n"
        "Connection: close\r\n\r\n"
        "OGG.Server";

    std::puts("epoll Event Loop running...");

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                // Accept new connections
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    Socket client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

                    if (client_fd == InvalidSocket) break; // EWOULDBLOCK / EAGAIN

                    set_non_blocking(client_fd);
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN | EPOLLET; // Edge-triggered for C10K efficiency
                    client_ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
                }
            } else {
                // Read from client socket
                Socket client_fd = events[i].data.fd;
                char buf[1024];
                int bytes = recv(client_fd, buf, sizeof(buf), 0);
                if (bytes > 0) {
                    send(client_fd, response, sizeof(response) - 1, 0);
                }
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                close_socket(client_fd);
            }
        }
    }

    close(epoll_fd);
}

inline Socket bind_and_listen_udp(unsigned short port) {
    Socket sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == InvalidSocket) return InvalidSocket;

    set_reuse_addr(sock);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SocketError) {
        close_socket(sock);
        return InvalidSocket;
    }

    return sock;
}

// Lightweight UDP Datagram Receive Loop
inline void run_udp_receiver(Socket udp_sock, UdpDatagramHandler handler) {
    uint8_t buffer[2048];
    sockaddr_in sender_addr{};
    socklen_t sender_len = sizeof(sender_addr);

    while (true) {
        int bytes = recvfrom(udp_sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                             reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);

        if (bytes >= static_cast<int>(sizeof(GamePacketHeader))) {
            auto* header = reinterpret_cast<GamePacketHeader*>(buffer);
            std::span<const uint8_t> payload(buffer + sizeof(GamePacketHeader), bytes - sizeof(GamePacketHeader));

            // Dispatch to UDP Controller
            handler(sender_addr, *header, payload);
        }
    }
}


constexpr char IPC_SOCKET_PATH[] = "/tmp/ogg_server_ipc.sock";

inline bool send_ipc_command(const char* cmd) {
    int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return false;
    }

    ::write(sock, cmd, std::strlen(cmd));
    ::close(sock);
    return true;
}

template<typename CommandCallback>
inline void run_ipc_listener(CommandCallback callback) {
    ::unlink(IPC_SOCKET_PATH);
    int server_sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (::bind(server_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(server_sock, 5) < 0) {
        ::close(server_sock);
        return;
    }

    while (true) {
        int client_sock = ::accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            char buf[64] = {0};
            ssize_t bytes = ::read(client_sock, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                buf[bytes] = '\0';
                callback(buf);
            }
            ::close(client_sock);
        }
    }
    ::close(server_sock);
}

} // namespace ogg::net

#endif // OGG_NIX_NET_HPP
