/**
 * @file pinwalk.c
 * @brief Throwaway bench tool: verify the XIAO ESP32C6 D-number -> GPIO
 *        mapping that src/main.c's pin #defines assume.
 *
 * Drives ONE of the seven signal GPIOs high at a time (all others held low)
 * and announces which, so a multimeter between the header pin and GND
 * identifies the physical pin empirically. The board's silkscreen only
 * prints D-numbers, so this is the only way to confirm the mapping without
 * trusting a published pinout table.
 *
 * Run this on a BARE XIAO with nothing wired to it.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "PINWALK";

#define HOLD_MS 5000    /* time to probe each pin */
#define GAP_MS  1000    /* all-low gap between pins */

/* Mirrors the GPIO map in src/main.c. If a row here turns out wrong, that
 * same row is what needs changing in main.c. */
static const struct {
    int         gpio;
    const char *dpin;
    const char *signal;
} PINS[] = {
    { 19, "D8", "STEP"      },
    { 17, "D7", "DIR"       },
    { 20, "D9", "EN"        },
    { 22, "D4", "Keypad Up" },
    { 23, "D5", "Keypad Dn" },
    { 16, "D6", "Keypad Fn" },
    {  2, "D2", "LED"       },
};

#define N_PINS (sizeof(PINS) / sizeof(PINS[0]))

void app_main(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < N_PINS; i++) {
        mask |= 1ULL << PINS[i].gpio;
    }

    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    for (size_t i = 0; i < N_PINS; i++) {
        gpio_set_level(PINS[i].gpio, 0);
    }

    ESP_LOGI(TAG, "XIAO ESP32C6 pin walker - expected mapping:");
    for (size_t i = 0; i < N_PINS; i++) {
        ESP_LOGI(TAG, "    %-3s = GPIO%-2d  (%s)",
                 PINS[i].dpin, PINS[i].gpio, PINS[i].signal);
    }
    ESP_LOGI(TAG, "Probe each header pin against GND with a multimeter.");
    ESP_LOGI(TAG, "Exactly one pin reads ~3.3V at a time; the rest are 0V.");
    ESP_LOGI(TAG, "If the HIGH pin is not the one named below, the mapping "
                  "is wrong and src/main.c needs that row corrected.");

    while (1) {
        for (size_t i = 0; i < N_PINS; i++) {
            ESP_LOGW(TAG, ">>> HIGH now: %s (GPIO%d) - the %s pin",
                     PINS[i].dpin, PINS[i].gpio, PINS[i].signal);
            gpio_set_level(PINS[i].gpio, 1);
            vTaskDelay(pdMS_TO_TICKS(HOLD_MS));
            gpio_set_level(PINS[i].gpio, 0);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
        }
        ESP_LOGI(TAG, "--- walk complete, repeating ---");
    }
}
