include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# Classifies and de-duplicates electrical data from any source. Portable: libc and boatdata only.
bi_component(NAME electrical
  SRCS src/electrical.c src/el_registry.c src/el_classify.c src/el_product.c src/el_merge.c src/el_publish.c src/el_label.c
  INCLUDE_DIRS include
  REQUIRES boatdata platform)
