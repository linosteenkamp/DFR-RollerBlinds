#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

#include <stdint.h>

/* Kept free of ESP-IDF headers so pure modules (ctl) can build a profile. */
typedef struct {
    uint32_t cruise_us;   /* step interval at cruise (e.g. 300 = ~3.3 kHz) */
    uint32_t start_us;    /* first/last-step interval (e.g. 500 = 2 kHz) */
    int32_t  accel_steps; /* ramp length in steps (e.g. 800) */
} motion_profile_t;

#endif /* MOTION_PROFILE_H */
