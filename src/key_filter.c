#include "key_filter.h"

void key_filter_init(key_filter_t *f, uint8_t max, int initial_level)
{
    f->max          = max;
    f->stable_level = initial_level ? 1 : 0;
    f->count        = f->stable_level ? max : 0;
    f->exc_depth    = 0;
    f->exc_samples  = 0;
    f->swallowed    = 0;
    f->traverse     = 0;
}

bool key_filter_sample(key_filter_t *f, int sampled_level)
{
    if (sampled_level) {
        if (f->count < f->max) f->count++;
    } else {
        if (f->count > 0) f->count--;
    }

    /* How far the counter has strayed from the rail the output is latched at.
     * Measuring against that rail rather than against a fixed end keeps the
     * bookkeeping symmetric without a second branch on stable_level below. */
    uint8_t dist = f->stable_level ? (uint8_t)(f->max - f->count) : f->count;

    if (dist > 0) {
        if (f->exc_samples < UINT16_MAX) f->exc_samples++;
        if (dist > f->exc_depth) f->exc_depth = dist;
    }

    /* Only the rails move the output; everything between leaves it latched. */
    if (dist == f->max) {
        f->stable_level = !f->stable_level;
        f->traverse     = f->exc_samples;
        f->exc_depth    = 0;
        f->exc_samples  = 0;
        return true;
    }

    /* Back where it started having gone somewhere: a press that never made it.
     * Recorded, not acted on — the output is deliberately unchanged. */
    if (dist == 0 && f->exc_depth > 0) {
        f->swallowed   = f->exc_depth;
        f->exc_depth   = 0;
        f->exc_samples = 0;
    }
    return false;
}

int key_filter_level(const key_filter_t *f)
{
    return f->stable_level;
}

uint16_t key_filter_traverse(const key_filter_t *f)
{
    return f->traverse;
}

uint8_t key_filter_take_swallowed(key_filter_t *f)
{
    uint8_t d = f->swallowed;
    f->swallowed = 0;
    return d;
}
