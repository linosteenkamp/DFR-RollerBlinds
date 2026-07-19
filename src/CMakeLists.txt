set(SRCS
    "main.c"
    "position.c"
    "ramp.c"
    "keypad_logic.c"
    "blind_store.c"
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
