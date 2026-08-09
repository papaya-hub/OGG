#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

namespace ogg::net {

#if defined(_WIN32)
using WsSocket = SOCKET;
constexpr WsSocket kInvalidWsSocket = INVALID_SOCKET;
#else
using WsSocket = int;
constexpr WsSocket kInvalidWsSocket = -1;
#endif

bool is_websocket_upgrade(const std::string& request, std::string_view path);
bool send_websocket_accept(WsSocket client_fd, const std::string& request);
void run_websocket_session(WsSocket client_fd);

bool websocket_client_connect(const wchar_t* host, std::uint16_t port, WsSocket& socket_out);
bool websocket_client_send_ping(WsSocket socket);
bool websocket_client_read_frame(WsSocket socket, int timeout_ms);

} // namespace ogg::net
