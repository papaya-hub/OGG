add_executable(ogg.stop-port main.cpp)

string(TOLOWER "${CMAKE_CXX_COMPILER}" _stop_port_cxx)
if(WIN32 OR _stop_port_cxx MATCHES "ucrt64|mingw64|mingw32|w64-mingw32")
    target_link_libraries(ogg.stop-port PRIVATE iphlpapi ws2_32)
endif()
