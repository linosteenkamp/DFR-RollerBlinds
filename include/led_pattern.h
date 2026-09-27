#ifndef LED_PATTERN_H
#define LED_PATTERN_H

/* Spec §2 LED patterns. Base patterns persist; transient patterns
 * (ACK/ERROR) play once and revert to the base. Kept free of ESP-IDF
 * headers so pure modules (ctl) can name a pattern. */
typedef enum {
    LED_OFF = 0,        /* normal: calibrated, idle */
    LED_CAL_MARK1,      /* 500 ms on / 1 s off: awaiting mark 1 (Open) */
    LED_CAL_MARK2,      /* fast ~5 Hz blink: awaiting mark 2 (Closed) */
    LED_UNCAL,          /* double-flash every 3 s: uncalibrated / pos unknown */
    LED_IDENTIFY,       /* steady rapid blink: Zigbee Identify */
    LED_NO_NETWORK,     /* 2 s on, 1 s off: not joined to a Zigbee network */
    LED_ACK,            /* transient: three quick flashes */
    LED_ERROR,          /* transient: five rapid flashes */
} led_pattern_t;

#endif /* LED_PATTERN_H */
