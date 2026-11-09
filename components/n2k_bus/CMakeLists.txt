include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# The loop core is pure C (libc, n2k, n2k_node); the CAN driver comes in through n2k_can_ops_t.
# The ESP task wrapper (src/n2k_bus_task.c) is added for the ESP build only.
bi_component(NAME n2k_bus
  SRCS src/n2k_bus.c
  INCLUDE_DIRS include
  REQUIRES n2k n2k_node
  ESP_SRCS src/n2k_bus_task.c
  ESP_REQUIRES freertos)
