include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# Pure C: libc and n2k only. Hardware comes in through n2k_node_ops_t.
bi_component(NAME n2k_node
  SRCS src/n2k_node.c
  INCLUDE_DIRS include
  REQUIRES n2k)
