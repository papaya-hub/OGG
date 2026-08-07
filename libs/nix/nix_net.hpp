#ifndef OGG_NIX_NET_HPP
#define OGG_NIX_NET_HPP

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace ogg::net {

using Socket = int;
constexpr Socket InvalidSocket = -1;
constexpr int SocketError = -1;

struct PlatformContext {
    // No explicit socket subsystem initialization needed on Linux/POSIX
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

inline int set_reuse_addr(Socket sock) {
    int opt = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

inline Socket bind_and_listen(unsigned short port, int backlog = 128) {
    // 1. Create Socket
    Socket server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == InvalidSocket) {
        std::puts("Failed to create socket.");
        return InvalidSocket;
    }

    // 2. Set Reuse Address
    if (set_reuse_addr(server_fd) == SocketError) {
        std::puts("Failed to set SO_REUSEADDR.");
        close_socket(server_fd);
        return InvalidSocket;
    }

    // 3. Bind
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SocketError) {
        std::puts("Bind failed.");
        close_socket(server_fd);
        return InvalidSocket;
    }

    // 4. Listen
    if (listen(server_fd, backlog) == SocketError) {
        std::puts("Listen failed.");
        close_socket(server_fd);
        return InvalidSocket;
    }

    return server_fd;
}

} // namespace ogg::net

#endif // OGG_NIX_NET_HPP
