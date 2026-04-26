# main/CMakeLists.txt

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "../common"
                       REQUIRES driver esp_wifi esp_adc nvs_flash bme280
                       )