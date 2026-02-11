cmake_minimum_required(VERSION 3.30)
project(supervisorcpp VERSION 0.1.0 LANGUAGES CXX)

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
find_package(Boost 1.84 REQUIRED COMPONENTS
    log
    log_setup
    program_options
    unit_test_framework
)

# Create static library for easier testing
add_library(supervisor_lib STATIC 
    supervisor-lib/config/config_parser.cpp
    supervisor-lib/logger/log_levels.cpp
    supervisor-lib/logger/log_writer.cpp
    supervisor-lib/logger/logger.cpp
    supervisor-lib/process/process.cpp
    supervisor-lib/process/process_manager.cpp
    supervisor-lib/rpc/rpc_connection.cpp
    supervisor-lib/rpc/rpc_server.cpp
    supervisor-lib/rpc/xmlrpc.cpp
    supervisor-lib/util/platform.cpp
    supervisor-lib/util/secure.cpp
    supervisor-lib/util/string.cpp
)
target_include_directories(supervisor_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/supervisor-lib
)
target_link_libraries(supervisor_lib PUBLIC
    Boost::headers
    Boost::log
    Boost::log_setup
    Boost::program_options
)

# Unified supervisor binary (busybox-style multi-call binary)
add_executable(supervisor
    supervisor-app/main.cpp
    supervisor-app/supervisord.cpp
    supervisor-app/supervisorctl.cpp
)
target_link_libraries(supervisor PRIVATE supervisor_lib)

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

# Copy test data to build directory (fixes ownership for root-owned validation)
file(COPY tests/data DESTINATION ${CMAKE_BINARY_DIR}/tests)

# Unit tests
add_executable(config_test
    tests/config/config_parser_test.cpp
)
target_link_libraries(config_test PRIVATE
    supervisor_lib
    Boost::unit_test_framework
)

# Parser robustness tests
add_executable(parser_robustness_test
    tests/config/parser_robustness_test.cpp
)
target_link_libraries(parser_robustness_test PRIVATE
    supervisor_lib
    Boost::unit_test_framework
)

# Integration tests
add_executable(integration_test
    tests/integration/integration_test.cpp
)
target_link_libraries(integration_test PRIVATE
    supervisor_lib
    Boost::unit_test_framework
)

# XML-RPC tests
add_executable(rpc_xml_test
    tests/rpc/xmlrpc_test.cpp
)
target_link_libraries(rpc_xml_test PRIVATE
    supervisor_lib
    Boost::unit_test_framework
)

# string util tests
add_executable(util_string_test
    tests/util/string_test.cpp
)
target_link_libraries(util_string_test PRIVATE
    supervisor_lib
    Boost::unit_test_framework
)


# Register tests with CTest
add_test(NAME ConfigParserTest COMMAND config_test)
add_test(NAME ParserRobustnessTest COMMAND parser_robustness_test)
add_test(NAME IntegrationTest COMMAND integration_test)
add_test(NAME RpcXmlTest COMMAND rpc_xml_test)
add_test(NAME UtilStringTest COMMAND util_string_test)

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
