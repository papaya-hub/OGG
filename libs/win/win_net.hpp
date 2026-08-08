#ifndef OGG_WIN_NET_HPP
#define OGG_WIN_NET_HPP

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

namespace ogg::net {

using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;
constexpr int SocketError = SOCKET_ERROR;

enum class IOOperation { Read, Write };

struct PerIoContext {
    WSAOVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[1024];
    IOOperation operation;
};

struct PlatformContext {
    PlatformContext() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~PlatformContext() {
        WSACleanup();
    }
    PlatformContext(const PlatformContext&) = delete;
    PlatformContext& operator=(const PlatformContext&) = delete;
};

inline void close_socket(Socket sock) {
    if (sock != InvalidSocket) {
        closesocket(sock);
    }
}

inline int set_reuse_addr(Socket sock) {
    int opt = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
}

inline Socket bind_and_listen(unsigned short port, int backlog = 128) {
    Socket server_fd = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (server_fd == InvalidSocket) return InvalidSocket;

    set_reuse_addr(server_fd);

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

// C10K IOCP Event Reactor
inline void run_iocp_event_loop(Socket server_fd) {
    HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!hIOCP) {
        std::puts("Failed to create IOCP handle.");
        return;
    }

    std::puts("IOCP Event Loop running...");

    const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 10\r\n"
        "Connection: close\r\n\r\n"
        "OGG.Server";

    while (true) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        Socket client_fd = WSAAccept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len, NULL, 0);

        if (client_fd != InvalidSocket) {
            // Bind newly accepted client socket to IOCP port
            CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_fd), hIOCP, static_cast<ULONG_PTR>(client_fd), 0);

            PerIoContext* ioCtx = new PerIoContext();
            ZeroMemory(&(ioCtx->overlapped), sizeof(WSAOVERLAPPED));
            ioCtx->operation = IOOperation::Read;
            ioCtx->wsaBuf.buf = ioCtx->buffer;
            ioCtx->wsaBuf.len = sizeof(ioCtx->buffer);

            DWORD flags = 0;
            DWORD bytesRecv = 0;

            // Post initial async read
            if (WSARecv(client_fd, &(ioCtx->wsaBuf), 1, &bytesRecv, &flags, &(ioCtx->overlapped), NULL) == SOCKET_ERROR) {
                if (WSAGetLastError() != ERROR_IO_PENDING) {
                    delete ioCtx;
                    close_socket(client_fd);
                    continue;
                }
            }

            // Process completion event asynchronously
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED pOverlapped = nullptr;

            BOOL result = GetQueuedCompletionStatus(hIOCP, &bytesTransferred, &completionKey, &pOverlapped, 5000);
            if (result && pOverlapped) {
                PerIoContext* completedCtx = reinterpret_cast<PerIoContext*>(pOverlapped);
                Socket activeClient = static_cast<Socket>(completionKey);

                if (bytesTransferred > 0 && completedCtx->operation == IOOperation::Read) {
                    send(activeClient, response, static_cast<int>(sizeof(response) - 1), 0);
                }

                close_socket(activeClient);
                delete completedCtx;
            }
        }
    }

    CloseHandle(hIOCP);
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

constexpr wchar_t IPC_PIPE_NAME[] = L"\\\\.\\pipe\\ogg_server_ipc";

// Send signal from CLI process to running server
inline bool send_ipc_command(const char* cmd) {
    HANDLE hPipe = CreateFileW(IPC_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    WriteFile(hPipe, cmd, static_cast<DWORD>(std::strlen(cmd)), &written, NULL);
    CloseHandle(hPipe);
    return true;
}

// Background thread in main server to listen for signals
template<typename CommandCallback>
inline void run_ipc_listener(CommandCallback callback) {
    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            IPC_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1024, 1024, 0, NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) break;

        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buf[64] = {0};
            DWORD bytesRead = 0;
            if (ReadFile(hPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                callback(buf);
            }
        }
        CloseHandle(hPipe);
    }
}

} // namespace ogg::net

#endif // OGG_WIN_NET_HPP
