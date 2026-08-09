# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "network" "display" "i2c" "ota"
    REQUIRES driver esp_driver_gpio esp_driver_ledc esp_driver_i2c freertos button sx1262 nvs_flash esp_wifi esp_netif u8g2_hal_esp32
             esp-tls esp_http_client esp_https_ota app_update bootloader_support json

)
