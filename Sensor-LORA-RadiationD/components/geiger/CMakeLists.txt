idf_component_register(SRCS "geiger.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver freertos esp_timer log)