#include "http_websocket.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace ogg::net {

namespace {

std::string to_lower_copy(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string extract_header_value(const std::string& request, const char* header_name) {
    const std::string needle = std::string(header_name) + ":";
    const std::string lower = to_lower_copy(request);
    const std::string needle_lower = to_lower_copy(needle);
    const std::size_t pos = lower.find(needle_lower);
    if (pos == std::string::npos) return {};
    std::size_t start = pos + needle.size();
    while (start < request.size() && (request[start] == ' ' || request[start] == '\t')) ++start;
    const std::size_t end = request.find("\r\n", start);
    if (end == std::string::npos) return {};
    return request.substr(start, end - start);
}

constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct Sha1 {
    std::uint32_t h[5]{};
    std::uint64_t len = 0;
    std::uint8_t buf[64]{};
    std::size_t buf_len = 0;

    static std::uint32_t rol(std::uint32_t v, int bits) {
        return (v << bits) | (v >> (32 - bits));
    }

    void process_block(const std::uint8_t* block) {
        std::uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    void update(const std::uint8_t* data, std::size_t size) {
        len += size * 8;
        while (size > 0) {
            const std::size_t take = std::min<std::size_t>(size, 64 - buf_len);
            std::memcpy(buf + buf_len, data, take);
            buf_len += take;
            data += take;
            size -= take;
            if (buf_len == 64) {
                process_block(buf);
                buf_len = 0;
            }
        }
    }

    void finalize(std::uint8_t out[20]) {
        buf[buf_len++] = 0x80;
        if (buf_len > 56) {
            while (buf_len < 64) buf[buf_len++] = 0;
            process_block(buf);
            buf_len = 0;
        }
        while (buf_len < 56) buf[buf_len++] = 0;
        for (int i = 7; i >= 0; --i) {
            buf[buf_len++] = static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF);
        }
        process_block(buf);
        for (int i = 0; i < 5; ++i) {
            out[i * 4] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFF);
            out[i * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFF);
            out[i * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFF);
            out[i * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xFF);
        }
    }

    Sha1() {
        h[0] = 0x67452301;
        h[1] = 0xEFCDAB89;
        h[2] = 0x98BADCFE;
        h[3] = 0x10325476;
        h[4] = 0xC3D2E1F0;
    }
};

std::string base64_encode(const std::uint8_t* data, std::size_t len) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
            ((i + 1 < len) ? (static_cast<std::uint32_t>(data[i + 1]) << 8) : 0) |
            ((i + 2 < len) ? static_cast<std::uint32_t>(data[i + 2]) : 0);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kTable[n & 63] : '=');
    }
    return out;
}

std::string compute_accept_key(const std::string& client_key) {
    const std::string combined = client_key + kWsGuid;
    Sha1 sha1{};
    sha1.update(reinterpret_cast<const std::uint8_t*>(combined.data()), combined.size());
    std::uint8_t digest[20]{};
    sha1.finalize(digest);
    return base64_encode(digest, 20);
}

bool send_all(WsSocket socket, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        const int n = send(socket, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool recv_until(WsSocket socket, std::string& buffer, const char* marker, int timeout_ms) {
#if defined(_WIN32)
    const DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
    char chunk[1024];
    while (buffer.find(marker) == std::string::npos) {
        const int n = recv(socket, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.size() > 16384) return false;
    }
    return true;
}

} // namespace

bool is_websocket_upgrade(const std::string& request, std::string_view path) {
    if (path != "/api/ogg/ws") return false;
    const std::string lower = to_lower_copy(request);
    return lower.find("upgrade: websocket") != std::string::npos;
}

bool send_websocket_accept(WsSocket client_fd, const std::string& request) {
    const std::string key = extract_header_value(request, "Sec-WebSocket-Key");
    if (key.empty()) return false;
    const std::string accept = compute_accept_key(key);
    char response[512];
    const int len = std::snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept.c_str()
    );
    if (len <= 0) return false;
    return send_all(client_fd, response, len);
}

