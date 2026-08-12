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
 * The active-low mapping stays with the caller.
 *
 * The excursion fields below are TELEMETRY ONLY — they never influence the
 * output. They exist because the two ways this filter can lose a real press
 * are both invisible downstream: a press too short to cross the rails emits
 * nothing at all, and a ragged edge delays a flip without leaving any record
 * that it was late. See key_filter_traverse() / key_filter_take_swallowed(). */
typedef struct {
    uint8_t count;         /* integrator, 0..max */
    uint8_t max;           /* samples needed to traverse rail to rail */
    int     stable_level;  /* debounced output */

    uint8_t  exc_depth;    /* furthest from the latched rail, this excursion */
    uint16_t exc_samples;  /* samples since the counter left the rail */
    uint8_t  swallowed;    /* depth of the last abandoned excursion, 0 = none */
    uint16_t traverse;     /* samples the last completed flip took */
} key_filter_t;

/* Seed at the rail matching initial_level, so a filter starting on a settled
 * line needs a full traverse before it can report anything. */
void key_filter_init(key_filter_t *f, uint8_t max, int initial_level);

/* Feed one raw sample. Returns true only when the debounced output flips. */
bool key_filter_sample(key_filter_t *f, int sampled_level);

int key_filter_level(const key_filter_t *f);

/* Samples the most recent flip took, rail to rail. Equals max on a cleanly
 * settled edge and exceeds it on a rattling one — and since the classifier
 * measures duration between flips, an edge slower than its partner shifts the
 * measured press length by the difference. That is the whole reason this is
 * reported rather than assumed symmetric. Valid after key_filter_sample()
 * returns true. */
uint16_t key_filter_traverse(const key_filter_t *f);

/* Depth of an excursion that turned back before reaching the far rail, i.e. a
 * press the filter swallowed: 0 when there was none. Clears on read, so the
 * caller sees each abandoned excursion once. Only the most recent is held —
 * poll it on every sample, as keypad.c does, and none can be missed.
 *
 * Depth is what separates the two cases that look identical downstream: a
 * shallow excursion is line noise being correctly rejected, while one that
 * came close to max is a genuine press that fell just short. */
uint8_t key_filter_take_swallowed(key_filter_t *f);

#endif /* KEY_FILTER_H */
