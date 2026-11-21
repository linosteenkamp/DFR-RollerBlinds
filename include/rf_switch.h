#ifndef RF_SWITCH_H
#define RF_SWITCH_H

#include "esp_err.h"

/* The RF switch of the Seeed XIAO ESP32C6 selects the antenna. The firmware
 * must drive it before the radio starts, or the radio gets a weak signal.
 * The blind and the test rig use it (refer to HARDWARE.md, "Fitting the U.FL
 * antenna").
 *
 * gpio_enable: low = switch enabled (GPIO3 on the XIAO ESP32C6).
 * gpio_select: low = ceramic antenna, high = U.FL (GPIO14). */
typedef enum {
    RF_ANT_CERAMIC = 0,  /* the antenna on the board */
    RF_ANT_UFL     = 1,  /* an external antenna on the U.FL connector */
} rf_antenna_t;

esp_err_t rf_switch_init(int gpio_enable, int gpio_select, rf_antenna_t antenna);

#endif /* RF_SWITCH_H */
