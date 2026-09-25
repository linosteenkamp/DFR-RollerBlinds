#ifndef FAKE_PORTS_H
#define FAKE_PORTS_H

/* Recording fake for ctl_ports_t. Every port call appends one record, in
 * order, so tests can assert what happened AND in which order. Save ports
 * return F.fail[<port>] (0 = OK), so any NVS failure can be injected. */

#include <string.h>
#include "ctl.h"

typedef enum {
    F_MOTION_START, F_MOTION_STOP, F_SET_REVERSED,
    F_SAVE_MOVE_FLAG, F_SAVE_SPAN, F_SAVE_POSITION, F_SAVE_REVERSED, F_SAVE_TRAVEL,
    F_MOTION_ALLOWED, F_OPERATIONAL, F_REPORT_LIFT, F_REPORT_MODE, F_REPORT_TRAVEL,
    F_FACTORY_RESET, F_LED_SET, F_LED_FLASH,
    F_CAL_TIMER_START, F_CAL_TIMER_STOP, F_REPORT_TIMER_START, F_REPORT_TIMER_STOP,
    F_TRACE, F_LOG,
    F__COUNT
} fcall_t;

typedef struct { fcall_t what; int32_t a, b; } frec_t;

static struct {
    frec_t    rec[1024];
    int       n;
    bool      moving;         /* what motion_is_moving() returns */
    int32_t   steps;          /* what motion_steps() returns */
    bool      joined;         /* what net_joined() returns */
    ctl_err_t start_err;      /* what motion_start() returns */
    int32_t   last_cap;       /* hard_cap of the last motion_start */
    uint32_t  last_cruise;    /* profile cruise_us of the last motion_start */
    ctl_err_t fail[F__COUNT]; /* non-zero: that save port returns it */
} F;

static inline void f_rec_add(fcall_t w, int32_t a, int32_t b)
{
    if (F.n < (int)(sizeof F.rec / sizeof F.rec[0])) {
        F.rec[F.n++] = (frec_t){ w, a, b };
    }
}

static inline int f_count(fcall_t w)
{
    int n = 0;
    for (int i = 0; i < F.n; i++) if (F.rec[i].what == w) n++;
    return n;
}

static inline int f_count_a(fcall_t w, int32_t a)
{
    int n = 0;
    for (int i = 0; i < F.n; i++) if (F.rec[i].what == w && F.rec[i].a == a) n++;
    return n;
}

static inline int f_count_ab(fcall_t w, int32_t a, int32_t b)
{
    int n = 0;
    for (int i = 0; i < F.n; i++) {
        if (F.rec[i].what == w && F.rec[i].a == a && F.rec[i].b == b) n++;
    }
    return n;
}

/* index of the first / last call of w, or -1 */
static inline int f_first(fcall_t w)
{
    for (int i = 0; i < F.n; i++) if (F.rec[i].what == w) return i;
    return -1;
}

static inline int f_last(fcall_t w)
{
    for (int i = F.n - 1; i >= 0; i--) if (F.rec[i].what == w) return i;
    return -1;
}

/* the last record of w; {F__COUNT, 0, 0} when there is none */
static inline frec_t f_rec(fcall_t w)
{
    int i = f_last(w);
    return i < 0 ? (frec_t){ F__COUNT, 0, 0 } : F.rec[i];
}

static ctl_err_t p_motion_start(int32_t from, int32_t to,
                                const motion_profile_t *prof, int32_t cap)
{
    f_rec_add(F_MOTION_START, from, to);
    F.last_cap = cap;
    F.last_cruise = prof->cruise_us;
    if (F.start_err == CTL_OK) F.moving = true;
    return F.start_err;
}
static void      p_motion_stop(void)            { f_rec_add(F_MOTION_STOP, 0, 0); }
static bool      p_motion_is_moving(void)       { return F.moving; }
static int32_t   p_motion_steps(void)           { return F.steps; }
static void      p_set_reversed(bool r)         { f_rec_add(F_SET_REVERSED, r, 0); }
static ctl_err_t p_save_move_flag(bool on)      { f_rec_add(F_SAVE_MOVE_FLAG, on, 0); return F.fail[F_SAVE_MOVE_FLAG]; }
static ctl_err_t p_save_span(bool v, int32_t s) { f_rec_add(F_SAVE_SPAN, v, s); return F.fail[F_SAVE_SPAN]; }
static ctl_err_t p_save_position(bool k, int32_t s) { f_rec_add(F_SAVE_POSITION, k, s); return F.fail[F_SAVE_POSITION]; }
static ctl_err_t p_save_reversed(bool r)        { f_rec_add(F_SAVE_REVERSED, r, 0); return F.fail[F_SAVE_REVERSED]; }
static ctl_err_t p_save_travel(uint16_t s)      { f_rec_add(F_SAVE_TRAVEL, s, 0); return F.fail[F_SAVE_TRAVEL]; }
static void      p_motion_allowed(bool a)       { f_rec_add(F_MOTION_ALLOWED, a, 0); }
static void      p_operational(bool o)          { f_rec_add(F_OPERATIONAL, o, 0); }
static void      p_report_lift(uint8_t p)       { f_rec_add(F_REPORT_LIFT, p, 0); }
static void      p_report_mode(bool r)          { f_rec_add(F_REPORT_MODE, r, 0); }
static void      p_report_travel(uint16_t s)    { f_rec_add(F_REPORT_TRAVEL, s, 0); }
static bool      p_net_joined(void)             { return F.joined; }
static void      p_factory_reset(void)          { f_rec_add(F_FACTORY_RESET, 0, 0); }
static void      p_led_set(led_pattern_t p)     { f_rec_add(F_LED_SET, p, 0); }
static void      p_led_flash(led_pattern_t p)   { f_rec_add(F_LED_FLASH, p, 0); }
static void      p_cal_timer_start(void)        { f_rec_add(F_CAL_TIMER_START, 0, 0); }
static void      p_cal_timer_stop(void)         { f_rec_add(F_CAL_TIMER_STOP, 0, 0); }
static void      p_report_timer_start(void)     { f_rec_add(F_REPORT_TIMER_START, 0, 0); }
static void      p_report_timer_stop(void)      { f_rec_add(F_REPORT_TIMER_STOP, 0, 0); }
static void      p_trace(uint16_t code, int32_t a, int32_t b) { (void)b; f_rec_add(F_TRACE, code, a); }
static void      p_log(const char *msg)         { (void)msg; f_rec_add(F_LOG, 0, 0); }

static const ctl_ports_t FAKE_PORTS = {
    .motion_start = p_motion_start, .motion_stop = p_motion_stop,
    .motion_is_moving = p_motion_is_moving, .motion_steps = p_motion_steps,
    .motion_set_reversed = p_set_reversed,
    .save_move_flag = p_save_move_flag, .save_span = p_save_span,
    .save_position = p_save_position, .save_reversed = p_save_reversed,
    .save_travel_time = p_save_travel,
    .set_motion_allowed = p_motion_allowed, .set_operational = p_operational,
    .report_lift = p_report_lift, .report_mode = p_report_mode,
    .report_travel_time = p_report_travel,
    .net_joined = p_net_joined, .factory_reset = p_factory_reset,
    .led_set = p_led_set, .led_flash = p_led_flash,
    .cal_timer_start = p_cal_timer_start, .cal_timer_stop = p_cal_timer_stop,
    .report_timer_start = p_report_timer_start, .report_timer_stop = p_report_timer_stop,
    .trace = p_trace, .log = p_log,
};

static inline void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    F.joined = true;
}

#endif /* FAKE_PORTS_H */
