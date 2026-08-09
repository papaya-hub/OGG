#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ogg::server_monitor {

struct Target {
    std::wstring host;
    std::uint16_t port = 0;
    bool local_probe = false;
    std::wstring display_label;
};

inline std::vector<Target> default_targets() {
    return {
        { L"127.0.0.1", 8123, true, L"" },
        { L"ogg.sendermesh.com", 8123, false, L"" },
        { L"in.msheriff.com", 8123, false, L"" },
    };
}

inline std::vector<Target> launcher_targets() {
    return {
        { L"127.0.0.1", 8123, true, L"local" },
        { L"ogg.sendermesh.com", 8123, false, L"server1" },
        { L"in.msheriff.com", 8123, false, L"server2" },
    };
}

struct BadgeSnapshot {
    std::wstring label;
    bool up = false;
    bool polling = false;
    bool checking = false;
    bool local_probe = false;
};

class Monitor {
public:
    using ChangeHandler = void (*)(void* user_data);

    Monitor() = default;
    ~Monitor();

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    void set_change_handler(void* user_data, ChangeHandler handler);

    void start(const std::vector<Target>& targets);
    void stop();
    std::vector<BadgeSnapshot> snapshot() const;
    std::size_t entry_count() const;
    void recheck(std::size_t index);

private:
#if defined(_WIN32)
    void* state_ = nullptr;
    void* change_user_data_ = nullptr;
    ChangeHandler change_handler_ = nullptr;
#endif
};

} // namespace ogg::server_monitor
