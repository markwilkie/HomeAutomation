idf_component_register(SRCS "main.c" "tuya_client.c" "matter_device.cpp"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES esp_http_client mbedtls espressif__esp_matter)

# Suppress format/type mismatch warning from esp_log_color.h on xtensa gcc 14.x
# where uint32_t resolves to 'long unsigned int' instead of 'unsigned int'
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# Work around C6 toolchain issue in upstream ClosureControl cluster templates.
# This app does not use ClosureControl, so disabling it avoids a CHIP optional
# comparison compile break on riscv/gcc14.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHIP_CONFIG_CLOSURE_CONTROL_CLUSTER_SERVER=0)
