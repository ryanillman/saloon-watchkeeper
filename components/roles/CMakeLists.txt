include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME roles
  SRCS src/role_bind.c src/role_engine.c src/page_engine.c src/engine_state.c
  INCLUDE_DIRS include
  REQUIRES boatdata config units ui lvgl)
