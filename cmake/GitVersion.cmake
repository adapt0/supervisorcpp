# Extract current git revision
execute_process(
    COMMAND git -c safe.directory=${SOURCE_DIR} rev-parse --short HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    # ERROR_QUIET
)
if(NOT GIT_REV)
    set(GIT_REV "unknown")
endif()

# Only regenerate if the revision or version changed (avoids unnecessary rebuilds)
message("${PROJECT_VERSION}[${GIT_REV}]")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" OLD_CONTENT)
    string(FIND "${OLD_CONTENT}" "${GIT_REV}" REV_FOUND)
    string(FIND "${OLD_CONTENT}" "${PROJECT_VERSION}" VER_FOUND)
    if(NOT REV_FOUND EQUAL -1 AND NOT VER_FOUND EQUAL -1)
        return()
    endif()
endif()

configure_file("${INPUT_FILE}" "${OUTPUT_FILE}" @ONLY)