void run_websocket_session(WsSocket client_fd) {
#if defined(_WIN32)
    const DWORD tv = 65000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
    std::uint8_t header[2];
    while (true) {
        const int n = recv(client_fd, reinterpret_cast<char*>(header), 2, 0);
        if (n != 2) break;
        const bool masked = (header[1] & 0x80) != 0;
        std::uint64_t payload_len = header[1] & 0x7F;
        if (payload_len == 126) {
            std::uint8_t ext[2];
            if (recv(client_fd, reinterpret_cast<char*>(ext), 2, 0) != 2) break;
            payload_len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
        } else if (payload_len == 127) {
            std::uint8_t ext[8];
            if (recv(client_fd, reinterpret_cast<char*>(ext), 8, 0) != 8) break;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | ext[i];
        }
        std::uint8_t mask[4]{};
        if (masked) {
            if (recv(client_fd, reinterpret_cast<char*>(mask), 4, 0) != 4) break;
        }
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_len));
        std::size_t got = 0;
        while (got < payload.size()) {
            const int r = recv(
                client_fd,
                reinterpret_cast<char*>(payload.data() + got),
                static_cast<int>(payload.size() - got),
                0
            );
            if (r <= 0) {
                got = payload.size() + 1;
                break;
            }
            got += static_cast<std::size_t>(r);
        }
        if (got > payload.size()) break;
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % 4];
            }
        }
        const std::uint8_t opcode = header[0] & 0x0F;
        if (opcode == 0x8) break;
        if (opcode == 0x9) {
            const std::uint8_t pong[2] = {0x8A, 0x00};
            send(client_fd, reinterpret_cast<const char*>(pong), 2, 0);
        }
    }
}

bool websocket_client_connect(const wchar_t* host, std::uint16_t port, WsSocket& socket_out) {
    socket_out = kInvalidWsSocket;
    if (!host) return false;

    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const std::wstring port_str = std::to_wstring(port);
    ADDRINFOW* result = nullptr;
    if (GetAddrInfoW(host, port_str.c_str(), &hints, &result) != 0 || !result) {
        if (result) FreeAddrInfoW(result);
        return false;
    }

    WsSocket sock = kInvalidWsSocket;
    for (ADDRINFOW* ptr = result; ptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == kInvalidWsSocket) continue;
        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) break;
        closesocket(sock);
        sock = kInvalidWsSocket;
    }
    FreeAddrInfoW(result);
    if (sock == kInvalidWsSocket) return false;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::array<std::uint8_t, 16> key_bytes{};
    for (auto& b : key_bytes) b = static_cast<std::uint8_t>(dist(gen));
    const std::string key = base64_encode(key_bytes.data(), key_bytes.size());

    char host_utf8[256]{};
    WideCharToMultiByte(CP_UTF8, 0, host, -1, host_utf8, sizeof(host_utf8), nullptr, nullptr);

    char request[1024];
    const int req_len = std::snprintf(
        request,
        sizeof(request),
        "GET /api/ogg/ws HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n",
        host_utf8,
        static_cast<unsigned>(port),
        key.c_str()
    );
    if (req_len <= 0 || !send_all(sock, request, req_len)) {
        closesocket(sock);
        return false;
    }

    std::string response;
    if (!recv_until(sock, response, "\r\n\r\n", 8000) ||
        response.find("101") == std::string::npos) {
        closesocket(sock);
        return false;
    }

    socket_out = sock;
    return true;
}

bool websocket_client_send_ping(WsSocket socket) {
    std::uint8_t frame[6] = {0x89, 0x80, 0, 0, 0, 0};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 2; i < 6; ++i) frame[i] = static_cast<std::uint8_t>(dist(gen));
    return send_all(socket, reinterpret_cast<const char*>(frame), 6);
}

bool websocket_client_read_frame(WsSocket socket, int timeout_ms) {
#if defined(_WIN32)
    const DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
    std::uint8_t header[2];
    const int n = recv(socket, reinterpret_cast<char*>(header), 2, 0);
    if (n != 2) return false;
    std::uint64_t payload_len = header[1] & 0x7F;
    if (payload_len == 126) {
        std::uint8_t ext[2];
        if (recv(socket, reinterpret_cast<char*>(ext), 2, 0) != 2) return false;
        payload_len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        std::uint8_t ext[8];
        if (recv(socket, reinterpret_cast<char*>(ext), 8, 0) != 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | ext[i];
    }
    std::vector<char> skip(static_cast<std::size_t>(payload_len));
    std::size_t got = 0;
    while (got < skip.size()) {
        const int r = recv(socket, skip.data() + got, static_cast<int>(skip.size() - got), 0);
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    const std::uint8_t opcode = header[0] & 0x0F;
    return opcode == 0xA || opcode == 0x9 || opcode == 0x1 || opcode == 0x0;
}

} // namespace ogg::net
