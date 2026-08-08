cmake_minimum_required(VERSION 3.15)
project(OffGridGames LANGUAGES CXX)

set(CMAKE_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Forced Release Mode" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Os -ffunction-sections -fdata-sections -fno-rtti)

string(TOLOWER "${CMAKE_CXX_COMPILER}" _ogg_cxx_compiler)
if(WIN32 OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR _ogg_cxx_compiler MATCHES "ucrt64|mingw64|mingw32|w64-mingw32")
    set(OGG_PLATFORM_WINDOWS TRUE)
    message(STATUS "Platform: Windows/MinGW (_WIN32)")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    add_compile_options(-fno-exceptions)
    add_link_options(-static-libgcc -static-libstdc++ -Wl,--gc-sections -s)
else()
    set(OGG_PLATFORM_WINDOWS FALSE)
    message(STATUS "Platform: POSIX (nix)")
    set(CMAKE_CXX_STANDARD 20)
    add_link_options(-Wl,--gc-sections -s)
endif()

file(READ "${CMAKE_SOURCE_DIR}/src/version.txt" OGG_VERSION_RAW)
string(STRIP "${OGG_VERSION_RAW}" OGG_VERSION)
set(OGG_CLIENT_EXE_BASENAME "ogg.client.${OGG_VERSION}")
set(OGG_CLIENT_EXE_NAME "${OGG_CLIENT_EXE_BASENAME}.exe")
set(OGG_CLIENT_PATCH_NAME "ogg.client.${OGG_VERSION}.patch")

configure_file(
    "${CMAKE_SOURCE_DIR}/libs/common/version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/version.hpp"
    @ONLY
)
set(OGG_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")

set(OGG_UI_SOURCES
    ${CMAKE_SOURCE_DIR}/libs/common/ui/window.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ui/render.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ui/widgets.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ui/shell_window.cpp
)

set(OGG_CLIENT_UI_SOURCES
    ${OGG_UI_SOURCES}
    ${CMAKE_SOURCE_DIR}/libs/common/ui/webview_host.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ui/hero_image.cpp
)

if(OGG_PLATFORM_WINDOWS)
    add_executable(ogg.client WIN32
        src/OffGridGamer.OffGridGames.Client/main.cpp
        src/OffGridGamer.OffGridGames.Client/client_ui_gui.cpp
        src/OffGridGamer.OffGridGames.Client/hero_art.rc
        libs/common/http_client.cpp
        libs/win/webview2/webview_loader.rc
        ${OGG_CLIENT_UI_SOURCES}
    )
else()
    add_executable(ogg.client src/OffGridGamer.OffGridGames.Client/main_stub.cpp)
endif()
set_target_properties(ogg.client PROPERTIES OUTPUT_NAME "${OGG_CLIENT_EXE_BASENAME}")

add_executable(ogg.server
    src/OffGridGamer.OffGridGames.Server/main.cpp
    src/OffGridGamer.OffGridGames.Server/controllers/controllers.cpp
    libs/common/http_wire.cpp
)

if(OGG_PLATFORM_WINDOWS)
    target_sources(ogg.server PRIVATE libs/win/http_server.cpp)
else()
    target_sources(ogg.server PRIVATE libs/nix/http_server.cpp)
endif()

add_executable(ogg.winsvc src/OffGridGamer.OffGridGames.WindowsService/main.cpp)

add_subdirectory(tools)

target_include_directories(ogg.client PRIVATE ${OGG_GENERATED_INCLUDE_DIR})
if(OGG_PLATFORM_WINDOWS)
    target_include_directories(ogg.client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/common
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/win/webview2/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Client
    )
    target_compile_options(ogg.client PRIVATE -fms-extensions)
    set_source_files_properties(libs/win/webview2/webview_loader.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/libs/win/webview2")
    set_source_files_properties(src/OffGridGamer.OffGridGames.Client/hero_art.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Client")
    target_link_libraries(ogg.client PRIVATE d2d1 dwrite dwmapi gdi32 gdiplus ole32 shlwapi uuid)
    add_custom_command(TARGET ogg.client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/libs/win/webview2/x64/WebView2Loader.dll
                $<TARGET_FILE_DIR:ogg.client>/WebView2Loader.dll
        COMMENT "Copying WebView2Loader.dll..."
    )
endif()
target_include_directories(ogg.server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/common
    ${OGG_GENERATED_INCLUDE_DIR}
)

if(OGG_PLATFORM_WINDOWS)
    target_include_directories(ogg.server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/win)
    target_link_libraries(ogg.server PRIVATE ws2_32)
else()
    target_include_directories(ogg.server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/nix)
    target_link_libraries(ogg.server PRIVATE pthread)
endif()

add_custom_command(TARGET ogg.client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:ogg.client>
            $<TARGET_FILE_DIR:ogg.client>/${OGG_CLIENT_PATCH_NAME}
    COMMENT "Creating client patch artifact..."
)

add_custom_command(TARGET ogg.server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Web/public_html
            $<TARGET_FILE_DIR:ogg.server>/public_html
    COMMENT "Copying Web assets to build directory..."
)

add_custom_command(TARGET ogg.server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ogg.server>/public_html
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:ogg.client>/${OGG_CLIENT_EXE_NAME}
            $<TARGET_FILE_DIR:ogg.server>/public_html/${OGG_CLIENT_EXE_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:ogg.client>/${OGG_CLIENT_PATCH_NAME}
            $<TARGET_FILE_DIR:ogg.server>/public_html/${OGG_CLIENT_PATCH_NAME}
    COMMENT "Copying client patch artifacts to server public_html..."
)

add_dependencies(ogg.server ogg.client)
