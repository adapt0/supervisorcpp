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
    supervisor-app/args_parser.cpp
    supervisor-app/main.cpp
    supervisor-app/supervisord.cpp
    supervisor-app/supervisorctl.cpp
)
target_link_libraries(supervisor PRIVATE supervisor_lib)
target_include_directories(supervisor PRIVATE ${CMAKE_BINARY_DIR}/generated)
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
file(GLOB_RECURSE TEST_DATA_FILES "${CMAKE_SOURCE_DIR}/tests/data/*")
add_custom_target(copy_test_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/tests/data
        ${CMAKE_BINARY_DIR}/tests/data
    DEPENDS ${TEST_DATA_FILES}
    COMMENT "Copying test data to build directory"
)

# Helper: define a test executable from a single source file
function(add_unit_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE supervisor_lib Boost::unit_test_framework)
    add_dependencies(${TARGET_NAME} copy_test_data)
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

add_unit_test(config_test              tests/config/config_parser_test.cpp)
add_unit_test(parser_robustness_test   tests/config/parser_robustness_test.cpp)
add_unit_test(integration_test         tests/integration/integration_test.cpp)
add_unit_test(rpc_xml_test             tests/rpc/xmlrpc_test.cpp)
add_unit_test(util_string_test         tests/util/string_test.cpp)
add_unit_test(util_secure_test         tests/util/secure_test.cpp)
add_unit_test(log_writer_test          tests/logger/log_writer_test.cpp)
add_unit_test(rpc_dispatch_test        tests/rpc/rpc_dispatch_test.cpp)
add_unit_test(rpc_connection_test      tests/rpc/rpc_connection_test.cpp)

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
