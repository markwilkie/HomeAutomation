idf_component_register(SRCS "main.cpp" "matter_device.cpp" "probes.c" "sensors.c" "spa_leds.c"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES espressif__esp_matter openthread nvs_flash esp_timer esp_adc
                                esp_driver_gpio esp_driver_mcpwm esp_driver_usb_serial_jtag vfs
                                espressif__ds18b20 espressif__onewire_bus)

# gcc 14 format warnings from esp_log_color.h (as in MiniSplit/ThreadRouter).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# ClosureControl cluster templates break on riscv/gcc14 (espressif/esp-matter#1604).
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHIP_CONFIG_CLOSURE_CONTROL_CLUSTER_SERVER=0)
