idf_component_register(
    SRCS "sx1262.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_spi esp_driver_gpio
)

# Optional: Add compile options for this component
# target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
