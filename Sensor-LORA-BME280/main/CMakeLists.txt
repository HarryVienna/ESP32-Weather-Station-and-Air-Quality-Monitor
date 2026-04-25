# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "config"
                       REQUIRES driver esp_adc nvs_flash sensor_driver button sx1262 u8g2_hal_esp32
                       )