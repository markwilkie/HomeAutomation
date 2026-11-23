cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "src")

# version.txt is the firmware version; see tools/ESP_BUILDS.md.
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/idf_project_defaults.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spav2-spa-monitor)
