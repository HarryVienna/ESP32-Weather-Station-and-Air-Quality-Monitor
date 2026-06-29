# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "config"
                       REQUIRES driver esp_adc nvs_flash sht45 sx1262 button u8g2_hal_esp32
                       )