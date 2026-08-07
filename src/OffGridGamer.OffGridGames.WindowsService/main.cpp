#include <iostream>
#include <format> // Requires C++20

int main() {
    // C++20 text formatting
    std::cout << std::format("Hello, {}!\n", "WinSvc");
    return 0;
}
