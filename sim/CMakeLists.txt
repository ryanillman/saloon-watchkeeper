cmake_minimum_required(VERSION 3.20)
project(boat_instruments_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

set(BI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
include(FetchContent)
include(CTest)

# LVGL, configured by sim/lv_conf.h
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.3.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)

# cJSON, as a target named "json" to match the ESP-IDF component name
FetchContent_Declare(cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.18
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)   # fetch only; we build cJSON.c ourselves below
FetchContent_MakeAvailable(cjson)
add_library(json STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(json PUBLIC ${cjson_SOURCE_DIR})

add_subdirectory(${BI_ROOT}/components/platform components/platform)
add_subdirectory(${BI_ROOT}/components/ui components/ui)
add_subdirectory(${BI_ROOT}/components/boatdata components/boatdata)
add_subdirectory(${BI_ROOT}/components/config components/config)
add_subdirectory(${BI_ROOT}/components/units components/units)
add_subdirectory(${BI_ROOT}/components/source_sk components/source_sk)

add_library(sim_boards STATIC sim_boards.c)
target_include_directories(sim_boards PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sim_boards PUBLIC ui)

if(BUILD_TESTING)
  FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(unity)
  target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
  add_subdirectory(${BI_ROOT}/tests tests)
endif()

find_package(PNG REQUIRED)
add_executable(sim sim_main.c snapshot.c)
target_link_libraries(sim PRIVATE ui sim_boards platform lvgl PNG::PNG)

add_executable(sk_probe sk_probe.c)
target_link_libraries(sk_probe PRIVATE source_sk boatdata platform config)
