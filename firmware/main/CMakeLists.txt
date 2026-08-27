idf_component_register(
    SRCS "firmware.c"
    INCLUDE_DIRS "."
)

target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
    "HOMEEDGE_FIRMWARE_VERSION=\"${PROJECT_VER}\""
)