idf_component_register(SRCS "live_test.c"
                       INCLUDE_DIRS "."
                       REQUIRES ir_tcl112 esp_driver_rmt esp_driver_usb_serial_jtag)
