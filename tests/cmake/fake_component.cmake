# Stands in for a component's CMakeLists.txt.
bi_component(NAME fake
  SRCS a.c
  INCLUDE_DIRS include
  REQUIRES lvgl
  ESP_REQUIRES esp_timer
  LINUX_LIBS pthread)
set(after_register_ran 1)   # IDF's early pass returns before this line
