file(GLOB SOURCES *.c)
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_gpio esp_driver_spi esp_driver_i2c freertos u8g2)
