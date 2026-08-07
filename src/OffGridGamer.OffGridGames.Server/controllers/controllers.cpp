#include "controllers.hpp"
#include "version.hpp"
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdio>
#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ogg::controllers {

static std::string g_index_html = "<html>OGG.Server</html>";
static std::string g_patch_filename = ogg::CLIENT_EXE_NAME;

std::filesystem::path get_exe_directory() {
#if defined(_WIN32)
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

static std::filesystem::path public_html_path(const std::filesystem::path& relative) {
    return get_exe_directory() / "public_html" / relative;
}

static bool path_starts_with(std::string_view path, std::string_view prefix) {
    return path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix;
}

void load_web_assets() {
    std::filesystem::path html_path = public_html_path("index.html");

    std::ifstream file(html_path, std::ios::binary);
    if (file.is_open()) {
        g_index_html = std::string((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        std::printf("[HTTP] Loaded %ls into memory.\n", html_path.c_str());
    } else {
        std::printf("[HTTP] Could not find %ls, using fallback 'OGG.Server'\n", html_path.c_str());
    }

    g_patch_filename = ogg::CLIENT_EXE_NAME;
    std::printf("[PATCH] Serving client artifact: %s\n", g_patch_filename.c_str());
}

ogg::net::HttpResponse handle_http_request(const ogg::net::HttpRequest& req) {
    if (req.path == "/" || req.path == "/index.html") {
        return {
            .status_code = 200,
            .content_type = "text/html",
            .body = g_index_html
        };
    }

    if (req.path == "/client" || req.path == "/client/") {
        std::filesystem::path file_path = public_html_path("client/index.html");
        if (std::filesystem::is_regular_file(file_path)) {
            return {
                .status_code = 200,
                .content_type = "text/html",
                .file_path = file_path
            };
        }
    }

    if (req.path == "/status") {
        return {
            .status_code = 200,
            .content_type = "text/plain",
            .body = "OGG.Server Active"
        };
    }

    if (req.path == "/api/ogg/patch") {
        return {
            .status_code = 200,
            .content_type = "text/plain",
            .body = g_patch_filename
        };
    }

    std::filesystem::path file_relative;
    if (path_starts_with(req.path, "/public_html/")) {
        file_relative = std::filesystem::path(req.path.substr(std::string_view("/public_html/").size()));
    } else if (path_starts_with(req.path, "/")) {
        file_relative = std::filesystem::path(req.path.substr(1));
    }

    if (!file_relative.empty()) {
        std::filesystem::path file_path = public_html_path(file_relative);
        if (std::filesystem::is_regular_file(file_path)) {
            std::string filename = file_path.filename().string();
            std::string content_type = "application/octet-stream";
            if (filename.ends_with(".html")) content_type = "text/html";
            else if (filename.ends_with(".txt")) content_type = "text/plain";
            else if (filename.ends_with(".jpg") || filename.ends_with(".jpeg")) content_type = "image/jpeg";
            else if (filename.ends_with(".patch")) content_type = "application/octet-stream";

            return {
                .status_code = 200,
                .content_type = content_type,
                .file_path = file_path
            };
        }
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
