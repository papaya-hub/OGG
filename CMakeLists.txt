cmake_minimum_required(VERSION 3.15)

set(_ogg_cxx_compiler "${CMAKE_CXX_COMPILER}")
string(TOLOWER "${_ogg_cxx_compiler}" _ogg_cxx_compiler_lower)
if(_ogg_cxx_compiler_lower MATCHES "ucrt64|mingw64|mingw32|w64-mingw32" OR WIN32)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(OffGridGames LANGUAGES CXX)

set(CMAKE_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Forced Release Mode" FORCE)

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
    ${CMAKE_SOURCE_DIR}/libs/common/ui/client_shell.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ui/xml_ui.cpp
)

set(OGG_SHELL_MONITOR_SOURCES
    ${CMAKE_SOURCE_DIR}/libs/common/http_websocket.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/server_monitor.cpp
)

set(OGG_CLIENT_UI_SOURCES
    ${OGG_UI_SOURCES}
    ${CMAKE_SOURCE_DIR}/libs/common/ui/hero_image.cpp
)

if(OGG_PLATFORM_WINDOWS)
    add_executable(ogg.client WIN32
        src/OffGridGamer.OffGridGames.Client/main.cpp
        src/OffGridGamer.OffGridGames.Client/client_ui_gui.cpp
        src/OffGridGamer.OffGridGames.Client/hero_art.rc
        libs/common/http_client.cpp
        ${OGG_SHELL_MONITOR_SOURCES}
        ${OGG_CLIENT_UI_SOURCES}
    )
else()
    add_executable(ogg.client src/OffGridGamer.OffGridGames.Client/main_stub.cpp)
endif()
set_target_properties(ogg.client PROPERTIES OUTPUT_NAME "${OGG_CLIENT_EXE_BASENAME}")

if(OGG_PLATFORM_WINDOWS)
    set_target_properties(ogg.client PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

add_executable(ogg.server
    src/OffGridGamer.OffGridGames.Server/main.cpp
    src/OffGridGamer.OffGridGames.Server/controllers/controllers.cpp
    libs/common/http_wire.cpp
    libs/common/http_websocket.cpp
)

if(OGG_PLATFORM_WINDOWS)
    target_sources(ogg.server PRIVATE libs/win/http_server.cpp)
else()
    target_sources(ogg.server PRIVATE libs/nix/http_server.cpp)
endif()

add_executable(ogg.winsvc src/OffGridGamer.OffGridGames.WindowsService/main.cpp)

add_subdirectory(tools)

if(OGG_PLATFORM_WINDOWS)
function(ogg_sync_settings_prebuild target)
    add_dependencies(${target} ogg.sync-settings)
    add_custom_command(TARGET ${target} PRE_BUILD
        COMMAND $<TARGET_FILE:ogg.sync-settings>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Syncing AppData settings into build/generated..."
    )
endfunction()
endif()

target_include_directories(ogg.client PRIVATE ${OGG_GENERATED_INCLUDE_DIR})
if(OGG_PLATFORM_WINDOWS)
    target_include_directories(ogg.client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Client
    )
    target_compile_options(ogg.client PRIVATE -fms-extensions)
    set(OGG_HERO_ART_JPG ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Client/hero_art.jpg)
    set_source_files_properties(src/OffGridGamer.OffGridGames.Client/hero_art.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Client"
        OBJECT_DEPENDS "${OGG_HERO_ART_JPG}")
    target_link_libraries(ogg.client PRIVATE d2d1 dwrite dwmapi gdi32 gdiplus ole32 shlwapi uuid comctl32 winhttp ws2_32)
    add_custom_command(TARGET ogg.client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/generated/client_login.xml
                $<TARGET_FILE_DIR:ogg.client>/client_login.xml
        COMMENT "Copying themed client_login.xml..."
    )

    add_executable(ogg.admin WIN32
        src/OffGridGamer.OffGridGames.Admin/main.cpp
        src/OffGridGamer.OffGridGames.Admin/admin_ui_gui.cpp
        src/OffGridGamer.OffGridGames.Admin/admin_window.cpp
        libs/common/login_image_assets.cpp
        libs/common/generated_media_assets.cpp
        libs/common/app_settings.cpp
        libs/common/ai_image.cpp
        libs/common/ai_video.cpp
        libs/common/http_client.cpp
        ${OGG_SHELL_MONITOR_SOURCES}
        libs/common/ui/hero_image.cpp
        ${OGG_UI_SOURCES}
    )
    set_target_properties(ogg.admin PROPERTIES
        OUTPUT_NAME "ogg.admin"
        INTERPROCEDURAL_OPTIMIZATION FALSE
    )
    target_include_directories(ogg.admin PRIVATE
        ${OGG_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin
    )
    target_link_libraries(ogg.admin PRIVATE d2d1 dwrite dwmapi gdi32 gdiplus ole32 winhttp comctl32 ws2_32)
    add_custom_command(TARGET ogg.admin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_dashboard.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_dashboard.xml
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_users.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_users.xml
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_assets_general.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_assets_general.xml
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_settings_ai.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_settings_ai.xml
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_settings_appearance.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_settings_appearance.xml
            COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_settings_typography.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_settings_typography.xml
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Admin/admin_settings_docs.xml
                $<TARGET_FILE_DIR:ogg.admin>/admin_settings_docs.xml
        COMMENT "Copying admin XML UI..."
    )
    ogg_sync_settings_prebuild(ogg.client)
    ogg_sync_settings_prebuild(ogg.admin)
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
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/generated/client_login.xml
            $<TARGET_FILE_DIR:ogg.server>/public_html/client_login.xml
    COMMENT "Copying client patch artifacts to server public_html..."
)

add_dependencies(ogg.server ogg.client)
