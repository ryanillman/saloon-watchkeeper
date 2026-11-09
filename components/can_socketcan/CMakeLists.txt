include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# Linux only: SocketCAN as an n2k_bus driver, for the simulator and tools. Empty under the ESP build.
if(ESP_PLATFORM)
  idf_component_register()
else()
  bi_component(NAME can_socketcan
    SRCS src/can_socketcan.c
    INCLUDE_DIRS include
    REQUIRES n2k_bus n2k)
endif()
