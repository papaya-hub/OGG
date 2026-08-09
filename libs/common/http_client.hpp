#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ogg::http_client {

std::wstring to_wide(const std::string& text);
std::string trim(const std::string& text);
std::string basename(const std::string& path);
std::string body_as_string(const std::vector<std::uint8_t>& body);

#if defined(_WIN32)
std::string build_http_url(const std::wstring& host, std::uint16_t port, const char* path);
void attach_stdio_console();

bool http_get(
    const std::wstring& host,
    std::uint16_t port,
    const std::wstring& path,
    int& status_code,
    std::vector<std::uint8_t>& body
);

bool download_file(
    const std::wstring& host,
    std::uint16_t port,
    const std::wstring& path,
    const std::filesystem::path& destination,
    const std::function<void(std::size_t received, std::size_t total)>& on_progress = nullptr
);

bool https_request(
    const wchar_t* host,
    const wchar_t* path,
    const wchar_t* method,
    const std::string& extra_headers,
    const std::vector<std::uint8_t>& request_body,
    int& status_code,
    std::vector<std::uint8_t>& response_body
);

int api_openai_calls();
int api_gemini_calls();
#endif

} // namespace ogg::http_client
