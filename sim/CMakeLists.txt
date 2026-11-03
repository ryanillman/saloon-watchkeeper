cmake_minimum_required(VERSION 3.20)
project(boat_instruments_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

set(BI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
include(FetchContent)
include(CTest)

add_subdirectory(${BI_ROOT}/components/platform components/platform)
add_subdirectory(${BI_ROOT}/components/ui components/ui)

if(BUILD_TESTING)
  FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(unity)
  add_subdirectory(${BI_ROOT}/tests tests)
endif()
