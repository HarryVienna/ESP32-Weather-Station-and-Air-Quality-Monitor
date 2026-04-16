set(component_srcs "bme280.c"
                   "sensor_driver.c"
                   "bme280_sensor_driver.c"
)

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS ""
                       PRIV_REQUIRES "driver"
                       REQUIRES "")
