cmake_minimum_required(VERSION 3.15)
project(OffGridGames LANGUAGES CXX)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define the executables
add_executable(ogg.client src/OffGridGamer.OffGridGames.Client/main.cpp)
add_executable(ogg.server src/OffGridGamer.OffGridGames.Server/main.cpp)
add_executable(ogg.winsvc src/OffGridGamer.OffGridGames.WindowsService/main.cpp)
