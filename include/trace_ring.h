#ifndef TRACE_RING_H
#define TRACE_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TRACE_DEPTH
#define TRACE_DEPTH 256
#endif

/* One traced decision. 16 bytes; meaning of a/b depends on code. */
typedef struct {
    uint32_t t_ms;   /* uptime when recorded */
    uint16_t code;   /* trace_code_t, see trace.h */
    uint16_t seq;    /* monotonic; retained records are always contiguous, so
                      * a gap can never appear in the dump. A wrap is signalled
                      * instead by count == TRACE_DEPTH, or by a header whose
                      * first record's seq is not 0. */
    int32_t  a, b;
} trace_rec_t;

/* The array is inline rather than behind a pointer so the whole structure is
 * a single RTC_NOINIT object on target and a plain local in host tests. */
typedef struct {
    uint32_t    magic;
    uint32_t    head;   /* next write index */
    uint32_t    count;  /* entries held; saturates at TRACE_DEPTH */
    uint16_t    seq;    /* next sequence number */
    trace_rec_t rec[TRACE_DEPTH];
} trace_ring_t;

/* True only if the ring holds real data. RTC_NOINIT memory is genuinely
 * uninitialised at power-on — garbage, not zeros — so this must reject a
 * cold boot or the first dump prints noise that looks exactly like records. */
bool trace_ring_valid(const trace_ring_t *r, uint32_t magic);

void trace_ring_reset(trace_ring_t *r, uint32_t magic);

void trace_ring_push(trace_ring_t *r, uint32_t t_ms, uint16_t code,
                     int32_t a, int32_t b);

uint32_t trace_ring_count(const trace_ring_t *r);

/* i == 0 is the oldest retained record; NULL if i >= count. */
const trace_rec_t *trace_ring_at(const trace_ring_t *r, uint32_t i);

#endif /* TRACE_RING_H */
