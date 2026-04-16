# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "lora" "esp-now"
    REQUIRES driver freertos button sx1262 nvs_flash esp_wifi esp_netif u8g2
)
