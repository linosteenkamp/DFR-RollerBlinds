#include "trace_ring.h"

#include <stddef.h>

bool trace_ring_valid(const trace_ring_t *r, uint32_t magic)
{
    /* Bookkeeping is checked as well as the magic: uninitialised RTC memory
     * can collide with any single value, but is very unlikely to also carry
     * in-range head/count. */
    return r->magic == magic &&
           r->head  <  TRACE_DEPTH &&
           r->count <= TRACE_DEPTH;
}

void trace_ring_reset(trace_ring_t *r, uint32_t magic)
{
    r->magic = magic;
    r->head  = 0;
    r->count = 0;
    r->seq   = 0;
}

void trace_ring_push(trace_ring_t *r, uint32_t t_ms, uint16_t code,
                     int32_t a, int32_t b)
{
    trace_rec_t *rec = &r->rec[r->head];
    rec->t_ms = t_ms;
    rec->code = code;
    rec->seq  = r->seq++;
    rec->a    = a;
    rec->b    = b;

    r->head = (r->head + 1) % TRACE_DEPTH;
    if (r->count < TRACE_DEPTH) r->count++;
}

uint32_t trace_ring_count(const trace_ring_t *r)
{
    return r->count;
}

const trace_rec_t *trace_ring_at(const trace_ring_t *r, uint32_t i)
{
    if (i >= r->count) return NULL;
    /* oldest sits count slots behind head, modulo the ring */
    uint32_t idx = (r->head + TRACE_DEPTH - r->count + i) % TRACE_DEPTH;
    return &r->rec[idx];
}
