add_executable(ogg.test-ai-image
    main.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/ai_image.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/login_image_assets.cpp
    ${CMAKE_SOURCE_DIR}/libs/common/http_client.cpp
)
set_target_properties(ogg.test-ai-image PROPERTIES
    OUTPUT_NAME "ogg.test-ai-image"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/test-ai-image"
)
target_include_directories(ogg.test-ai-image PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/common
    ${OGG_GENERATED_INCLUDE_DIR}
)
target_link_libraries(ogg.test-ai-image PRIVATE winhttp gdiplus ole32)
