include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# Display side of the NMEA 2000 bus: decoded values into boatdata. Not part of the library (needs boatdata).
bi_component(NAME source_can
  SRCS src/source_can.c src/can_instances.c
  INCLUDE_DIRS include
  REQUIRES n2k electrical boatdata platform json)
