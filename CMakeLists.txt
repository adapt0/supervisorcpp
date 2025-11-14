cmake_minimum_required(VERSION 3.20)
project(supervisorcpp VERSION 0.1.0 LANGUAGES CXX)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Find Boost
find_package(Boost 1.75 REQUIRED COMPONENTS
    log
    log_setup
    filesystem
    system
    thread
    program_options
    unit_test_framework
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/supervisor-lib
    ${CMAKE_SOURCE_DIR}/supervisor-app
    ${Boost_INCLUDE_DIRS}
)

# Source files for libsupervisord (library code only)
set(SUPERVISORD_LIB_SOURCES
    supervisor-lib/config/config_parser.cpp
    supervisor-lib/util/logger.cpp
    supervisor-lib/process/process.cpp
    supervisor-lib/process/process_manager.cpp
    supervisor-lib/process/log_writer.cpp
    supervisor-lib/rpc/rpc_server.cpp
)

# Create static library for easier testing
add_library(supervisord_lib STATIC ${SUPERVISORD_LIB_SOURCES})
target_link_libraries(supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# Unified supervisor binary (busybox-style multi-call binary)
add_executable(supervisor
    supervisor-app/main.cpp
    supervisor-app/supervisord.cpp
    supervisor-app/supervisorctl.cpp
)
target_link_libraries(supervisor
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

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

# Unit tests
add_executable(config_test
    tests/config/config_parser_test.cpp
)
target_link_libraries(config_test
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# Parser robustness tests
add_executable(parser_robustness_test
    tests/config/parser_robustness_test.cpp
)
target_link_libraries(parser_robustness_test
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# XML-RPC parser tests
add_executable(xmlrpc_parser_test
    tests/rpc/xmlrpc_parser_test.cpp
)
target_link_libraries(xmlrpc_parser_test
    ${Boost_LIBRARIES}
    pthread
)

# Integration tests
add_executable(integration_test
    tests/integration/integration_test.cpp
)
target_link_libraries(integration_test
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# Register tests with CTest
add_test(NAME ConfigParserTest COMMAND config_test)
add_test(NAME ParserRobustnessTest COMMAND parser_robustness_test)
add_test(NAME XmlRpcParserTest COMMAND xmlrpc_parser_test)
add_test(NAME IntegrationTest COMMAND integration_test)

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
message(STATUS "  Boost Libraries:    ${Boost_LIBRARIES}")
message(STATUS "")
