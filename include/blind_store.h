#ifndef BLIND_STORE_H
#define BLIND_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool    span_valid;
    int32_t closed_steps;
    bool    pos_known;
    int32_t cur_steps;
    bool    motor_reversed;
    bool    move_in_progress;   /* set at move start, cleared on clean end */
} blind_store_data_t;

/* Open the namespace and load everything; missing keys become safe defaults
 * (uncalibrated, not reversed, no move in progress). */
esp_err_t blind_store_init(blind_store_data_t *out);

esp_err_t blind_store_save_span(bool span_valid, int32_t closed_steps);
esp_err_t blind_store_save_position(bool pos_known, int32_t cur_steps);
esp_err_t blind_store_save_motor_reversed(bool reversed);
esp_err_t blind_store_set_move_flag(bool in_progress);

#endif /* BLIND_STORE_H */
