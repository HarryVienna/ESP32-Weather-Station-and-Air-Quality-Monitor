
idf_component_register(
    SRCS "button.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)

# Optional: Add compile options for this component
# target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
