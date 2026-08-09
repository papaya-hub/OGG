#include "server_monitor.hpp"

#include "http_websocket.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace ogg::server_monitor {

namespace {

#if defined(_WIN32)

struct EntryState {
    Target target;
    std::wstring label;
    std::atomic<bool> up{false};
    std::atomic<bool> polling{false};
    std::atomic<bool> checking{false};
    std::thread recheck_thread;
};

struct SharedState {
    std::vector<std::unique_ptr<EntryState>> entries;
    std::atomic<bool> running{false};
    std::thread local_thread;
    void* change_user_data = nullptr;
    Monitor::ChangeHandler change_handler = nullptr;
};

void notify_change(SharedState* state) {
    if (state && state->change_handler) {
        state->change_handler(state->change_user_data);
    }
}

std::wstring make_label(const Target& target) {
    if (!target.display_label.empty()) return target.display_label;
    return target.host + L":" + std::to_wstring(target.port);
}

bool tcp_probe(const wchar_t* host, std::uint16_t port) {
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

    bool ok = false;
    for (ADDRINFOW* ptr = result; ptr; ptr = ptr->ai_next) {
        SOCKET sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            ok = true;
            closesocket(sock);
            break;
        }
        closesocket(sock);
    }
    FreeAddrInfoW(result);
    return ok;
}

bool remote_ws_check_once(const Target& target) {
    ogg::net::WsSocket sock = ogg::net::kInvalidWsSocket;
    if (!ogg::net::websocket_client_connect(target.host.c_str(), target.port, sock)) {
        return false;
    }
    const bool alive = ogg::net::websocket_client_read_frame(sock, 10000);
    if (sock != ogg::net::kInvalidWsSocket) {
        closesocket(sock);
    }
    return alive;
}

void local_probe_loop(SharedState* state) {
    while (state->running.load(std::memory_order_acquire)) {
        for (auto& entry_ptr : state->entries) {
            EntryState& entry = *entry_ptr;
            if (!entry.target.local_probe) continue;
            const bool was_up = entry.up.load(std::memory_order_acquire);
            entry.polling.store(true, std::memory_order_release);
            const bool ok = tcp_probe(entry.target.host.c_str(), entry.target.port);
            entry.up.store(ok, std::memory_order_release);
            entry.polling.store(false, std::memory_order_release);
            if (ok != was_up) {
                notify_change(state);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void remote_recheck_worker(SharedState* state, std::size_t index) {
    EntryState& entry = *state->entries[index];
    const bool ok = remote_ws_check_once(entry.target);
    entry.up.store(ok, std::memory_order_release);
    entry.checking.store(false, std::memory_order_release);
    notify_change(state);
}

#endif

} // namespace

Monitor::~Monitor() {
    stop();
}

void Monitor::set_change_handler(void* user_data, ChangeHandler handler) {
    change_user_data_ = user_data;
    change_handler_ = handler;
#if defined(_WIN32)
    if (!state_) return;
    auto* state = static_cast<SharedState*>(state_);
    state->change_user_data = user_data;
    state->change_handler = handler;
#else
    (void)user_data;
    (void)handler;
#endif
}

void Monitor::start(const std::vector<Target>& targets) {
#if defined(_WIN32)
    stop();
    if (targets.empty()) return;

    static std::once_flag wsa_once;
    std::call_once(wsa_once, [] {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });

    auto* state = new SharedState{};
    state_ = state;
    state->entries.reserve(targets.size());
    for (const auto& target : targets) {
        auto entry = std::make_unique<EntryState>();
        entry->target = target;
        entry->label = make_label(target);
        state->entries.push_back(std::move(entry));
    }
    state->running.store(true, std::memory_order_release);

    bool has_local = false;
    for (const auto& entry_ptr : state->entries) {
        if (entry_ptr->target.local_probe) {
            has_local = true;
            break;
        }
    }
    if (has_local) {
        state->local_thread = std::thread(local_probe_loop, state);
    }
    state->change_user_data = change_user_data_;
    state->change_handler = change_handler_;
#else
    (void)targets;
#endif
}

void Monitor::stop() {
#if defined(_WIN32)
    if (!state_) return;
    auto* state = static_cast<SharedState*>(state_);
    state->running.store(false, std::memory_order_release);
    if (state->local_thread.joinable()) state->local_thread.join();
    for (auto& entry_ptr : state->entries) {
        EntryState& entry = *entry_ptr;
        if (entry.recheck_thread.joinable()) entry.recheck_thread.join();
    }
    delete state;
    state_ = nullptr;
#endif
}

std::vector<BadgeSnapshot> Monitor::snapshot() const {
    std::vector<BadgeSnapshot> out;
#if defined(_WIN32)
    if (!state_) return out;
    const auto* state = static_cast<const SharedState*>(state_);
    out.reserve(state->entries.size());
    for (const auto& entry_ptr : state->entries) {
        const EntryState& entry = *entry_ptr;
        BadgeSnapshot badge{};
        badge.label = entry.label;
        badge.up = entry.up.load(std::memory_order_acquire);
        badge.polling = entry.polling.load(std::memory_order_acquire);
        badge.checking = entry.checking.load(std::memory_order_acquire);
        badge.local_probe = entry.target.local_probe;
        out.push_back(std::move(badge));
    }
#endif
    return out;
}

std::size_t Monitor::entry_count() const {
#if defined(_WIN32)
    if (!state_) return 0;
    const auto* state = static_cast<const SharedState*>(state_);
    return state->entries.size();
#else
    return 0;
#endif
}

void Monitor::recheck(std::size_t index) {
#if defined(_WIN32)
    if (!state_) return;
    auto* state = static_cast<SharedState*>(state_);
    if (index >= state->entries.size()) return;

    EntryState& entry = *state->entries[index];
    if (entry.target.local_probe) return;

    bool expected = false;
    if (!entry.checking.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    if (entry.recheck_thread.joinable()) {
        entry.recheck_thread.join();
    }

    notify_change(state);
    entry.recheck_thread = std::thread(remote_recheck_worker, state, index);
#else
    (void)index;
#endif
}

} // namespace ogg::server_monitor
