#ifndef RF_SWITCH_H
#define RF_SWITCH_H

#include <stdbool.h>
#include "esp_err.h"

/* The RF switch of the Seeed XIAO ESP32C6 selects the antenna. The firmware
 * must drive it before the radio starts, or the radio gets a weak signal.
 * The blind and the rig use it (refer to HARDWARE.md, "Fitting the U.FL
 * antenna").
 *
 * gpio_enable: low = switch enabled (GPIO3 on the XIAO ESP32C6).
 * gpio_select: low = ceramic antenna, high = U.FL (GPIO14). */
esp_err_t rf_switch_init(int gpio_enable, int gpio_select, bool external_antenna);

#endif /* RF_SWITCH_H */
