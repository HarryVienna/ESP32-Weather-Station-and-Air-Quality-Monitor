FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources} 
                       INCLUDE_DIRS "."
                       REQUIRES bh1750 c4001 sen66 json nvs_flash esp-tls esp_wifi esp_netif esp_http_client esp_timer
)
