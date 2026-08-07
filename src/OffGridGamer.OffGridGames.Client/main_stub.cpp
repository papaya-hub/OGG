#include "version.hpp"
#include <cstdio>

int main() {
    std::printf("OGG Client %s (GUI requires Windows)\n", ogg::VERSION);
    return 0;
}
