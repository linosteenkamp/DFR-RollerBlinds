#ifndef BLIND_STORE_DATA_H
#define BLIND_STORE_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Everything blind_store persists, as loaded at boot. Kept free of ESP-IDF
 * headers so pure modules (ctl) can take it. */
typedef struct {
    bool    span_valid;
    int32_t closed_steps;
    bool    pos_known;
    int32_t cur_steps;
    bool    motor_reversed;
    bool    move_in_progress;   /* set at move start, cleared on clean end */
    uint16_t travel_secs;       /* full-travel time in seconds; 0 = never set */
} blind_store_data_t;

#endif /* BLIND_STORE_DATA_H */
