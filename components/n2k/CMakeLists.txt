include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# Pure C: libc only, no ESP-IDF, no allocation. tools/check-layering.sh enforces it.
bi_component(NAME n2k
  SRCS src/n2k_id.c src/n2k_rx.c src/n2k_tx.c
  INCLUDE_DIRS include)
