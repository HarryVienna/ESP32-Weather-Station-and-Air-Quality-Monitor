# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources} 
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos button sx1262 nvs_flash esp_wifi esp_event esp_netif
                       )