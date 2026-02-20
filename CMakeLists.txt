cmake_minimum_required(VERSION 3.20)
project(NetGateLite VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

option(USE_NS3 "Link against local ns-3 installation" OFF)
option(USE_TARANTOOL "Enable Tarantool in-memory storage integration" OFF)

add_executable(netgate
    src/main.cpp
    src/ns3_analyzer.cpp
    src/tarantool_sink.cpp
)

if(USE_NS3)
    message(STATUS "Linking against local ns-3 workspace...")
    
    set(NS3_DIR "$ENV{HOME}/workspace/ns-3" CACHE PATH "Path to local ns-3 directory")
    
    if(NOT EXISTS "${NS3_DIR}/build/lib/libns3-dev-core-default.so")
        message(FATAL_ERROR "ns-3 compiled library not found in ${NS3_DIR}/build/lib ! Make sure you ran './ns3 build' in that directory.")
    endif()

    target_compile_definitions(netgate PRIVATE ENABLE_NS3)
    target_include_directories(netgate PRIVATE "${NS3_DIR}/build/include")
    
    # Let CMake find the .so explicitly to set rpath correctly
    find_library(NS3_CORE_LIB NAMES ns3-dev-core-default PATHS "${NS3_DIR}/build/lib" NO_DEFAULT_PATH)
    
    if(NS3_CORE_LIB)
        target_link_libraries(netgate PRIVATE ${NS3_CORE_LIB})
    else()
        message(FATAL_ERROR "Could not find ns3-dev-core-default.so")
    endif()
else()
    message(STATUS "ns-3 integration disabled. Pass -DUSE_NS3=ON to enable.")
endif()

if(USE_TARANTOOL)
    message(STATUS "Tarantool integration enabled.")
    target_compile_definitions(netgate PRIVATE ENABLE_TARANTOOL)
else()
    message(STATUS "Tarantool integration disabled. Pass -DUSE_TARANTOOL=ON to enable.")
endif()

target_include_directories(netgate PRIVATE src)
target_link_libraries(netgate PRIVATE Threads::Threads)
