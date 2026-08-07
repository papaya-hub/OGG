#pragma once

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#include <string_view>
#include <span>
#include <cstdint>

namespace ogg::net {

struct HttpRequest {
    std::string_view method;
    std::string_view path;
    std::string_view body;
};

struct HttpResponse {
    int status_code = 200;
    std::string_view content_type = "text/plain";
    std::string_view body;
};

#pragma pack(push, 1)
struct GamePacketHeader {
    uint16_t packet_id;
    uint16_t length;
};
#pragma pack(pop)

using HttpHandler = HttpResponse(*)(const HttpRequest& req);
using TcpPacketHandler = void(*)(uint64_t client_id, const GamePacketHeader& header, std::span<const uint8_t> payload);
using UdpDatagramHandler = void(*)(const sockaddr_in& sender, const GamePacketHeader& header, std::span<const uint8_t> payload);

} // namespace ogg::net

namespace ogg::controllers {
	void load_web_assets();
    ogg::net::HttpResponse handle_http_request(const ogg::net::HttpRequest& req);
	void handle_tcp_packet(uint64_t client_id, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload);
	void handle_udp_datagram(const sockaddr_in& sender, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload);
} // namespace ogg::controllers
