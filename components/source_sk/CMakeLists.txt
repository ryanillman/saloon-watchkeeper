include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# The transport is per platform; tests link the core and supply a fake transport.
if(ESP_PLATFORM)
  bi_component(NAME source_sk
    SRCS src/sk_parse.c src/sk_client.c
    INCLUDE_DIRS include
    REQUIRES boatdata json platform config alerts
    ESP_SRCS src/transport_esp.c
    ESP_REQUIRES esp_http_client)
else()
  bi_component(NAME source_sk_core
    SRCS src/sk_parse.c src/sk_client.c
    INCLUDE_DIRS include
    REQUIRES boatdata json platform config alerts)
  find_package(CURL REQUIRED)
  add_library(source_sk STATIC src/sk_parse.c src/sk_client.c src/transport_curl.c)
  target_include_directories(source_sk PUBLIC include)
  target_link_libraries(source_sk PUBLIC boatdata json platform config alerts CURL::libcurl)
endif()
