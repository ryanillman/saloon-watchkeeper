include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
# The transport is per platform; tests link the core and supply a fake transport.
if(ESP_PLATFORM)
  bi_component(NAME source_sk
    SRCS src/sk_parse.c src/sk_client.c src/sk_access.c src/sk_history.c src/sk_cmd.c src/sk_electrical.c
    INCLUDE_DIRS include
    REQUIRES boatdata electrical json platform config alerts history
    ESP_SRCS src/transport_esp.c
    ESP_REQUIRES esp_http_client)
else()
  bi_component(NAME source_sk_core
    SRCS src/sk_parse.c src/sk_client.c src/sk_access.c src/sk_history.c src/sk_cmd.c src/sk_electrical.c
    INCLUDE_DIRS include
    REQUIRES boatdata electrical json platform config alerts history)
  find_package(CURL REQUIRED)
  add_library(source_sk STATIC src/sk_parse.c src/sk_client.c src/sk_access.c src/sk_history.c src/sk_cmd.c src/sk_electrical.c src/transport_curl.c)
  target_include_directories(source_sk PUBLIC include)
  target_link_libraries(source_sk PUBLIC boatdata electrical json platform config alerts history CURL::libcurl)
endif()
