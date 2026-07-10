# NOTE: switched from WiFi to Thread as the Matter transport (see
# sdkconfig.defaults). CONFIG_OPENTHREAD_ENABLED=y should pull in ESP-IDF's
# "openthread" component transitively via espressif__esp_matter, but this
# hasn't been build-verified (no ESP-IDF toolchain in the environment this
# was written in -- see BUILD.md). If the build can't find OpenThread
# headers, add "openthread" to REQUIRES below.
idf_component_register(SRCS "main.c" "tuya_client.c" "bme280.c" "matter_device.cpp"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES esp_http_client mbedtls esp_driver_i2c espressif__esp_matter)

# Suppress format/type mismatch warning from esp_log_color.h on xtensa gcc 14.x
# where uint32_t resolves to 'long unsigned int' instead of 'unsigned int'
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# Work around C6 toolchain issue in upstream ClosureControl cluster templates.
# This app does not use ClosureControl, so disabling it avoids a CHIP optional
# comparison compile break on riscv/gcc14.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHIP_CONFIG_CLOSURE_CONTROL_CLUSTER_SERVER=0)
