add_subdirectory(versionbump)
add_subdirectory(stop-port)

if(OGG_PLATFORM_WINDOWS)
    add_subdirectory(icongen)
    add_subdirectory(hero)
    add_subdirectory(sync-settings)
    add_subdirectory(test-ai-image)

    set(OGG_LAUNCHER_DIR ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Launcher)
    set(OGG_LAUNCHER_SVG ${OGG_LAUNCHER_DIR}/icon.svg)
    set(OGG_LAUNCHER_ICON ${OGG_LAUNCHER_DIR}/icon.ico)

    add_custom_command(
        OUTPUT ${OGG_LAUNCHER_ICON}
        COMMAND $<TARGET_FILE:ogg.icongen> ${OGG_LAUNCHER_SVG} ${OGG_LAUNCHER_ICON}
        DEPENDS ogg.icongen ${OGG_LAUNCHER_SVG}
        COMMENT "Converting launcher icon.svg to icon.ico..."
    )
    add_custom_target(ogg_launcher_icon DEPENDS ${OGG_LAUNCHER_ICON})

    add_executable(ogg.launcher
        ${CMAKE_SOURCE_DIR}/src/OffGridGamer.OffGridGames.Launcher/main.cpp
        ${OGG_LAUNCHER_DIR}/launcher_ui_cli.cpp
        ${OGG_LAUNCHER_DIR}/launcher_ui_gui.cpp
        ${CMAKE_SOURCE_DIR}/libs/common/http_client.cpp
        ${CMAKE_SOURCE_DIR}/libs/common/http_websocket.cpp
        ${CMAKE_SOURCE_DIR}/libs/common/server_monitor.cpp
        ${CMAKE_SOURCE_DIR}/libs/common/ui/hero_image.cpp
        ${OGG_UI_SOURCES}
        ${OGG_LAUNCHER_DIR}/icon.rc
    )
    add_dependencies(ogg.launcher ogg_launcher_icon)
    set_target_properties(ogg.launcher PROPERTIES
        OUTPUT_NAME "ogg.launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        INTERPROCEDURAL_OPTIMIZATION FALSE
    )
    set_source_files_properties(${OGG_LAUNCHER_DIR}/icon.rc PROPERTIES
        COMPILE_FLAGS "-I${OGG_LAUNCHER_DIR}")
    target_include_directories(ogg.launcher PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/common
        ${OGG_LAUNCHER_DIR}
        ${OGG_GENERATED_INCLUDE_DIR}
    )
    target_link_libraries(ogg.launcher PRIVATE winhttp shlwapi d2d1 dwrite dwmapi ole32 gdi32 gdiplus comctl32 ws2_32)
    add_dependencies(ogg.launcher ogg.sync-settings)
    add_custom_command(TARGET ogg.launcher PRE_BUILD
        COMMAND $<TARGET_FILE:ogg.sync-settings>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Syncing AppData settings into build/generated..."
    )
endif()
