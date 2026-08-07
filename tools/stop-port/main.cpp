#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <dirent.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif

static bool parse_port(const char* text, std::uint16_t& port) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0 || value > 65535) return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

#if defined(_WIN32)

static std::vector<DWORD> find_pids_on_port(std::uint16_t port) {
    std::vector<DWORD> pids;
    DWORD size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER) {
        return pids;
    }

    std::vector<std::uint8_t> buffer(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
        return pids;
    }

    const std::uint16_t target = port;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        const std::uint16_t local_port = ntohs(static_cast<u_short>(row.dwLocalPort & 0xFFFF));
        if (local_port == target && row.dwOwningPid != 0) {
            pids.push_back(row.dwOwningPid);
        }
    }
    return pids;
}

static bool stop_pid(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) return false;
    const BOOL ok = TerminateProcess(process, 1);
    CloseHandle(process);
    return ok != FALSE;
}

#else

static std::uint16_t parse_hex_port(const std::string& hex) {
    return static_cast<std::uint16_t>(std::strtoul(hex.c_str(), nullptr, 16));
}

static std::vector<pid_t> find_pids_for_inode(const std::string& inode) {
    std::vector<pid_t> pids;
    DIR* proc = opendir("/proc");
    if (!proc) return pids;

    while (auto* entry = readdir(proc)) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        const pid_t pid = static_cast<pid_t>(std::strtol(entry->d_name, nullptr, 10));
        const std::string fd_dir = std::string("/proc/") + entry->d_name + "/fd";
        DIR* fds = opendir(fd_dir.c_str());
        if (!fds) continue;

        while (auto* fd_entry = readdir(fds)) {
            if (fd_entry->d_name[0] < '0' || fd_entry->d_name[0] > '9') continue;
            const std::string fd_path = fd_dir + "/" + fd_entry->d_name;
            std::ifstream link(fd_path);
            if (!link) continue;
            std::string target;
            std::getline(link, target);
            if (target == "socket:[" + inode + "]") {
                pids.push_back(pid);
                break;
            }
        }
        closedir(fds);
    }
    closedir(proc);
    return pids;
}

static std::vector<pid_t> find_pids_on_port(std::uint16_t port) {
    std::vector<pid_t> pids;
    std::ifstream tcp("/proc/net/tcp");
    if (!tcp.is_open()) return pids;

    std::string line;
    std::getline(tcp, line);
    while (std::getline(tcp, line)) {
        std::istringstream iss(line);
        std::string sl, local, rem, st, tx, rx, inode_field;
        iss >> sl >> local >> rem >> st >> tx >> rx >> inode_field;
        const auto colon = local.find(':');
        if (colon == std::string::npos) continue;
        const std::uint16_t local_port = parse_hex_port(local.substr(colon + 1));
        if (local_port != port) continue;

        for (pid_t pid : find_pids_for_inode(inode_field)) {
            pids.push_back(pid);
        }
    }
    return pids;
}

static bool stop_pid(pid_t pid) {
    return kill(pid, SIGTERM) == 0 || kill(pid, SIGKILL) == 0;
}

#endif

static int stop_port(std::uint16_t port) {
    const auto pids = find_pids_on_port(port);
    if (pids.empty()) {
        std::printf("stop-port: no process listening on port %u\n", port);
        return 0;
    }

    int stopped = 0;
    for (const auto pid : pids) {
#if defined(_WIN32)
        if (stop_pid(static_cast<DWORD>(pid))) {
            std::printf("stop-port: stopped pid %lu on port %u\n", static_cast<unsigned long>(pid), port);
            ++stopped;
        } else {
            std::fprintf(stderr, "stop-port: failed to stop pid %lu\n", static_cast<unsigned long>(pid));
        }
#else
        if (stop_pid(pid)) {
            std::printf("stop-port: stopped pid %d on port %u\n", static_cast<int>(pid), port);
            ++stopped;
        } else {
            std::fprintf(stderr, "stop-port: failed to stop pid %d\n", static_cast<int>(pid));
        }
#endif
    }
    return stopped > 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ogg.stop-port <port> [port ...]\n");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        std::uint16_t port = 0;
        if (!parse_port(argv[i], port)) {
            std::fprintf(stderr, "stop-port: invalid port '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        if (stop_port(port) != 0) {
            rc = 1;
        }
    }
    return rc;
}
