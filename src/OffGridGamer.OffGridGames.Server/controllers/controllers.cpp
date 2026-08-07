#include "controllers.hpp"
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdio>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ogg::controllers {

// Cached HTML content buffer loaded on startup
// Fallback HTML string:
static std::string g_index_html = "<html>OGG.Server</html>";

// Helper to locate executable path on Windows
std::filesystem::path get_exe_directory() {
#if defined(_WIN32)
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

void load_web_assets() {
    // Dynamically build path: <EXE_DIR>/public_html/index.html
    std::filesystem::path html_path = get_exe_directory() / "public_html" / "index.html";

    std::ifstream file(html_path, std::ios::binary);
    if (file.is_open()) {
        g_index_html = std::string((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        std::printf("[HTTP] Loaded %ls into memory.\n", html_path.c_str());
    } else {
        std::printf("[HTTP] Could not find %ls, using fallback 'OGG.Server'\n", html_path.c_str());
    }
}


ogg::net::HttpResponse handle_http_request(const ogg::net::HttpRequest& req) {
    if (req.path == "/" || req.path == "/index.html") {
        return {
            .status_code = 200,
            .content_type = "text/html",
            .body = g_index_html
        };
    }

    if (req.path == "/status") {
        return {
            .status_code = 200,
            .content_type = "text/plain",
            .body = "OGG.Server Active"
        };
    }

    return {
        .status_code = 404,
        .content_type = "text/plain",
        .body = "404 Not Found"
    };
}

void handle_tcp_packet(uint64_t client_id, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload) {
    std::printf("[TCP %llu] Packet ID: 0x%04X, Length: %d\n", client_id, header.packet_id, header.length);
}

void handle_udp_datagram(const sockaddr_in& sender, const ogg::net::GamePacketHeader& header, std::span<const uint8_t> payload) {
    std::printf("[UDP] Packet ID: 0x%04X\n", header.packet_id);
}

} // namespace ogg::controllers
