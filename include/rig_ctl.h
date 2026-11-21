#ifndef RIG_CTL_H
#define RIG_CTL_H

/* The dispatcher core of the test rig. All the decisions of the rig occur
 * here, one rig_event_t at a time. Pure C with no ESP-IDF headers, so that
 * it runs in the host tests. All side effects go through rig_ports_t.
 * rig_main.c connects the ports to the real modules. The tests connect
 * them to a fake that records the calls.
 *
 * Concurrency: only the dispatcher task calls rig_handle(). rig_ctl calls
 * only its ports and pure modules. */

#include <stdbool.h>
#include <stdint.h>
#include "motion_profile.h"
#include "rig_event.h"
#include "rig_record.h"

typedef int rig_err_t;   /* 0 = OK; carries esp_err_t values unchanged */
#define RIG_OK 0

typedef struct {
    /* motion */
    rig_err_t (*motion_start)(int32_t from_steps, int32_t to_steps,
                              const motion_profile_t *prof, int32_t hard_cap);
    void      (*motion_stop)(void);
    int32_t   (*motion_steps)(void);   /* the live position */
    /* publish: the adapter must not block. It returns false when it
     * dropped the record. rig_ctl does not change a decision for that. */
    bool (*publish)(const rig_record_t *rec, uint8_t qos, bool retain);
} rig_ports_t;

typedef struct {
    uint32_t start_us;         /* the interval of the first and last step */
    int32_t  accel_steps;      /* the ramp length, as on the blind */
    uint32_t min_interval_us;  /* the fastest interval that `move` accepts */
    uint32_t max_interval_us;  /* the slowest interval that `move` accepts */
    int32_t  hard_cap;         /* the longest move that `move` accepts */
} rig_config_t;

typedef struct {
    const rig_ports_t *io;
    rig_config_t cfg;
    rig_state_t  state;
    int32_t      pos;          /* the absolute position when idle */
    int32_t      move_from;    /* the current move */
    int32_t      move_target;
    uint32_t     move_interval_us;
    bool         stop_sent;    /* a stop of the current move was requested */
} rig_t;

void rig_init(rig_t *r, const rig_ports_t *io, const rig_config_t *cfg);
void rig_handle(rig_t *r, const rig_event_t *ev);

#endif /* RIG_CTL_H */
