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
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

# Source files for libsupervisord (shared library for testing)
set(SUPERVISORD_LIB_SOURCES
    src/config/config_parser.cpp
    src/util/logger.cpp
    src/process/process.cpp
    src/process/process_manager.cpp
    src/process/log_writer.cpp
    src/rpc/rpc_server.cpp
)

# Create static library for easier testing
add_library(supervisord_lib STATIC ${SUPERVISORD_LIB_SOURCES})
target_link_libraries(supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# Main supervisord executable
add_executable(supervisord
    src/main.cpp
)
target_link_libraries(supervisord
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
)

# supervisorctl executable (placeholder for later)
add_executable(supervisorctl
    src/ctl/supervisorctl.cpp
)
target_link_libraries(supervisorctl
    supervisord_lib
    ${Boost_LIBRARIES}
    pthread
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
install(TARGETS supervisord supervisorctl
    RUNTIME DESTINATION bin
)

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
