file(GLOB SOURCES *.c)
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "include"
                       REQUIRES driver freertos u8g2)
