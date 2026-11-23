# Included by every ESP-IDF project in this repo, before ESP-IDF's
# project.cmake, so a build only recompiles what actually changed.
# Why each line is here: tools/ESP_BUILDS.md.

# Firmware version = the project's version.txt and nothing else (bump it on a
# behavior change; editing it re-runs CMake). Left unset, ESP-IDF fills it in
# from `git describe` of the repo, which re-runs CMake after every commit
# anywhere in HomeAutomation -- and a CMake run costs ~800 Matter/OpenThread
# recompiles (esp-matter rewrites its generated headers on every run).
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/version.txt")

# esp_insights (pulled in by esp-matter) runs git_describe on its own folder
# "just to automatically trigger re-runs of CMake". managed_components/ sits
# inside this repo, so that tracked HomeAutomation's HEAD: every commit,
# pull or branch switch re-ran CMake. Git stops its search here instead.
set(ENV{GIT_CEILING_DIRECTORIES} "${CMAKE_SOURCE_DIR}/managed_components")
