# bi_component(): one component definition for both builds.
# Under ESP-IDF it registers an IDF component; in the simulator it makes a static library.
#
# This is a macro, not a function: IDF's idf_component_register sets variables in,
# and return()s from, the component's CMakeLists. A function would swallow both.
#
#   REQUIRES      dependencies in both builds (component names = sim target names)
#   ESP_REQUIRES  IDF-only components (esp_timer, driver, ...)
#   LINUX_LIBS    simulator-only link libraries
include_guard(GLOBAL)

macro(bi_component)
  cmake_parse_arguments(_bi "" "NAME" "SRCS;INCLUDE_DIRS;REQUIRES;ESP_REQUIRES;LINUX_SRCS;ESP_SRCS;LINUX_LIBS" ${ARGN})
  if(ESP_PLATFORM)
    idf_component_register(SRCS ${_bi_SRCS} ${_bi_ESP_SRCS}
                           INCLUDE_DIRS ${_bi_INCLUDE_DIRS}
                           REQUIRES ${_bi_REQUIRES} ${_bi_ESP_REQUIRES})
  else()
    add_library(${_bi_NAME} STATIC ${_bi_SRCS} ${_bi_LINUX_SRCS})
    target_include_directories(${_bi_NAME} PUBLIC ${_bi_INCLUDE_DIRS})
    target_link_libraries(${_bi_NAME} PUBLIC ${_bi_REQUIRES} ${_bi_LINUX_LIBS})
  endif()
endmacro()
