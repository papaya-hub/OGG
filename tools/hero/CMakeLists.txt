add_executable(ogg.hero main.cpp)
target_include_directories(ogg.hero PRIVATE ${CMAKE_SOURCE_DIR}/libs/common)
target_link_libraries(ogg.hero PRIVATE winhttp gdiplus ole32)
set_target_properties(ogg.hero PROPERTIES
    OUTPUT_NAME "ogg.hero"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/hero"
)
