cmake_minimum_required(VERSION 3.16)

# Firmware version: version.txt plus the git commit (as MiniSplit/SpaV2 do).
# Bump version.txt on behavior changes.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" WS_VERSION LIMIT_COUNT 1)
execute_process(COMMAND git describe --always --dirty --abbrev=7 --match "no-tags-used"
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE WS_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(WS_GIT)
    set(PROJECT_VER "${WS_VERSION}+${WS_GIT}")
else()
    set(PROJECT_VER "${WS_VERSION}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weatherstation-thread)
