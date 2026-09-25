set(SRCS
    "main.c"
    "ctl.c"
    "position.c"
    "ramp.c"
    "keypad_logic.c"
    "key_filter.c"
    "keypad.c"
    "trace_ring.c"
    "trace.c"
    "blind_store.c"
    "motion.c"
    "status_led.c"
    "covering.c"
)

idf_component_register(
    SRCS ${SRCS}
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
