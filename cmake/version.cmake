find_package(Git QUIET)
execute_process(COMMAND ${GIT_EXECUTABLE} log --pretty=format:'%h' -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)

# Check whether we got any revision (which isn't
# always the case, e.g., when someone downloaded a zip
# file from GitHub instead of a checkout)
if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "N/A")
    set(GIT_TAG "N/A")
    set(GIT_BRANCH "N/A")
    set(GIT_NAME "N/A")
else()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --exact-match --tags
        OUTPUT_VARIABLE GIT_TAG
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --dirty --always --tags
        OUTPUT_VARIABLE GIT_NAME
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    string(SUBSTRING "${GIT_REV}" 1 7 GIT_REV)
endif()

set(VERSION "#define GIT_REV \"${GIT_REV}\"
#define GIT_TAG \"${GIT_TAG}\"
#define GIT_BRANCH \"${GIT_BRANCH}\"
#define GIT_NAME \"${GIT_NAME}\"
"
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../include/version.h)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../include/version.h VERSION_)
else()
    set(VERSION_ "")
endif()

if (NOT "${VERSION}" STREQUAL "${VERSION_}")
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/../include/version.h "${VERSION}")
endif()
