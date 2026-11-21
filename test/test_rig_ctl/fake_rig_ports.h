#ifndef FAKE_RIG_PORTS_H
#define FAKE_RIG_PORTS_H

/* A fake for rig_ports_t that records the calls. Each port call adds one
 * record, in sequence, so that a test can check what occurred and in
 * which sequence. Prior art: test/test_ctl/fake_ports.h. */

#include <string.h>
#include "rig_ctl.h"

typedef enum {
    F_MOTION_START, F_MOTION_STOP, F_PUBLISH,
    F__COUNT
} fcall_t;

typedef struct {
    fcall_t      what;
    int32_t      from, to;       /* F_MOTION_START */
    motion_profile_t prof;       /* F_MOTION_START */
    int32_t      hard_cap;       /* F_MOTION_START */
    rig_record_t rec;            /* F_PUBLISH */
    uint8_t      qos;            /* F_PUBLISH */
    bool         retain;         /* F_PUBLISH */
} frec_t;

static struct {
    frec_t    rec[256];
    int       n;
    int32_t   steps;          /* what motion_steps() returns */
    rig_err_t start_err;      /* what motion_start() returns */
    bool      publish_ok;     /* what publish() returns */
} F;

static inline frec_t *f_add(fcall_t w)
{
    static frec_t sink;
    if (F.n >= (int)(sizeof F.rec / sizeof F.rec[0])) return &sink;
    frec_t *r = &F.rec[F.n++];
    memset(r, 0, sizeof *r);
    r->what = w;
    return r;
}

static inline int f_count(fcall_t w)
{
    int n = 0;
    for (int i = 0; i < F.n; i++) if (F.rec[i].what == w) n++;
    return n;
}

/* The number of publish calls on a topic. */
static inline int f_count_topic(rig_topic_t t)
{
    int n = 0;
    for (int i = 0; i < F.n; i++) {
        if (F.rec[i].what == F_PUBLISH && F.rec[i].rec.topic == t) n++;
    }
    return n;
}

/* The last record of w, or NULL. */
static inline const frec_t *f_last(fcall_t w)
{
    for (int i = F.n - 1; i >= 0; i--) if (F.rec[i].what == w) return &F.rec[i];
    return NULL;
}

/* The last publish call on a topic, or NULL. */
static inline const frec_t *f_last_topic(rig_topic_t t)
{
    for (int i = F.n - 1; i >= 0; i--) {
        if (F.rec[i].what == F_PUBLISH && F.rec[i].rec.topic == t) return &F.rec[i];
    }
    return NULL;
}

static rig_err_t p_motion_start(int32_t from, int32_t to,
                                const motion_profile_t *prof, int32_t hard_cap)
{
    frec_t *r = f_add(F_MOTION_START);
    r->from = from;
    r->to = to;
    r->prof = *prof;
    r->hard_cap = hard_cap;
    return F.start_err;
}

static void p_motion_stop(void) { f_add(F_MOTION_STOP); }

static int32_t p_motion_steps(void) { return F.steps; }

static bool p_publish(const rig_record_t *rec, uint8_t qos, bool retain)
{
    frec_t *r = f_add(F_PUBLISH);
    r->rec = *rec;
    r->qos = qos;
    r->retain = retain;
    return F.publish_ok;
}

static const rig_ports_t FAKE_PORTS = {
    .motion_start = p_motion_start,
    .motion_stop  = p_motion_stop,
    .motion_steps = p_motion_steps,
    .publish      = p_publish,
};

static inline void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    F.publish_ok = true;
}

#endif /* FAKE_RIG_PORTS_H */
