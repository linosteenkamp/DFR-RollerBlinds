/**
 * @file rf_switch.c
 * @brief Drive the XIAO ESP32C6 RF switch: enable it, select the antenna.
 */
#include "rf_switch.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RF_SW";

esp_err_t rf_switch_init(int gpio_enable, int gpio_select, bool external_antenna)
{
    gpio_config_t io = { .pin_bit_mask = (1ULL << gpio_enable) | (1ULL << gpio_select),
                         .mode = GPIO_MODE_OUTPUT };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level(gpio_enable, 0);
    gpio_set_level(gpio_select, external_antenna ? 1 : 0);
    ESP_LOGW(TAG, "RF switch: enabled, %s antenna", external_antenna ? "U.FL" : "ceramic");
    return ESP_OK;
}
