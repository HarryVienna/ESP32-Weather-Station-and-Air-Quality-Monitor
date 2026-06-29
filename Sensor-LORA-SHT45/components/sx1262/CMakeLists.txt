file(GLOB SOURCES *.c)
idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES driver
)

# Optional: Add compile options for this component
# target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
