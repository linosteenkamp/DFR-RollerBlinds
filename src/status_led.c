/**
 * @file status_led.c
 * @brief 50 ms-tick pattern player driving the external status LED with the
 *        onboard LED mirroring it. Patterns are on/off bitmasks over a
 *        repeating frame of 60 ticks (3 s).
 */
#include "status_led.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <stdint.h>

#define TICK_MS   50
#define FRAME     60          /* 60 ticks * 50 ms = 3 s frame */

static int s_ext = -1, s_onb = -1;
static led_pattern_t s_base = LED_OFF;
static led_pattern_t s_trans = LED_OFF;   /* LED_OFF = no transient */
static int s_base_tick;    /* base and transient keep independent tick counters */
static int s_trans_tick;   /* so a base change can never corrupt a flash train */
static esp_timer_handle_t s_timer;

/* on/off per tick for each pattern; t is the tick inside the frame */
static bool pattern_level(led_pattern_t p, int t)
{
    switch (p) {
    case LED_CAL_MARK1: return (t / 10) % 2 == 0;          /* 1 Hz */
    case LED_CAL_MARK2: return (t / 2)  % 2 == 0;          /* fast, 5 Hz (spec: ~5 Hz) */
    case LED_UNCAL:     return t == 0 || t == 1 || t == 4 || t == 5; /* dbl flash / 3 s */
    case LED_IDENTIFY:  return t % 2 == 0;                 /* rapid 10 Hz */
    case LED_ACK:       return t < 12 && (t / 2) % 2 == 0; /* 3 flashes */
    case LED_ERROR:                                        /* gap, then 5 fast flashes:
                                                             * must NOT share MARK2's rate —
                                                             * a same-rate error is invisible
                                                             * against an already-blinking LED */
        return t >= 4 && t < 14 && (t % 2) == 0;

    case LED_OFF:
    default:            return false;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    bool lvl;
    if (s_trans != LED_OFF) {
        lvl = pattern_level(s_trans, s_trans_tick);
        s_trans_tick++;
        /* transient ends after its flash train (ACK 12, ERROR 14 ticks) */
        if ((s_trans == LED_ACK && s_trans_tick >= 12) ||
            (s_trans == LED_ERROR && s_trans_tick >= 14)) {
            s_trans = LED_OFF;
        }
    } else {
        lvl = pattern_level(s_base, s_base_tick);
        s_base_tick = (s_base_tick + 1) % FRAME;
    }
    gpio_set_level(s_ext, lvl);
    gpio_set_level(s_onb, lvl);
}

esp_err_t status_led_init(int gpio_ext, int gpio_onboard)
{
    s_ext = gpio_ext;
    s_onb = gpio_onboard;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << gpio_ext) | (1ULL << gpio_onboard),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    const esp_timer_create_args_t targs = {
        .callback = tick_cb, .name = "led_tick",
    };
    err = esp_timer_create(&targs, &s_timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(s_timer, TICK_MS * 1000);
}

void status_led_set(led_pattern_t base)
{
    if (base != s_base) { s_base = base; s_base_tick = 0; }
}

void status_led_flash(led_pattern_t transient)
{
    if (transient != LED_ACK && transient != LED_ERROR) {
        return;   /* only flash trains are transients; base patterns never overlay */
    }
    s_trans = transient;
    s_trans_tick = 0;
}
