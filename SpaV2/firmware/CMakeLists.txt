cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "src")

# Firmware version: version.txt plus the git commit (as MiniSplit does). Matter
# reports it as SoftwareVersionString, shown in HA as the device's "Firmware".
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" SPAV2_VERSION LIMIT_COUNT 1)
execute_process(COMMAND git describe --always --dirty --abbrev=7 --match "no-tags-used"
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE SPAV2_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(SPAV2_GIT)
    set(PROJECT_VER "${SPAV2_VERSION}+${SPAV2_GIT}")
else()
    set(PROJECT_VER "${SPAV2_VERSION}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spav2-spa-monitor)
