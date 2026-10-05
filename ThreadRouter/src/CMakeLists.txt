idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES espressif__esp_matter openthread)

# Suppress format/type mismatch warning from esp_log_color.h on xtensa gcc 14.x
# where uint32_t resolves to 'long unsigned int' instead of 'unsigned int'.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# Work around C6 toolchain issue in upstream ClosureControl cluster templates
# (espressif/esp-matter#1604, present in esp_matter 1.4.2 and 1.5.0 alike).
# This app does not use ClosureControl, so disabling it avoids the break.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHIP_CONFIG_CLOSURE_CONTROL_CLUSTER_SERVER=0)
