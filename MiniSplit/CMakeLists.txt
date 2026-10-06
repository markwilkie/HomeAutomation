cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "src" "components")

# Firmware version: version.txt (bump by hand on a behavior change) plus the
# git commit it was built from, "-dirty" if built with uncommitted changes.
# Matter reports it as the Basic Information SoftwareVersionString (esp_app_desc),
# which HA shows as the device's "Firmware"; also logged at boot. Max 31 chars.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" MINISPLIT_VERSION LIMIT_COUNT 1)
execute_process(COMMAND git describe --always --dirty --abbrev=7 --match "no-tags-used"
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE MINISPLIT_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(MINISPLIT_GIT)
    set(PROJECT_VER "${MINISPLIT_VERSION}+${MINISPLIT_GIT}")
else()
    set(PROJECT_VER "${MINISPLIT_VERSION}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minisplit-matter-bridge)
