cmake_minimum_required(VERSION 3.15)
project(OffGridGames LANGUAGES CXX)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Forced Release Mode" FORCE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_compile_options(
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
)

add_link_options(
    -static-libgcc
    -static-libstdc++
    -Wl,--gc-sections
    -s
)

# Define executables
add_executable(ogg.client src/OffGridGamer.OffGridGames.Client/main.cpp)
add_executable(ogg.server src/OffGridGamer.OffGridGames.Server/main.cpp)
add_executable(ogg.winsvc src/OffGridGamer.OffGridGames.WindowsService/main.cpp)

# Target-specific platform includes
if(WIN32)
    target_include_directories(ogg.server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/win32)
    target_link_libraries(ogg.server PRIVATE ws2_32)
else()
    target_include_directories(ogg.server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/nix)
endif()
