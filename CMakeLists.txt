# ESP-IDF project. Build one board at a time:
#   idf.py -D BOARD=square4 -B build/square4 build
#   idf.py -D BOARD=square4 -D BI_EXTRA=diag/probe.defaults -B build/square4-probe build
cmake_minimum_required(VERSION 3.16)

if(NOT BOARD)
  message(FATAL_ERROR "Set BOARD: idf.py -D BOARD=square4|lcd7|lcd7b -B build/<board> build")
endif()
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/sdkconfig.defaults)
  message(FATAL_ERROR "Unknown BOARD '${BOARD}'")
endif()

# BI_EXTRA: optional extra defaults files, e.g. -D BI_EXTRA=diag/probe.defaults
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/sdkconfig.defaults")
foreach(extra ${BI_EXTRA})
  list(APPEND SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/${extra})
endforeach()
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(saloon_watchkeeper)
