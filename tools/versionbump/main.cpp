#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::vector<int> parse_version_parts(const std::string& version) {
    std::vector<int> parts;
    std::size_t index = 0;
    while (index < version.size()) {
        const std::size_t next = version.find('.', index);
        const std::string part = version.substr(index, next == std::string::npos ? std::string::npos : next - index);
        parts.push_back(std::stoi(part));
        if (next == std::string::npos) break;
        index = next + 1;
    }
    return parts;
}

std::string format_version(const std::vector<int>& parts) {
    std::string out = "v";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out.push_back('.');
        out += std::to_string(parts[i]);
    }
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    const char* path = argc > 1 ? argv[1] : "src/version.txt";

    std::ifstream in(path);
    if (!in.is_open()) {
        std::fprintf(stderr, "versionbump: cannot read %s\n", path);
        return 1;
    }

    std::string line;
    std::getline(in, line);
    in.close();

    const std::string old_version = trim(line);
    if (old_version.empty() || old_version[0] != 'v') {
        std::fprintf(stderr, "versionbump: invalid version '%s'\n", old_version.c_str());
        return 1;
    }

    std::vector<int> parts = parse_version_parts(old_version.substr(1));
    if (parts.empty()) {
        std::fprintf(stderr, "versionbump: invalid version '%s'\n", old_version.c_str());
        return 1;
    }

    parts.back() += 1;
    const std::string new_version = format_version(parts);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "versionbump: cannot write %s\n", path);
        return 1;
    }

    out << new_version << '\n';
    if (!out) {
        std::fprintf(stderr, "versionbump: write failed for %s\n", path);
        return 1;
    }

    std::printf("versionbump: %s -> %s\n", old_version.c_str(), new_version.c_str());
    return 0;
}
