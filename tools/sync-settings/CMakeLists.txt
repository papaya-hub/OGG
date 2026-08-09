add_executable(ogg.sync-settings
    main.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/login_image_assets.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/http_client.cpp
)
target_include_directories(ogg.sync-settings PRIVATE ${CMAKE_SOURCE_DIR}/libs/common)
target_link_libraries(ogg.sync-settings PRIVATE winhttp ole32)
set_target_properties(ogg.sync-settings PROPERTIES
    OUTPUT_NAME "ogg.sync-settings"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/sync-settings"
)
