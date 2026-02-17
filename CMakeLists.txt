# SPDX-License-Identifier: MIT
# Copyright (c) 2025-2026 Chris Byrne

cmake_minimum_required(VERSION 3.28)
project(supervisorcpp VERSION 26.02.0 LANGUAGES CXX)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

STRING(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE_LOWER MATCHES debug)
    # debug build
else ()
    # static link libs to reduce target burden
    set(Boost_USE_STATIC_LIBS ON)
endif()

# Find Boost
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost 1.80 REQUIRED COMPONENTS
    log
    log_setup
    program_options
    unit_test_framework
)

# Create static library for easier testing
add_library(supervisor_lib STATIC
    supervisor-lib/src/config/config_parser.cpp
    supervisor-lib/src/logger/log_levels.cpp
    supervisor-lib/src/logger/log_writer.cpp
    supervisor-lib/src/logger/logger.cpp
    supervisor-lib/src/process/process.cpp
    supervisor-lib/src/process/process_manager.cpp
    supervisor-lib/src/rpc/rpc_connection.cpp
    supervisor-lib/src/rpc/rpc_server.cpp
    supervisor-lib/src/rpc/xmlrpc.cpp
    supervisor-lib/src/util/platform.cpp
    supervisor-lib/src/util/secure.cpp
    supervisor-lib/src/util/string.cpp
)
target_include_directories(supervisor_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/supervisor-lib/src
)
target_link_libraries(supervisor_lib PUBLIC
    Boost::headers
    Boost::log
    Boost::log_setup
    Boost::program_options
)

# Generate version.h at build time with current git revision
add_custom_target(git_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DINPUT_FILE=${CMAKE_SOURCE_DIR}/cmake/version.h.in
        -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/generated/version.h
        -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
    COMMENT "Checking git revision"
)

# Unified supervisor binary (busybox-style multi-call binary)
add_executable(supervisor
    supervisor-app/src/args_parser.cpp
    supervisor-app/src/main.cpp
    supervisor-app/src/daemon/supervisord.cpp
    supervisor-app/src/ctl/supervisorctl.cpp
)
target_link_libraries(supervisor PRIVATE supervisor_lib)
target_include_directories(supervisor PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/supervisor-app/src
)
add_dependencies(supervisor git_version)

# Create symlinks in build directory for testing/development
add_custom_command(TARGET supervisor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        supervisor supervisord
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        supervisor supervisorctl
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Creating symlinks for busybox-style invocation"
)

# Enable testing
enable_testing()

# Copy test data to build directory at build time (not configure time)
# This ensures changes to test data are reflected without re-running cmake
file(GLOB_RECURSE TEST_DATA_FILES "${CMAKE_SOURCE_DIR}/supervisor-lib/tests/data/*")
add_custom_target(copy_test_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/supervisor-lib/tests/data
        ${CMAKE_BINARY_DIR}/tests/data
    DEPENDS ${TEST_DATA_FILES}
    COMMENT "Copying test data to build directory"
)

# Helper: define a lib test executable from a single source file
function(add_lib_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE supervisor_lib Boost::unit_test_framework)
    add_dependencies(${TARGET_NAME} copy_test_data)
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

# Helper: define an app test executable (links lib + has app include path)
function(add_app_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE supervisor_lib Boost::unit_test_framework)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/supervisor-app/src
        ${CMAKE_BINARY_DIR}/generated
    )
    add_dependencies(${TARGET_NAME} copy_test_data)
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

# Library tests
add_lib_test(config_test              supervisor-lib/tests/config/config_parser_test.cpp)
add_lib_test(parser_robustness_test   supervisor-lib/tests/config/parser_robustness_test.cpp)
add_lib_test(integration_test         supervisor-lib/tests/integration/integration_test.cpp)
add_lib_test(rpc_xml_test             supervisor-lib/tests/rpc/xmlrpc_test.cpp)
add_lib_test(util_string_test         supervisor-lib/tests/util/string_test.cpp)
add_lib_test(util_secure_test         supervisor-lib/tests/util/secure_test.cpp)
add_lib_test(log_writer_test          supervisor-lib/tests/logger/log_writer_test.cpp)
add_lib_test(rpc_dispatch_test        supervisor-lib/tests/rpc/rpc_dispatch_test.cpp)
add_lib_test(rpc_connection_test      supervisor-lib/tests/rpc/rpc_connection_test.cpp)

# App tests
add_app_test(ctl_handler_test         supervisor-app/tests/ctl_handler_test.cpp)

# Install targets
install(TARGETS supervisor
    RUNTIME DESTINATION bin
)

# Create symlinks for busybox-style invocation
install(CODE "
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        supervisor \${CMAKE_INSTALL_PREFIX}/bin/supervisord)
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        supervisor \${CMAKE_INSTALL_PREFIX}/bin/supervisorctl)
")

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  CMake Version:      ${CMAKE_VERSION}")
message(STATUS "  C++ Compiler:       ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ Standard:       C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "  Boost Version:      ${Boost_VERSION}")
message(STATUS "")
