#include "key_filter.h"

void key_filter_init(key_filter_t *f, uint8_t max, int initial_level)
{
    f->max          = max;
    f->stable_level = initial_level ? 1 : 0;
    f->count        = f->stable_level ? max : 0;
}

bool key_filter_sample(key_filter_t *f, int sampled_level)
{
    if (sampled_level) {
        if (f->count < f->max) f->count++;
    } else {
        if (f->count > 0) f->count--;
    }

    /* Only the rails move the output; everything between leaves it latched. */
    if (f->count == f->max && f->stable_level == 0) {
        f->stable_level = 1;
        return true;
    }
    if (f->count == 0 && f->stable_level == 1) {
        f->stable_level = 0;
        return true;
    }
    return false;
}

int key_filter_level(const key_filter_t *f)
{
    return f->stable_level;
}
