#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <objbase.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#include "http_client.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

volatile long g_openai_api_calls = 0;
volatile long g_gemini_api_calls = 0;

void record_api_call(const wchar_t* host) {
    if (!host) return;
    if (_wcsicmp(host, L"api.openai.com") == 0) {
        InterlockedIncrement(&g_openai_api_calls);
    } else if (_wcsicmp(host, L"generativelanguage.googleapis.com") == 0) {
        InterlockedIncrement(&g_gemini_api_calls);
    }
}

namespace {

constexpr DWORD kResolveTimeoutMs = 3000;
constexpr DWORD kConnectTimeoutMs = 3000;
constexpr DWORD kPatchSendTimeoutMs = 5000;
constexpr DWORD kPatchReceiveTimeoutMs = 5000;
constexpr DWORD kDownloadSendTimeoutMs = 10000;
constexpr DWORD kDownloadReceiveTimeoutMs = 60000;

void apply_timeouts(HINTERNET session, DWORD send_timeout_ms, DWORD receive_timeout_ms) {
    WinHttpSetTimeouts(
        session,
        kResolveTimeoutMs,
        kConnectTimeoutMs,
        send_timeout_ms,
        receive_timeout_ms
    );
}

void apply_secure_protocols(HINTERNET session) {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#if defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
}

HINTERNET open_session(DWORD send_timeout_ms, DWORD receive_timeout_ms) {
    HINTERNET session = WinHttpOpen(
        L"OGG.HttpClient/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!session) return nullptr;
    apply_timeouts(session, send_timeout_ms, receive_timeout_ms);
    apply_secure_protocols(session);
    return session;
}

struct ComApartment {
    bool should_uninitialize = false;
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        should_uninitialize = (hr == S_OK);
    }
    ~ComApartment() {
        if (should_uninitialize) CoUninitialize();
    }
};

} // namespace

namespace ogg::http_client {

void attach_stdio_console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
}

std::wstring to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::string basename(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string body_as_string(const std::vector<std::uint8_t>& body) {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

std::string build_http_url(const std::wstring& host, std::uint16_t port, const char* path) {
    char host_utf8[256]{};
    WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, host_utf8, sizeof(host_utf8), nullptr, nullptr);
    std::string url = "http://";
    url += host_utf8;
    url += ':';
    url += std::to_string(port);
    if (path && path[0]) {
        if (path[0] != '/') url += '/';
        url += path;
    }
    return url;
}

bool http_get(
    const std::wstring& host,
    std::uint16_t port,
    const std::wstring& path,
    int& status_code,
    std::vector<std::uint8_t>& body
) {
    body.clear();
    status_code = 0;

    ComApartment com;

    HINTERNET session = open_session(kPatchSendTimeoutMs, kPatchReceiveTimeoutMs);
    if (!session) {
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const bool sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    const bool received = sent && WinHttpReceiveResponse(request, nullptr);
    if (!received) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    );
    status_code = static_cast<int>(status);

    std::array<std::uint8_t, 4096> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            break;
        }
        if (read == 0) break;
        const std::size_t offset = body.size();
        body.resize(offset + read);
        std::memcpy(body.data() + offset, buffer.data(), read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

bool download_file(
    const std::wstring& host,
    std::uint16_t port,
    const std::wstring& path,
    const fs::path& destination,
    const std::function<void(std::size_t received, std::size_t total)>& on_progress
) {
    ComApartment com;

    HINTERNET session = open_session(kDownloadSendTimeoutMs, kDownloadReceiveTimeoutMs);
    if (!session) {
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const bool sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    const bool received = sent && WinHttpReceiveResponse(request, nullptr);
    if (!received) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    );
    if (status != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD content_length = 0;
    DWORD content_length_size = sizeof(content_length);
    const bool has_length = WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &content_length,
        &content_length_size,
        WINHTTP_NO_HEADER_INDEX
    );

    fs::path temp_path = destination;
    temp_path += ".download";

    FILE* file = _wfopen(temp_path.c_str(), L"wb");
    if (!file) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::size_t received_total = 0;
    const std::size_t total_size = has_length ? static_cast<std::size_t>(content_length) : 0;

    std::array<std::uint8_t, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            break;
        }
        if (read == 0) break;

        const std::size_t written = fwrite(buffer.data(), 1, read, file);
        if (written != read) {
            fclose(file);
            fs::remove(temp_path);
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        received_total += read;
        if (on_progress) {
            on_progress(received_total, total_size);
        }
    }

    fclose(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (received_total == 0) {
        fs::remove(temp_path);
        return false;
    }

    fs::remove(destination);
    fs::rename(temp_path, destination);
    return true;
}

bool https_request(
    const wchar_t* host,
    const wchar_t* path,
    const wchar_t* method,
    const std::string& extra_headers,
    const std::vector<std::uint8_t>& request_body,
    int& status_code,
    std::vector<std::uint8_t>& response_body
) {
    response_body.clear();
    status_code = 0;
    if (!host || !path || !method) return false;

    record_api_call(host);

    ComApartment com;

    HINTERNET session = open_session(kDownloadSendTimeoutMs, kDownloadReceiveTimeoutMs);
    if (!session) return false;

    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        method,
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::wstring headers = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    if (!extra_headers.empty()) {
        headers += to_wide(extra_headers);
        if (!extra_headers.empty() && extra_headers.back() != '\n') headers += L"\r\n";
    }

    const bool sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(-1),
        request_body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<BYTE*>(request_body.data()),
        static_cast<DWORD>(request_body.size()),
        static_cast<DWORD>(request_body.size()),
        0
    );
    const bool received = sent && WinHttpReceiveResponse(request, nullptr);
    if (!received) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    );
    status_code = static_cast<int>(status);

    std::array<std::uint8_t, 8192> buffer{};
    constexpr std::size_t kMaxResponseBytes = 64u * 1024u * 1024u;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) break;
        if (read == 0) break;
        if (response_body.size() > kMaxResponseBytes || read > buffer.size()) break;
        const std::size_t offset = response_body.size();
        if (offset + static_cast<std::size_t>(read) > kMaxResponseBytes) break;
        response_body.resize(offset + read);
        std::memcpy(response_body.data() + offset, buffer.data(), read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

int api_openai_calls() {
    return static_cast<int>(InterlockedCompareExchange(&g_openai_api_calls, 0, 0));
}

int api_gemini_calls() {
    return static_cast<int>(InterlockedCompareExchange(&g_gemini_api_calls, 0, 0));
}

} // namespace ogg::http_client
