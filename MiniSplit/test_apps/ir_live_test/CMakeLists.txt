cmake_minimum_required(VERSION 3.16)

# version.txt is the firmware version; see tools/ESP_BUILDS.md.
include(${CMAKE_CURRENT_LIST_DIR}/../../../tools/cmake/idf_project_defaults.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ir_live_test)
