# bi_component(): one component definition for both builds.
# Under ESP-IDF it registers an IDF component; in the simulator it makes a static library.
include_guard(GLOBAL)

function(bi_component)
  cmake_parse_arguments(C "" "NAME" "SRCS;INCLUDE_DIRS;REQUIRES;LINUX_SRCS;ESP_SRCS;LINUX_LIBS" ${ARGN})
  if(ESP_PLATFORM)
    idf_component_register(SRCS ${C_SRCS} ${C_ESP_SRCS}
                           INCLUDE_DIRS ${C_INCLUDE_DIRS}
                           REQUIRES ${C_REQUIRES})
  else()
    add_library(${C_NAME} STATIC ${C_SRCS} ${C_LINUX_SRCS})
    target_include_directories(${C_NAME} PUBLIC ${C_INCLUDE_DIRS})
    target_link_libraries(${C_NAME} PUBLIC ${C_REQUIRES} ${C_LINUX_LIBS})
  endif()
endfunction()
