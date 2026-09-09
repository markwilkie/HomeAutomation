# Wraps the real MiniSplit IR TX driver as an isolated component for this
# standalone live-unit test app. Referenced by relative path -- not copied --
# so this always exercises the actual shipped driver. Deliberately skips
# ../../../../src/CMakeLists.txt's own component registration: that one
# pulls in espressif__esp_matter/openthread/lwip/etc. for the full Matter
# firmware, none of which this RMT-only test needs.
idf_component_register(SRCS "../../../../src/ir_tcl112.c"
                       INCLUDE_DIRS "../../../../include"
                       REQUIRES esp_driver_rmt)
