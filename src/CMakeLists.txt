# FW_APP selects the firmware that this component builds. PlatformIO passes
# it from board_build.cmake_extra_args (-DFW_APP=...). The default is the
# blind controller.
if(NOT DEFINED FW_APP)
    set(FW_APP "blind")
endif()

# Modules that more than one firmware uses.
set(COMMON_SRCS
    "motion.c"
    "ramp.c"
    "key_filter.c"
    "rf_switch.c"
)

if(FW_APP STREQUAL "blind")
    set(APP_SRCS
        "main.c"
        "ctl.c"
        "position.c"
        "keypad_logic.c"
        "keypad.c"
        "trace_ring.c"
        "trace.c"
        "blind_store.c"
        "status_led.c"
        "covering.c"
    )
else()
    message(FATAL_ERROR "Unknown FW_APP '${FW_APP}' (expected: blind)")
endif()

idf_component_register(
    SRCS ${COMMON_SRCS} ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        nvs_flash
        esp_event
        esp_timer
        esp_driver_gpio
        esp_driver_gptimer
)
