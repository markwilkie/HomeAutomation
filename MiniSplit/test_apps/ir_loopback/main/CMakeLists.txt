idf_component_register(SRCS "loopback_test.c" "ir_frame_decode.c"
                       INCLUDE_DIRS "."
                       REQUIRES ir_tcl112 esp_driver_rmt)
