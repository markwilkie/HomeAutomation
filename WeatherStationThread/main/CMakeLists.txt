idf_component_register(SRCS "main.c" "status_led.c"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES esp_driver_gpio esp_app_format esp_system esp_hw_support esp_timer)
