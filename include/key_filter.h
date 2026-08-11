#ifndef KEY_FILTER_H
#define KEY_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/* Integrator debounce for a mechanical key on an electrically noisy line.
 *
 * Unlike a plain change detector, the output flips only when the raw level
 * has travelled the FULL distance between the rails: the counter climbs
 * while the sample reads high and falls while it reads low, and the output
 * changes only at count == 0 and count == max. A line that rattles moves the
 * counter back and forth without ever crossing, so noise is rejected on
 * energy rather than on where the samples happen to land.
 *
 * Polarity-free: feed the raw GPIO level, read a debounced raw level back.
 * The active-low mapping stays with the caller. */
typedef struct {
    uint8_t count;         /* integrator, 0..max */
    uint8_t max;           /* samples needed to traverse rail to rail */
    int     stable_level;  /* debounced output */
} key_filter_t;

/* Seed at the rail matching initial_level, so a filter starting on a settled
 * line needs a full traverse before it can report anything. */
void key_filter_init(key_filter_t *f, uint8_t max, int initial_level);

/* Feed one raw sample. Returns true only when the debounced output flips. */
bool key_filter_sample(key_filter_t *f, int sampled_level);

int key_filter_level(const key_filter_t *f);

#endif /* KEY_FILTER_H */
