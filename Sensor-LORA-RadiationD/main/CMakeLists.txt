# main/CMakeLists.txt

# .c-Dateien in main/ und Unterordnern (aber nicht ulp/ — ULP-.S separat)
FILE(GLOB app_sources ${CMAKE_SOURCE_DIR}/main/*.c ${CMAKE_SOURCE_DIR}/main/config/*.c)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "config" "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES driver esp_adc esp_pm nvs_flash sx1262 button u8g2_hal_esp32 ulp
                       )

# ULP-FSM: pulse_cnt.S kompilieren und als Binär einbetten
set(ulp_app_name ulp_main)
set(ulp_s_sources "${CMAKE_CURRENT_LIST_DIR}/ulp/pulse_cnt.S")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")
