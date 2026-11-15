idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_mcpwm esp_driver_gpio esp_timer espressif__ds18b20 espressif__onewire_bus)
