cmake_minimum_required(VERSION 3.16)
project(clap-trap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(CLAP_TRAP_BUILD_TESTS "Build tests" ON)
option(CLAP_TRAP_BUILD_EXAMPLES "Build examples" ON)
option(CLAP_TRAP_WASM_SUPPORT "Enable WASM plugin support via wclap-bridge" OFF)

# Optional: wclap-bridge for WASM plugin support
if(CLAP_TRAP_WASM_SUPPORT)
    find_package(wclap-bridge QUIET)
    if(NOT wclap-bridge_FOUND)
        # Try to find it as a subdirectory (CI uses ./wclap-bridge, local dev uses ../wclap-bridge)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wclap-bridge/CMakeLists.txt")
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/wclap-bridge" "${CMAKE_BINARY_DIR}/wclap-bridge")
            set(wclap-bridge_FOUND TRUE)
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../wclap-bridge/CMakeLists.txt")
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../wclap-bridge" "${CMAKE_BINARY_DIR}/wclap-bridge")
            set(wclap-bridge_FOUND TRUE)
        else()
            message(FATAL_ERROR "CLAP_TRAP_WASM_SUPPORT is ON but wclap-bridge not found. "
                "Either install wclap-bridge, or place it at ./wclap-bridge/ or ../wclap-bridge/")
        endif()
    endif()
endif()

# Fetch CLAP headers
include(FetchContent)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.2
)
FetchContent_MakeAvailable(clap)

# Main library
add_library(clap-trap
    src/plugin-loader.cpp
    src/test-host.cpp
    src/audio-buffers.cpp
    src/wav-file.cpp
)

target_include_directories(clap-trap PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(clap-trap PUBLIC clap)

# Optional WASM support via wclap-bridge
if(CLAP_TRAP_WASM_SUPPORT)
    target_link_libraries(clap-trap PRIVATE wclap-bridge)
    target_compile_definitions(clap-trap PUBLIC CLAP_TRAP_HAS_WASM=1)
endif()

# Platform-specific linking
if(APPLE)
    target_link_libraries(clap-trap PRIVATE "-framework CoreFoundation")
elseif(UNIX)
    target_link_libraries(clap-trap PRIVATE dl)
endif()

# CLI tool
if(CLAP_TRAP_BUILD_EXAMPLES)
    add_executable(clap-trap-bin examples/cli.cpp)
    set_target_properties(clap-trap-bin PROPERTIES OUTPUT_NAME clap-trap)
    target_link_libraries(clap-trap-bin PRIVATE clap-trap)
endif()

# Tests
if(CLAP_TRAP_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(clap-trap-tests
        tests/test_main.cpp
    )
    target_link_libraries(clap-trap-tests PRIVATE
        clap-trap
        Catch2::Catch2WithMain
    )

    enable_testing()
    add_test(NAME clap-trap-tests COMMAND clap-trap-tests)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS clap-trap
    EXPORT clap-trap-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/clap-trap
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT clap-trap-targets
    FILE clap-trap-config.cmake
    NAMESPACE clap-trap::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clap-trap
)
