#include "controllers.hpp"
#include <cstdio>

namespace ogg::controllers {

// Handle reliable TCP actions (Login, Item Pickup, Chat)
void handle_tcp_packet(uint64_t client_id, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload) {
    switch (header.packet_id) {
        case 0x0001: // CMD_LOGIN
            std::printf("[TCP %llu] Login Request\n", client_id);
            break;
        case 0x0002: // CMD_CHAT
            std::printf("[TCP %llu] Chat Message Received (%d bytes)\n", client_id, header.length);
            break;
        default:
            std::printf("[TCP %llu] Unknown Packet ID: 0x%04X\n", client_id, header.packet_id);
            break;
    }
}

// Handle fast unreliable UDP actions (Transform update, Aiming, Snapshots)
void handle_udp_datagram(const sockaddr_in& sender, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload) {
    switch (header.packet_id) {
        case 0x0100: // CMD_PLAYER_TRANSFORM
            // Fast low-latency position update
            break;
        case 0x0101: // CMD_PING
            // Reply with pong
            break;
    }
}

} // namespace ogg::controllers
