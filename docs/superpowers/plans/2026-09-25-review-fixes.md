# Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every dispatcher decision into a pure, host-tested `ctl` module, then fix the 2026-09-25 review findings test-first, shipping as v2.4.0.

**Architecture:** `src/ctl.c` owns all state that `src/main.c` holds today and makes every motion, calibration and persistence decision; every side effect goes through a `ctl_ports_t` struct of function pointers, bound to the real modules in `main.c` and to a recording fake in host tests. Phase 1 is a behaviour-preserving extraction pinned by characterisation tests; phase 2 fixes each finding red-then-green; phase 3 changes the code outside the core (ISR queue, keypad, ramp, LED).

**Tech Stack:** C11, ESP-IDF 5.5 via PlatformIO (`pioarduino` platform 55.03.31-2), FreeRTOS queue sets, Unity host tests (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-09-25-review-fixes-design.md` (read it before starting; review finding IDs S1…S12 / T1…T8 used below come from its table).

## Global Constraints

- Branch: `review-fixes` (off `main`). Do not touch `fn-to-d3` except in Task 17.
- `src/ctl.c` and every header it includes must compile on the host with **no ESP-IDF or FreeRTOS headers** (`pio test -e native` builds with `-std=c11 -Wall -Wextra -I include` and `#include`s the `.c` files directly).
- Only the dispatcher task calls `ctl_handle()`. `ctl` calls nothing but its ports and the pure modules (`position`, `ramp`).
- Do not rename NVS keys, `APP_EVT_ZB_SET_SPEED`, or existing trace codes. New `trace_code_t` and `app_event_type_t` values go **at the end** of their enums (the RTC trace survives resets; renumbering relabels old records).
- Calibration timeout stays `CAL_TIMEOUT_US (10LL * 60 * 1000000)`.
- Motion constants in `src/main.c` (`START_US 500`, `ACCEL_STEPS 800`, `JOG_CRUISE_US 300`, `JOG_START_US 900`, `MIN_SPAN_STEPS 6000`, `HARD_CAP_MARGIN 2400`, `JOG_UNBOUNDED 2000000`, `REPORT_PERIOD_US`) keep their values.
- Pin map stays the **current** one on this branch (Fn D6/GPIO16, LED D3/GPIO21).
- Firmware build: `pio run -e seeed_xiao_esp32c6_zigbee`. Before any build that will be flashed, `rm -rf .pio/build/seeed_xiao_esp32c6_zigbee` first: PlatformIO has flashed a stale binary here before while reporting SUCCESS.
- Never open the serial port of, or flash, a board that is in service (`blinds lounge side`). `bench2` (USB serial `A0:F2:62:87:8E:0C`) is the bench board. Flashing and bench checks are **human gates**: stop and ask the owner.
- Never restart zigbee2mqtt or touch the z2m box (`499.steenkamps.org`) without the owner's go-ahead in that moment.
- Commit messages: prose explaining why, ending with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. A Zigbee goto that preempts a keypad jog, after which the key is released: the travel must continue. The dead-man must only stop moves a jog started. Test in Task 6.
2. Zigbee Stop in the end-of-move gap while a goto is parked: nothing runs after `MOTION_DONE`. Test in Task 5.
3. Identify started and stopped inside Calibration Mode: the LED returns to the calibration pattern, not OFF. Test in Task 3.
4. `travel_time` written during a move: the running move keeps its cruise, the next move uses the new one. Test in Task 2.
5. Mark 2's span saves but its position save fails: the next boot must come up needing a Re-home with the new span, never Calibrated. Test in Task 7.

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `include/led_pattern.h` | create | `led_pattern_t` alone, so pure code can name LED patterns |
| `include/motion_profile.h` | create | `motion_profile_t` alone |
| `include/blind_store_data.h` | create | `blind_store_data_t` alone |
| `include/status_led.h`, `include/motion.h`, `include/blind_store.h` | modify | include the three headers above instead of defining the types |
| `include/ctl.h`, `src/ctl.c` | create | the dispatcher core and its ports contract |
| `test/test_ctl/fake_ports.h`, `test/test_ctl/test_ctl.c` | create | recording fake ports and the ctl test suite |
| `src/main.c` | modify | shrinks to wiring: GPIO map, constants, ports table, queues, dispatcher loop, `app_main` |
| `include/app_event.h` | modify | `APP_EVT_BOOT_SYNC`; comment fixes |
| `include/trace.h` | modify | `TRC_DEADMAN_STOP`, `TRC_DONE_POST_FAILED` |
| `src/motion.c`, `include/motion.h` | modify | dedicated done queue, post-failure counter, ramp calls |
| `src/ramp.c`, `include/ramp.h`, `test/test_ramp/test_ramp.c` | modify | stop maths, `RAMP_HOT` |
| `src/keypad.c`, `include/keypad.h` | modify | `keypad_key_held()`; `post_kp()` stops calling `motion_stop()` |
| `src/status_led.c` | modify | critical section around base/transient state |
| `z2m/dfr_roller_blinds.js` | modify | `motor_reversed` read-back |
| `src/CMakeLists.txt`, `platformio.ini` | modify | add `ctl.c`; add `test_ctl` to the native filter |
| `CLAUDE.md`, `CONTEXT.md`, `DEVELOPER_GUIDE.md`, design spec | modify | docs (Task 14) |

---

## Phase 1: extraction, no behaviour change

### Task 1: Split the shared types into pure headers

**Files:**
- Create: `include/led_pattern.h`, `include/motion_profile.h`, `include/blind_store_data.h`
- Modify: `include/status_led.h`, `include/motion.h`, `include/blind_store.h`

**Interfaces:**
- Produces: `led_pattern_t` in `led_pattern.h`; `motion_profile_t` in `motion_profile.h`; `blind_store_data_t` in `blind_store_data.h`. Names and members unchanged.

- [ ] **Step 1: Create `include/led_pattern.h`** by moving the enum out of `status_led.h` verbatim:

```c
#ifndef LED_PATTERN_H
#define LED_PATTERN_H

/* Spec §2 LED patterns. Base patterns persist; transient patterns
 * (ACK/ERROR) play once and revert to the base. Kept free of ESP-IDF
 * headers so pure modules (ctl) can name a pattern. */
typedef enum {
    LED_OFF = 0,        /* normal: calibrated, idle */
    LED_CAL_MARK1,      /* 500 ms on / 1 s off: awaiting mark 1 (Open) */
    LED_CAL_MARK2,      /* fast ~5 Hz blink: awaiting mark 2 (Closed) */
    LED_UNCAL,          /* double-flash every 3 s: uncalibrated / pos unknown */
    LED_IDENTIFY,       /* steady rapid blink: Zigbee Identify */
    LED_NO_NETWORK,     /* 2 s on, 1 s off: not joined to a Zigbee network */
    LED_ACK,            /* transient: three quick flashes */
    LED_ERROR,          /* transient: five rapid flashes */
} led_pattern_t;

#endif /* LED_PATTERN_H */
```

In `include/status_led.h`, delete the enum and its comment and add `#include "led_pattern.h"` after `#include "esp_err.h"`.

- [ ] **Step 2: Create `include/motion_profile.h`:**

```c
#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

#include <stdint.h>

/* Kept free of ESP-IDF headers so pure modules (ctl) can build a profile. */
typedef struct {
    uint32_t cruise_us;   /* step interval at cruise (e.g. 300 = ~3.3 kHz) */
    uint32_t start_us;    /* first/last-step interval (e.g. 500 = 2 kHz) */
    int32_t  accel_steps; /* ramp length in steps (e.g. 800) */
} motion_profile_t;

#endif /* MOTION_PROFILE_H */
```

In `include/motion.h`, delete the `motion_profile_t` typedef and add `#include "motion_profile.h"` after `#include "freertos/queue.h"`.

- [ ] **Step 3: Create `include/blind_store_data.h`:**

```c
#ifndef BLIND_STORE_DATA_H
#define BLIND_STORE_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Everything blind_store persists, as loaded at boot. Kept free of ESP-IDF
 * headers so pure modules (ctl) can take it. */
typedef struct {
    bool    span_valid;
    int32_t closed_steps;
    bool    pos_known;
    int32_t cur_steps;
    bool    motor_reversed;
    bool    move_in_progress;   /* set at move start, cleared on clean end */
    uint16_t travel_secs;       /* full-travel time in seconds; 0 = never set */
} blind_store_data_t;

#endif /* BLIND_STORE_DATA_H */
```

In `include/blind_store.h`, delete the typedef and add `#include "blind_store_data.h"` after `#include "esp_err.h"`.

- [ ] **Step 4: Build firmware and host tests**

Run: `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: firmware `SUCCESS`; `74 test cases: 74 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add include/led_pattern.h include/motion_profile.h include/blind_store_data.h include/status_led.h include/motion.h include/blind_store.h
git commit -m "Split LED, motion-profile and store types into pure headers

The dispatcher core that follows must build in host tests, where no
ESP-IDF header exists. These three types were only reachable through
headers that pull in esp_err.h or FreeRTOS. No behaviour change.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: `ctl` core, fake ports, and motion characterisation tests

The code in this task is a **transcription** of `src/main.c`'s dispatcher logic with no behaviour change. Two deliberate exceptions, both invisible to behaviour: `s_cal_mode` is replaced by `pos.cal != POS_CAL_NONE` (identical in every path: `position_cal_abort`, `position_wipe` and a finishing `position_cal_mark` all set `POS_CAL_NONE`), and the three copies of the abort sequence become one `cal_abort()`, which also stops the calibration timer on the timeout path (stopping a one-shot that already fired is a no-op). Log messages lose their `esp_err_to_name()` detail; the trace keeps the error code.

**Files:**
- Create: `include/ctl.h`, `src/ctl.c`, `test/test_ctl/fake_ports.h`, `test/test_ctl/test_ctl.c`
- Modify: `platformio.ini` (native `test_filter`)

**Interfaces:**
- Consumes: `led_pattern_t`, `motion_profile_t`, `blind_store_data_t` (Task 1); `position.h`, `ramp.h`, `app_event.h`, `keypad_logic.h`, `trace.h` codes.
- Produces (used by Tasks 3-9):
  - `typedef int ctl_err_t; #define CTL_OK 0`
  - `ctl_ports_t` (fields listed in Step 1), `ctl_config_t`, `ctl_t`
  - `void ctl_init(ctl_t *c, const ctl_ports_t *io, const ctl_config_t *cfg, const blind_store_data_t *boot);`
  - `void ctl_handle(ctl_t *c, const app_event_t *ev);`
  - `bool ctl_calibrated(const ctl_t *c);`
  - `bool ctl_in_cal_mode(const ctl_t *c);`
  - `led_pattern_t ctl_led_pattern(const ctl_t *c);`
  - `uint16_t ctl_achieved_travel_secs(const ctl_t *c);`
  - `void ctl_refresh_outputs(ctl_t *c);` (public only until Task 8)
  - Fake: `F` (recorder state), `FAKE_PORTS`, `fake_reset()`, `f_count()`, `f_first()`, `f_last()`, `f_rec()`, `f_count_a()`, `f_count_ab()`

- [ ] **Step 1: Create `include/ctl.h`**

```c
#ifndef CTL_H
#define CTL_H

/* Dispatcher core. Every decision about motion, calibration and persistence
 * happens here, one app_event_t at a time. Pure C with no ESP-IDF headers so
 * it runs in host tests: every side effect goes through ctl_ports_t, which
 * main.c binds to the real modules and the tests bind to a recording fake.
 *
 * Concurrency: only the dispatcher task may call ctl_handle(), and ctl calls
 * nothing but its ports and the pure position/ramp modules. */

#include <stdbool.h>
#include <stdint.h>
#include "app_event.h"
#include "blind_store_data.h"
#include "keypad_logic.h"
#include "led_pattern.h"
#include "motion_profile.h"
#include "position.h"

typedef int ctl_err_t;   /* 0 = OK; carries esp_err_t values unchanged */
#define CTL_OK 0

typedef struct {
    /* motion */
    ctl_err_t (*motion_start)(int32_t from_steps, int32_t to_steps,
                              const motion_profile_t *prof, int32_t hard_cap);
    void      (*motion_stop)(void);
    bool      (*motion_is_moving)(void);   /* removed in Task 5 */
    int32_t   (*motion_steps)(void);
    void      (*motion_set_reversed)(bool reversed);
    /* persistence (NVS namespace "blind") */
    ctl_err_t (*save_move_flag)(bool in_progress);
    ctl_err_t (*save_span)(bool span_valid, int32_t closed_steps);
    ctl_err_t (*save_position)(bool pos_known, int32_t cur_steps);
    ctl_err_t (*save_reversed)(bool reversed);
    ctl_err_t (*save_travel_time)(uint16_t secs);
    /* Zigbee */
    void (*set_motion_allowed)(bool allowed);
    void (*set_operational)(bool calibrated);
    void (*report_lift)(uint8_t pct);
    void (*report_mode)(bool reversed);
    void (*report_travel_time)(uint16_t secs);
    bool (*net_joined)(void);
    void (*factory_reset)(void);
    /* operator */
    void (*led_set)(led_pattern_t base);
    void (*led_flash)(led_pattern_t transient);
    /* timers */
    void (*cal_timer_start)(void);
    void (*cal_timer_stop)(void);
    void (*report_timer_start)(void);
    void (*report_timer_stop)(void);
    /* diagnostics */
    void (*trace)(uint16_t code, int32_t a, int32_t b);
    void (*log)(const char *msg);
} ctl_ports_t;

typedef struct {
    motion_profile_t move;   /* travel profile; cruise_us recomputed from travel time */
    motion_profile_t jog;
    int32_t min_span_steps;
    int32_t hard_cap_margin;
    int32_t jog_unbounded;
} ctl_config_t;

typedef struct {
    const ctl_ports_t *io;
    ctl_config_t cfg;          /* cfg.move.cruise_us is live */
    position_t pos;
    int32_t  raw;              /* raw step frame; valid in Calibration Mode too */
    bool     reversed;
    uint16_t travel_secs;      /* requested full-travel time; 0 = unset */
    bool     identifying;      /* Zigbee Identify in progress */
    bool     cal_moved;        /* jogged during this Calibration Mode session */
    bool     cal_abort_pending;/* timeout arrived mid-move: abort on MOTION_DONE */
    bool     pending_valid;    /* Zigbee goto parked while a move decelerates */
    uint8_t  pending_pct;
    uint8_t  move_start_pct;   /* lift % when the current move began */
} ctl_t;

void ctl_init(ctl_t *c, const ctl_ports_t *io, const ctl_config_t *cfg,
              const blind_store_data_t *boot);
void ctl_handle(ctl_t *c, const app_event_t *ev);

bool          ctl_calibrated(const ctl_t *c);
bool          ctl_in_cal_mode(const ctl_t *c);
led_pattern_t ctl_led_pattern(const ctl_t *c);
uint16_t      ctl_achieved_travel_secs(const ctl_t *c);

/* Pushes lockout, ConfigStatus, lift and the LED base pattern. Public only
 * while app_main still does the post-join sync itself (removed in Task 8). */
void ctl_refresh_outputs(ctl_t *c);

#endif /* CTL_H */
```

- [ ] **Step 2: Create `src/ctl.c`**

```c
/**
 * @file ctl.c
 * @brief Dispatcher core. Every decision the firmware makes about motion,
 *        calibration and persistence happens here, one app_event_t at a time,
 *        on the dispatcher task. Gesture matrix and calibration flow per
 *        CONTEXT.md / spec §6; z2m motion lockout per spec §5. Pure: every
 *        side effect goes through c->io.
 */
#include "ctl.h"
#include "ramp.h"
#include "trace.h"

static bool in_cal(const ctl_t *c) { return c->pos.cal != POS_CAL_NONE; }

static bool moving(const ctl_t *c) { return c->io->motion_is_moving(); }

bool ctl_calibrated(const ctl_t *c)  { return position_calibrated(&c->pos); }
bool ctl_in_cal_mode(const ctl_t *c) { return in_cal(c); }

led_pattern_t ctl_led_pattern(const ctl_t *c)
{
    if (c->identifying) return LED_IDENTIFY;
    if (c->pos.cal == POS_CAL_WAIT_MARK1 || c->pos.cal == POS_CAL_WAIT_REHOME) {
        return LED_CAL_MARK1;
    }
    if (c->pos.cal == POS_CAL_WAIT_MARK2) return LED_CAL_MARK2;
    if (!c->io->net_joined()) return LED_NO_NETWORK;
    return ctl_calibrated(c) ? LED_OFF : LED_UNCAL;
}

void ctl_refresh_outputs(ctl_t *c)
{
    bool cal = ctl_calibrated(c);
    c->io->set_motion_allowed(cal);
    c->io->set_operational(cal);
    c->io->report_lift(position_lift_pct(&c->pos));
    c->io->led_set(ctl_led_pattern(c));
}

/* ---------- travel time ---------- */

/* Derive the cruise interval from the requested travel time and the current
 * span (clamped). Read at move start, so a change mid-move applies to the
 * NEXT move. */
static void recompute_cruise(ctl_t *c)
{
    c->cfg.move.cruise_us = ramp_us_from_travel_time(c->travel_secs,
                                                     c->pos.closed_steps);
}

/* The duration the current cruise interval actually produces. With no span
 * there is nothing to clamp against, so the request stands unchanged;
 * calibration will correct it. */
uint16_t ctl_achieved_travel_secs(const ctl_t *c)
{
    return (c->pos.closed_steps > 0)
        ? ramp_travel_time_from_us(c->cfg.move.cruise_us, c->pos.closed_steps)
        : c->travel_secs;
}

/* Recompute the cruise interval and tell z2m what was actually applied.
 * Called on a z2m write and whenever the span changes — the stored value is
 * a duration, so its meaning moves with the span. */
static void apply_travel_time(ctl_t *c)
{
    recompute_cruise(c);
    c->io->report_travel_time(ctl_achieved_travel_secs(c));
}

/* ---------- moves ---------- */

static int32_t hard_cap(const ctl_t *c)
{
    /* Watchdog cap applies to calibrated moves only. Every state where the
     * operator jogs with a deadman hold (uncalibrated, Position Unknown,
     * calibration mode) must be uncapped — matching jog()'s target choice —
     * or Re-home/recal jogs would be refused while span_valid is still set. */
    return ctl_calibrated(c) ? c->pos.closed_steps + c->cfg.hard_cap_margin
                             : c->cfg.jog_unbounded + 1;
}

static void start_move(ctl_t *c, int32_t target, const motion_profile_t *prof)
{
    if (target == c->raw) {
        c->io->trace(TRC_MOVE_NOOP, target, c->pos.closed_steps);
        c->io->led_flash(LED_ACK);   /* heard you; already there */
        ctl_refresh_outputs(c);      /* keep reports honest, no NVS churn */
        return;
    }
    int32_t cap = hard_cap(c);       /* hoisted so a refusal can record it */
    c->move_start_pct = position_lift_pct(&c->pos);
    c->io->trace(TRC_MOVE_START, c->raw, target);
    c->io->save_move_flag(true);
    ctl_err_t err = c->io->motion_start(c->raw, target, prof, cap);
    if (err != CTL_OK) {
        c->io->save_move_flag(false);
        c->io->trace(TRC_MOVE_REFUSED, err, cap);
        c->io->led_flash(LED_ERROR);  /* refusing — needs attention */
        c->io->log("move refused");
    } else {
        c->io->report_timer_start();
    }
}

static void goto_pct(ctl_t *c, uint8_t pct)
{
    if (!ctl_calibrated(c)) return;
    start_move(c, position_target_for_pct(&c->pos, pct), &c->cfg.move);
}

static void jog(ctl_t *c, bool up)
{
    int32_t target;
    if (ctl_calibrated(c)) {
        target = up ? 0 : c->pos.closed_steps;              /* clamped jog */
    } else {
        target = up ? c->raw - c->cfg.jog_unbounded
                    : c->raw + c->cfg.jog_unbounded;
    }
    start_move(c, target, &c->cfg.jog);
}

/* ---------- calibration (spec §6) ---------- */

/* Leave Calibration Mode without a result. The span stays untouched, but if
 * the blind was jogged while in the mode the stored position no longer
 * matches reality — drop to Position Unknown so a Re-home is demanded
 * instead of trusting stale state. */
static void cal_abort(ctl_t *c)
{
    c->cal_abort_pending = false;
    position_cal_abort(&c->pos);
    c->io->cal_timer_stop();
    if (c->cal_moved && c->pos.span_valid) {
        position_mark_unknown(&c->pos);
        c->io->save_position(false, 0);
    }
}

static void enter_or_exit_cal(ctl_t *c)
{
    if (moving(c)) return;                     /* only from standstill */
    if (in_cal(c)) {                           /* second long-press: abort */
        cal_abort(c);
    } else {
        position_cal_enter(&c->pos);
        c->cal_moved = false;
        c->cal_abort_pending = false;
        c->raw = c->pos.pos_known ? c->pos.cur_steps : 0;   /* fresh raw frame */
        c->io->cal_timer_start();
    }
    ctl_refresh_outputs(c);
}

static void handle_mark(ctl_t *c)
{
    if (moving(c)) { c->io->motion_stop(); return; }   /* Fn tap = stop first */
    if (!in_cal(c)) {
        c->io->led_flash(LED_ACK);                      /* heard you; nothing to mark */
        return;
    }
    /* raw stays one continuous frame through the whole calibration —
     * position_cal_mark stores mark 1's raw and computes the span as the
     * difference at mark 2, so the caller must NOT re-anchor between marks. */
    if (position_cal_mark(&c->pos, c->raw, c->cfg.min_span_steps)) {
        if (!in_cal(c)) {                               /* calibration finished */
            c->io->cal_timer_stop();
            c->io->save_span(c->pos.span_valid, c->pos.closed_steps);
            c->io->save_position(c->pos.pos_known, c->pos.cur_steps);
            c->raw = c->pos.cur_steps;                  /* re-anchor raw frame */
            apply_travel_time(c);   /* new span -> same duration, new interval */
        }
        c->io->led_flash(LED_ACK);
    } else {
        c->io->led_flash(LED_ERROR);                    /* stay awaiting mark 2 */
    }
    ctl_refresh_outputs(c);
}

static void toggle_reversed(ctl_t *c)
{
    if (moving(c)) return;
    c->reversed = !c->reversed;
    c->io->motion_set_reversed(c->reversed);
    c->io->save_reversed(c->reversed);
    /* direction sense changed -> all stored steps are meaningless (spec §6) */
    bool was_cal = in_cal(c);
    position_wipe(&c->pos);
    if (was_cal) c->io->cal_timer_stop();
    c->io->save_span(false, 0);
    c->io->save_position(false, 0);
    c->raw = 0;
    c->io->report_mode(c->reversed);
    c->io->led_flash(LED_ACK);
    apply_travel_time(c);   /* span was wiped: fall back until recalibrated */
    ctl_refresh_outputs(c);
}

/* ---------- Zigbee ---------- */

/* Spec §7: a command during a move preempts — decelerate to stop, then run
 * the new target (last writer wins). The target is parked until MOTION_DONE. */
static void zb_goto_request(ctl_t *c, uint8_t pct)
{
    if (!ctl_calibrated(c)) return;   /* lockout backstop */
    bool mv = moving(c);
    c->io->trace(TRC_ZB_CMD, pct, mv ? 1 : 0);
    if (mv) {
        c->pending_pct   = pct;
        c->pending_valid = true;
        c->io->motion_stop();
    } else {
        goto_pct(c, pct);
    }
}

/* ---------- events ---------- */

static void handle_keypad(ctl_t *c, kp_event_t e)
{
    c->pending_valid = false;   /* any local input is the last writer (spec §7) */
    bool cal_dev = ctl_calibrated(c);
    /* Calibration state folded into the record (0x100 bit): a lockout
     * early-return downstream is otherwise indistinguishable from a
     * dispatcher that silently decided nothing. */
    c->io->trace(TRC_KEY_EVENT, e.type, e.key | (cal_dev ? 0x100 : 0));
    switch (e.type) {
    case KP_EVT_TAP:
        if (moving(c)) { c->io->motion_stop(); break; }   /* any tap stops */
        if (e.key == KEY_FN) { handle_mark(c); break; }
        if (cal_dev) {                                     /* full travel */
            goto_pct(c, e.key == KEY_UP ? 0 : 100);
        } else if (!in_cal(c)) {
            /* Inside Calibration Mode a short Up/Down tap stays quiet:
             * LED_ERROR there already means "mark rejected", and firing it
             * here too could send the operator jogging the wrong way. The
             * lockout signal only applies outside the mode. */
            c->io->led_flash(LED_ERROR);   /* lockout: needs calibration */
        }
        break;
    case KP_EVT_HOLD_START:
        if (!moving(c)) jog(c, e.key == KEY_UP);
        break;
    case KP_EVT_HOLD_END:
        c->io->motion_stop();
        break;
    case KP_EVT_FN_LONG:
        enter_or_exit_cal(c);
        break;
    /* KP_EVT_CHORD_REVERSE is retired and never emitted; motor_reversed is a
     * z2m setting (APP_EVT_ZB_SET_REVERSED). */
    case KP_EVT_FACTORY_RESET:
        /* Never mid-move: the reset reboots, and rebooting with
         * move_in_progress still set drops the device to Position Unknown. */
        if (moving(c)) {
            c->io->log("factory reset ignored: stop the blind first");
            break;
        }
        c->io->log("keypad factory reset: erasing Zigbee state");
        c->io->factory_reset();   /* does not return on the device */
        break;
    default:
        break;
    }
}

static void on_motion_done(ctl_t *c, int32_t steps, bool completed)
{
    c->io->trace(TRC_MOVE_DONE, steps, completed);
    c->io->report_timer_stop();
    c->raw = steps;
    ctl_err_t perr = CTL_OK;
    if (in_cal(c)) {
        if (!c->cal_moved) {
            /* first jog of this session: position on disk is now stale —
             * persist untrusted so a power blip can't boot back into a
             * confidently wrong Calibrated state */
            perr = c->io->save_position(false, 0);
        }
        c->cal_moved = true;
        if (c->cal_abort_pending) cal_abort(c);   /* timeout hit mid-jog */
    } else {
        position_set_current(&c->pos, position_clamp(&c->pos, steps));
        perr = c->io->save_position(c->pos.pos_known, c->pos.cur_steps);
        /* Dead-zone feedback: a move too small to change the reported lift %
         * is invisible to the operator and to z2m alike, so it is
         * indistinguishable from a dead key unless we say so. */
        if (ctl_calibrated(c) &&
            position_lift_pct(&c->pos) == c->move_start_pct) {
            c->io->led_flash(LED_ACK);
        }
    }
    if (perr == CTL_OK) {
        c->io->save_move_flag(false);
    } else {
        /* leaving the flag set forces a re-home next boot rather than
         * trusting a position that failed to persist */
        c->io->log("position save failed: move flag left set, re-home on next boot");
    }
    if (c->pending_valid && !in_cal(c) && ctl_calibrated(c)) {
        uint8_t pct = c->pending_pct;   /* ZB preemption: last writer */
        c->pending_valid = false;
        goto_pct(c, pct);
    }
    ctl_refresh_outputs(c);
}

void ctl_handle(ctl_t *c, const app_event_t *ev)
{
    switch (ev->type) {
    case APP_EVT_KEYPAD:   handle_keypad(c, ev->kp);      break;
    case APP_EVT_ZB_OPEN:  zb_goto_request(c, 0);         break;
    case APP_EVT_ZB_CLOSE: zb_goto_request(c, 100);       break;
    case APP_EVT_ZB_GOTO:  zb_goto_request(c, ev->pct);   break;
    case APP_EVT_ZB_STOP:
        c->pending_valid = false;
        c->io->motion_stop();
        break;
    case APP_EVT_ZB_SET_REVERSED:
        if (moving(c)) {
            c->io->report_mode(c->reversed);   /* reject: rewrite truth */
        } else if (ev->on != c->reversed) {
            toggle_reversed(c);
        }
        break;
    case APP_EVT_ZB_SET_SPEED:
        c->travel_secs = ev->secs;
        c->io->save_travel_time(c->travel_secs);
        apply_travel_time(c);
        break;
    case APP_EVT_ZB_NET_LOST:
    case APP_EVT_ZB_NET_JOINED:
        ctl_refresh_outputs(c);
        break;
    case APP_EVT_MOTION_DONE:
        on_motion_done(c, ev->steps, ev->completed);
        break;
    case APP_EVT_CAL_TIMEOUT:
        if (!in_cal(c)) break;
        if (moving(c)) {
            /* finish stopping; abort on DONE so the DONE steps aren't written
             * into position as trusted state */
            c->cal_abort_pending = true;
            c->io->motion_stop();
        } else {
            cal_abort(c);
            ctl_refresh_outputs(c);
        }
        break;
    case APP_EVT_REPORT_TICK:
        if (moving(c) && ctl_calibrated(c)) {
            position_t tmp = c->pos;
            position_set_current(&tmp, c->io->motion_steps());
            c->io->report_lift(position_lift_pct(&tmp));
        }
        break;
    case APP_EVT_IDENTIFY:
        c->identifying = ev->on;
        ctl_refresh_outputs(c);
        break;
    default:
        break;
    }
}

void ctl_init(ctl_t *c, const ctl_ports_t *io, const ctl_config_t *cfg,
              const blind_store_data_t *boot)
{
    *c = (ctl_t){0};
    c->io  = io;
    c->cfg = *cfg;
    position_init(&c->pos, boot->span_valid, boot->closed_steps,
                  boot->pos_known, boot->cur_steps);
    if (boot->move_in_progress) {
        /* power died mid-move: position no longer trusted (spec §6) */
        position_mark_unknown(&c->pos);
        io->save_position(false, 0);
        io->save_move_flag(false);
        io->log("unclean shutdown mid-move -> Position Unknown, re-home needed");
    }
    c->reversed    = boot->motor_reversed;
    c->travel_secs = boot->travel_secs;
    /* Cruise speed must be correct before the dispatcher can serve a keypad
     * tap, which is well before the Zigbee join completes. */
    recompute_cruise(c);
    c->raw = c->pos.pos_known ? c->pos.cur_steps : 0;
}
```

- [ ] **Step 3: Create `test/test_ctl/fake_ports.h`**

```c
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
```

Note on `p_trace`: the record stores `a = code`, `b = the trace's a`, so `f_count_a(F_TRACE, TRC_MOVE_NOOP)` counts records of that code.

- [ ] **Step 4: Create `test/test_ctl/test_ctl.c` with the helpers and the motion characterisation tests**

```c
#include <unity.h>
#include "../../src/position.c"
#include "../../src/ramp.c"
#include "../../src/ctl.c"
#include "fake_ports.h"

void setUp(void) {}
void tearDown(void) {}

#define SPAN 24000

static ctl_t C;
static const ctl_config_t CFG = {
    .move = { 150, 500, 800 },
    .jog  = { 300, 900, 800 },
    .min_span_steps  = 6000,
    .hard_cap_margin = 2400,
    .jog_unbounded   = 2000000,
};

/* Boot from a given NVS state; clears the call log afterwards so each test
 * sees only what its own events caused. */
static void boot_full(bool span_ok, int32_t span, bool pos_ok, int32_t pos,
                      bool moving_flag, uint16_t travel)
{
    fake_reset();
    blind_store_data_t b = {
        .span_valid = span_ok, .closed_steps = span,
        .pos_known = pos_ok, .cur_steps = pos,
        .motor_reversed = false, .move_in_progress = moving_flag,
        .travel_secs = travel,
    };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    F.n = 0;
}
static void boot_cal(int32_t pos) { boot_full(true, SPAN, true, pos, false, 0); }
static void boot_uncal(void)      { boot_full(false, 0, false, 0, false, 0); }

static void ev_type(app_event_type_t t)
{
    app_event_t e = { .type = t };
    ctl_handle(&C, &e);
}
static void kp(kp_event_type_t t, key_id_t k)
{
    app_event_t e = { .type = APP_EVT_KEYPAD, .kp = { .type = t, .key = k } };
    ctl_handle(&C, &e);
}
static void zb_goto(uint8_t pct)
{
    app_event_t e = { .type = APP_EVT_ZB_GOTO, .pct = pct };
    ctl_handle(&C, &e);
}
static void ev_on(app_event_type_t t, bool on)
{
    app_event_t e = { .type = t, .on = on };
    ctl_handle(&C, &e);
}
static void ev_secs(uint16_t secs)
{
    app_event_t e = { .type = APP_EVT_ZB_SET_SPEED, .secs = secs };
    ctl_handle(&C, &e);
}
/* The ISR finished and its MOTION_DONE is delivered now. */
static void done(int32_t steps, bool completed)
{
    F.moving = false;
    app_event_t e = { .type = APP_EVT_MOTION_DONE, .steps = steps,
                      .completed = completed };
    ctl_handle(&C, &e);
}

/* ---------- boot ---------- */

static void test_boot_clean_calibrated_restores_without_saving(void)
{
    boot_full(true, SPAN, true, 12000, false, 0);
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT32(12000, C.raw);
    fake_reset();
    blind_store_data_t b = { .span_valid = true, .closed_steps = SPAN,
                             .pos_known = true, .cur_steps = 12000 };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_POSITION));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_MOVE_FLAG));
}

static void test_boot_with_move_flag_drops_to_position_unknown(void)
{
    fake_reset();
    blind_store_data_t b = { .span_valid = true, .closed_steps = SPAN,
                             .pos_known = true, .cur_steps = 5000,
                             .move_in_progress = true };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_FALSE(C.pos.pos_known);
    TEST_ASSERT_TRUE(C.pos.span_valid);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, false, 0));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_MOVE_FLAG, false));
    TEST_ASSERT_EQUAL_INT32(0, C.raw);
}

static void test_boot_travel_time_sets_cruise(void)
{
    boot_full(true, 300000, true, 0, false, 30);   /* 30 s over 300 000 steps */
    TEST_ASSERT_EQUAL_UINT32(100, C.cfg.move.cruise_us);
    boot_full(true, 300000, true, 0, false, 0);    /* never set -> default */
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US, C.cfg.move.cruise_us);
}

/* ---------- LED base pattern ---------- */

static void test_led_pattern_priority_ladder(void)
{
    boot_cal(0);
    TEST_ASSERT_EQUAL(LED_OFF, ctl_led_pattern(&C));
    F.joined = false;
    TEST_ASSERT_EQUAL(LED_NO_NETWORK, ctl_led_pattern(&C));
    boot_uncal();
    F.joined = false;
    TEST_ASSERT_EQUAL(LED_NO_NETWORK, ctl_led_pattern(&C));  /* not UNCAL */
    F.joined = true;
    TEST_ASSERT_EQUAL(LED_UNCAL, ctl_led_pattern(&C));
    C.pos.cal = POS_CAL_WAIT_MARK2;
    TEST_ASSERT_EQUAL(LED_CAL_MARK2, ctl_led_pattern(&C));
    C.identifying = true;
    TEST_ASSERT_EQUAL(LED_IDENTIFY, ctl_led_pattern(&C));
}

/* ---------- keypad travel ---------- */

static void test_tap_down_calibrated_runs_full_travel(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, SPAN));
    TEST_ASSERT_TRUE(f_first(F_SAVE_MOVE_FLAG) < f_first(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT32(1, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_REPORT_TIMER_START));
    TEST_ASSERT_EQUAL_INT32(SPAN + 2400, F.last_cap);
    TEST_ASSERT_EQUAL_UINT32(150, F.last_cruise);
}

static void test_tap_at_limit_acks_without_moving(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_TRACE, TRC_MOVE_NOOP));
}

static void test_tap_while_moving_stops(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
}

static void test_tap_uncalibrated_flashes_error(void)
{
    boot_uncal();
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
}

static void test_motion_done_persists_then_clears_flag(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.n = 0;
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, true, SPAN));
    TEST_ASSERT_TRUE(f_first(F_SAVE_POSITION) < f_first(F_SAVE_MOVE_FLAG));
    TEST_ASSERT_EQUAL_INT32(0, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_REPORT_TIMER_STOP));
    TEST_ASSERT_EQUAL_INT32(100, f_rec(F_REPORT_LIFT).a);
    TEST_ASSERT_EQUAL_INT32(SPAN, C.raw);
}

static void test_position_save_failure_leaves_move_flag_set(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.n = 0;
    F.fail[F_SAVE_POSITION] = 0x105;
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_MOVE_FLAG));
}

static void test_dead_zone_move_acks(void)
{
    boot_cal(12000);                       /* 50 % */
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    F.n = 0;
    done(12050, false);                    /* still 50 % */
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
}

static void test_refused_move_flashes_error_and_clears_flag(void)
{
    boot_cal(0);
    F.start_err = 0x103;
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_MOVE_FLAG, true));
    TEST_ASSERT_EQUAL_INT32(0, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_REPORT_TIMER_START));
}

static void test_uncalibrated_jog_is_unbounded_and_uncapped(void)
{
    boot_uncal();
    kp(KP_EVT_HOLD_START, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, -2000000));
    TEST_ASSERT_EQUAL_INT32(2000001, F.last_cap);
    TEST_ASSERT_EQUAL_UINT32(300, F.last_cruise);
}

static void test_calibrated_jog_is_clamped_to_limits(void)
{
    boot_cal(12000);
    kp(KP_EVT_HOLD_START, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 12000, 0));
}

/* ---------- Zigbee ---------- */

static void test_zb_goto_idle_moves(void)
{
    boot_cal(0);
    zb_goto(50);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, 12000));
}

static void test_zb_goto_uncalibrated_is_ignored(void)
{
    boot_uncal();
    zb_goto(50);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
}

static void test_zb_goto_while_moving_parks_and_runs_after_done(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 8000, 6000));
}

static void test_zb_stop_discards_parked_goto(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    ev_type(APP_EVT_ZB_STOP);
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
}

static void test_keypad_input_discards_parked_goto(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    kp(KP_EVT_TAP, KEY_UP);          /* local input is the last writer */
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
}

static void test_report_tick_reports_live_lift_while_moving(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.steps = 6000;
    F.n = 0;
    ev_type(APP_EVT_REPORT_TICK);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_LIFT, 25));
}

static void test_travel_time_write_persists_and_reports_applied(void)
{
    boot_full(true, 300000, true, 0, false, 0);
    ev_secs(30);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_TRAVEL, 30));
    TEST_ASSERT_EQUAL_UINT32(100, C.cfg.move.cruise_us);
    TEST_ASSERT_EQUAL_INT32(30, f_rec(F_REPORT_TRAVEL).a);
}

/* Review Focus 4: a travel-time write mid-move applies to the NEXT move. */
static void test_travel_time_write_mid_move_applies_to_next_move(void)
{
    boot_full(true, 300000, true, 0, false, 30);    /* cruise 100 */
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_UINT32(100, F.last_cruise);
    ev_secs(60);                                     /* cruise 200 from now on */
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));   /* running move untouched */
    done(300000, true);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_UINT32(200, F.last_cruise);
}

static void test_identify_overrides_then_restores_led(void)
{
    boot_cal(0);
    ev_on(APP_EVT_IDENTIFY, true);
    TEST_ASSERT_EQUAL_INT32(LED_IDENTIFY, f_rec(F_LED_SET).a);
    ev_on(APP_EVT_IDENTIFY, false);
    TEST_ASSERT_EQUAL_INT32(LED_OFF, f_rec(F_LED_SET).a);
}

static void test_network_lost_shows_no_network(void)
{
    boot_cal(0);
    F.joined = false;
    ev_type(APP_EVT_ZB_NET_LOST);
    TEST_ASSERT_EQUAL_INT32(LED_NO_NETWORK, f_rec(F_LED_SET).a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_clean_calibrated_restores_without_saving);
    RUN_TEST(test_boot_with_move_flag_drops_to_position_unknown);
    RUN_TEST(test_boot_travel_time_sets_cruise);
    RUN_TEST(test_led_pattern_priority_ladder);
    RUN_TEST(test_tap_down_calibrated_runs_full_travel);
    RUN_TEST(test_tap_at_limit_acks_without_moving);
    RUN_TEST(test_tap_while_moving_stops);
    RUN_TEST(test_tap_uncalibrated_flashes_error);
    RUN_TEST(test_motion_done_persists_then_clears_flag);
    RUN_TEST(test_position_save_failure_leaves_move_flag_set);
    RUN_TEST(test_dead_zone_move_acks);
    RUN_TEST(test_refused_move_flashes_error_and_clears_flag);
    RUN_TEST(test_uncalibrated_jog_is_unbounded_and_uncapped);
    RUN_TEST(test_calibrated_jog_is_clamped_to_limits);
    RUN_TEST(test_zb_goto_idle_moves);
    RUN_TEST(test_zb_goto_uncalibrated_is_ignored);
    RUN_TEST(test_zb_goto_while_moving_parks_and_runs_after_done);
    RUN_TEST(test_zb_stop_discards_parked_goto);
    RUN_TEST(test_keypad_input_discards_parked_goto);
    RUN_TEST(test_report_tick_reports_live_lift_while_moving);
    RUN_TEST(test_travel_time_write_persists_and_reports_applied);
    RUN_TEST(test_travel_time_write_mid_move_applies_to_next_move);
    RUN_TEST(test_identify_overrides_then_restores_led);
    RUN_TEST(test_network_lost_shows_no_network);
    return UNITY_END();
}
```

- [ ] **Step 5: Add the suite to `platformio.ini`**

In `[env:native]`, append `    test_ctl` as a new line under `test_filter =` (after `test_trace_ring`).

- [ ] **Step 6: Run the suite**

Run: `pio test -e native -f test_ctl`
Expected: `24 test cases: 24 succeeded`. These are characterisation tests: they describe what the code in `main.c` does today. If one fails, the transcription in Step 2 differs from `main.c`; fix `ctl.c` to match `main.c`, not the test to match `ctl.c`, unless the test misreads `main.c`.

- [ ] **Step 7: Run everything**

Run: `pio test -e native`
Expected: `98 test cases: 98 succeeded`.

- [ ] **Step 8: Commit**

```bash
git add include/ctl.h src/ctl.c test/test_ctl platformio.ini
git commit -m "Add ctl: the dispatcher core behind ports, with motion tests

A transcription of main.c's dispatcher into a pure module whose side
effects all go through ctl_ports_t, so the decisions can be host-tested.
Nothing calls it yet. The characterisation tests pin today's behaviour
for boot, the LED ladder, keypad travel, jogs, Zigbee preemption, live
reports and travel time, so the rewiring that follows can prove it
changed nothing.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Characterisation tests for calibration, reversal and reset

No production code changes unless a test exposes a transcription error in `ctl.c` (fix `ctl.c` to match `main.c` in that case).

**Files:**
- Modify: `test/test_ctl/test_ctl.c`

**Interfaces:**
- Consumes: everything Task 2 produced.

- [ ] **Step 1: Add the tests** above `int main(void)` in `test/test_ctl/test_ctl.c`:

```c
/* ---------- calibration (spec §6) ---------- */

static void test_fn_long_from_calibrated_enters_full_calibration(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, C.pos.cal);
    TEST_ASSERT_EQUAL_INT32(5000, C.raw);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_CAL_TIMER_START));
    TEST_ASSERT_EQUAL_INT32(LED_CAL_MARK1, f_rec(F_LED_SET).a);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT32(false, f_rec(F_MOTION_ALLOWED).a);
}

static void test_fn_long_with_position_unknown_enters_rehome(void)
{
    boot_full(true, SPAN, false, 0, false, 0);
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_REHOME, C.pos.cal);
    TEST_ASSERT_EQUAL_INT32(LED_CAL_MARK1, f_rec(F_LED_SET).a);
}

static void test_fn_long_while_moving_does_not_enter(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_CAL_TIMER_START));
}

static void test_full_calibration_happy_path(void)
{
    boot_uncal();
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, 2000000));
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(1000, false);
    TEST_ASSERT_TRUE(C.cal_moved);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, false, 0));  /* first jog */
    kp(KP_EVT_TAP, KEY_FN);                                            /* mark 1 */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, C.pos.cal);
    TEST_ASSERT_EQUAL_INT32(LED_CAL_MARK2, f_rec(F_LED_SET).a);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(25000, false);
    F.n = 0;
    kp(KP_EVT_TAP, KEY_FN);                                            /* mark 2 */
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT32(SPAN, C.pos.closed_steps);
    TEST_ASSERT_EQUAL_INT32(SPAN, C.raw);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_SPAN, true, SPAN));
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, true, SPAN));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_CAL_TIMER_STOP));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_REPORT_TRAVEL));
}

static void test_mark2_too_short_is_rejected_and_keeps_waiting(void)
{
    boot_uncal();
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(1000, false);
    kp(KP_EVT_TAP, KEY_FN);                 /* mark 1 at 1000 */
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(3000, false);                      /* only 2000 below mark 1 */
    F.n = 0;
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, C.pos.cal);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_SPAN));
}

static void test_rehome_single_mark_restores_calibration(void)
{
    boot_full(true, SPAN, false, 0, false, 0);
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_UP);
    kp(KP_EVT_HOLD_END, KEY_UP);
    done(-500, false);
    F.n = 0;
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT32(0, C.pos.cur_steps);
    TEST_ASSERT_EQUAL_INT32(SPAN, C.pos.closed_steps);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, true, 0));
}

static void test_abort_without_jog_keeps_calibration(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    F.n = 0;
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_POSITION));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_CAL_TIMER_STOP));
}

static void test_abort_after_jog_drops_to_position_unknown(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(7000, false);
    F.n = 0;
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_FALSE(C.pos.pos_known);
    TEST_ASSERT_TRUE(C.pos.span_valid);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, false, 0));
}

static void test_timeout_when_idle_aborts(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    ev_type(APP_EVT_CAL_TIMEOUT);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
}

static void test_timeout_mid_jog_defers_abort_until_done(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    ev_type(APP_EVT_CAL_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_NOT_EQUAL(POS_CAL_NONE, C.pos.cal);
    done(7000, false);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_FALSE(C.pos.pos_known);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
}

static void test_timeout_outside_calibration_is_ignored(void)
{
    boot_cal(5000);
    ev_type(APP_EVT_CAL_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(0, F.n);
}

static void test_fn_tap_outside_calibration_acks(void)
{
    boot_cal(5000);
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
}

static void test_fn_tap_while_moving_stops(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
}

/* keypad-feedback check 8: brief Up/Down taps inside Calibration Mode are
 * silent — LED_ERROR there already means "mark rejected". */
static void test_updown_taps_inside_calibration_do_not_flash(void)
{
    boot_uncal();
    kp(KP_EVT_FN_LONG, KEY_FN);
    F.n = 0;
    kp(KP_EVT_TAP, KEY_UP);
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_LED_FLASH));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
}

/* Review Focus 3: Identify inside Calibration Mode returns to the mode's
 * pattern, not to OFF. */
static void test_identify_inside_calibration_returns_to_cal_pattern(void)
{
    boot_cal(0);
    kp(KP_EVT_FN_LONG, KEY_FN);
    ev_on(APP_EVT_IDENTIFY, true);
    TEST_ASSERT_EQUAL_INT32(LED_IDENTIFY, f_rec(F_LED_SET).a);
    ev_on(APP_EVT_IDENTIFY, false);
    TEST_ASSERT_EQUAL_INT32(LED_CAL_MARK1, f_rec(F_LED_SET).a);
}

/* ---------- Motor Reversed ---------- */

static void test_reverse_from_zigbee_wipes_calibration(void)
{
    boot_cal(5000);
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    TEST_ASSERT_TRUE(C.reversed);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_FALSE(C.pos.span_valid);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SET_REVERSED, true));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_REVERSED, true));
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_SPAN, false, 0));
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, false, 0));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_MODE, true));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
    TEST_ASSERT_EQUAL_INT32(0, C.raw);
}

static void test_reverse_to_same_value_does_nothing(void)
{
    boot_cal(5000);
    ev_on(APP_EVT_ZB_SET_REVERSED, false);
    TEST_ASSERT_EQUAL_INT(0, F.n);
}

static void test_reverse_while_moving_is_rejected(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.n = 0;
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    TEST_ASSERT_FALSE(C.reversed);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_MODE, false));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_REVERSED));
}

static void test_reverse_inside_calibration_exits_mode(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    F.n = 0;
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_CAL_TIMER_STOP));
}

/* ---------- factory reset ---------- */

static void test_factory_reset_when_idle(void)
{
    boot_cal(0);
    kp(KP_EVT_FACTORY_RESET, KEY_FN);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_FACTORY_RESET));
}

static void test_factory_reset_while_moving_is_refused(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    kp(KP_EVT_FACTORY_RESET, KEY_FN);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_FACTORY_RESET));
}
```

Add to `main()`, before `return UNITY_END();`:

```c
    RUN_TEST(test_fn_long_from_calibrated_enters_full_calibration);
    RUN_TEST(test_fn_long_with_position_unknown_enters_rehome);
    RUN_TEST(test_fn_long_while_moving_does_not_enter);
    RUN_TEST(test_full_calibration_happy_path);
    RUN_TEST(test_mark2_too_short_is_rejected_and_keeps_waiting);
    RUN_TEST(test_rehome_single_mark_restores_calibration);
    RUN_TEST(test_abort_without_jog_keeps_calibration);
    RUN_TEST(test_abort_after_jog_drops_to_position_unknown);
    RUN_TEST(test_timeout_when_idle_aborts);
    RUN_TEST(test_timeout_mid_jog_defers_abort_until_done);
    RUN_TEST(test_timeout_outside_calibration_is_ignored);
    RUN_TEST(test_fn_tap_outside_calibration_acks);
    RUN_TEST(test_fn_tap_while_moving_stops);
    RUN_TEST(test_updown_taps_inside_calibration_do_not_flash);
    RUN_TEST(test_identify_inside_calibration_returns_to_cal_pattern);
    RUN_TEST(test_reverse_from_zigbee_wipes_calibration);
    RUN_TEST(test_reverse_to_same_value_does_nothing);
    RUN_TEST(test_reverse_while_moving_is_rejected);
    RUN_TEST(test_reverse_inside_calibration_exits_mode);
    RUN_TEST(test_factory_reset_when_idle);
    RUN_TEST(test_factory_reset_while_moving_is_refused);
```

- [ ] **Step 2: Run**

Run: `pio test -e native -f test_ctl`
Expected: `45 test cases: 45 succeeded`.

- [ ] **Step 3: Commit**

```bash
git add test/test_ctl/test_ctl.c
git commit -m "ctl: characterisation tests for calibration, reversal and reset

Pins today's Calibration Mode flow (full, Re-home, marks, abort with and
without a jog, timeout idle and mid-jog), the silent Up/Down taps inside
the mode, Identify over the calibration pattern, the Motor Reversed wipe
and the factory-reset guard, ahead of rewiring main.c onto ctl.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Rewire `main.c` onto `ctl` (no behaviour change) + bench gate

**Files:**
- Modify: `src/main.c` (replace everything from `static QueueHandle_t s_queue;` to the end of the file)
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `ctl_*` (Task 2).
- Produces: `static ctl_t s_ctl`, `static const ctl_ports_t PORTS`, `static const ctl_config_t CFG` in `main.c`, used by Tasks 6, 8, 10.

- [ ] **Step 1: Add `ctl.c` to the build.** In `src/CMakeLists.txt`, add `    "ctl.c"` on the line after `    "main.c"`.

- [ ] **Step 2: Replace `src/main.c` from `static QueueHandle_t s_queue;` to end of file** with:

```c
static QueueHandle_t      s_queue;
static ctl_t              s_ctl;          /* owned by the dispatcher task */
static esp_timer_handle_t s_cal_timer;
static esp_timer_handle_t s_report_timer;

/* ---------- ports: ctl's side effects, bound to the real modules ---------- */

static void p_cal_timer_start(void)    { esp_timer_start_once(s_cal_timer, CAL_TIMEOUT_US); }
static void p_cal_timer_stop(void)     { esp_timer_stop(s_cal_timer); }
static void p_report_timer_start(void) { esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US); }
static void p_report_timer_stop(void)  { esp_timer_stop(s_report_timer); }
static void p_trace(uint16_t code, int32_t a, int32_t b) { TRACE(code, a, b); }
static void p_log(const char *msg)     { ESP_LOGW(TAG, "%s", msg); }

static const ctl_ports_t PORTS = {
    .motion_start        = motion_start,
    .motion_stop         = motion_stop,
    .motion_is_moving    = motion_is_moving,
    .motion_steps        = motion_current_steps,
    .motion_set_reversed = motion_set_reversed,
    .save_move_flag      = blind_store_set_move_flag,
    .save_span           = blind_store_save_span,
    .save_position       = blind_store_save_position,
    .save_reversed       = blind_store_save_motor_reversed,
    .save_travel_time    = blind_store_save_travel_time,
    .set_motion_allowed  = covering_set_motion_allowed,
    .set_operational     = covering_set_operational,
    .report_lift         = covering_report_lift,
    .report_mode         = covering_report_mode,
    .report_travel_time  = covering_report_travel_time,
    .net_joined          = zb_core_is_joined,
    .factory_reset       = zb_core_factory_reset,
    .led_set             = status_led_set,
    .led_flash           = status_led_flash,
    .cal_timer_start     = p_cal_timer_start,
    .cal_timer_stop      = p_cal_timer_stop,
    .report_timer_start  = p_report_timer_start,
    .report_timer_stop   = p_report_timer_stop,
    .trace               = p_trace,
    .log                 = p_log,
};

static const ctl_config_t CFG = {
    .move            = { RAMP_DEFAULT_CRUISE_US, START_US, ACCEL_STEPS },
    .jog             = { JOG_CRUISE_US, JOG_START_US, ACCEL_STEPS },
    .min_span_steps  = MIN_SPAN_STEPS,
    .hard_cap_margin = HARD_CAP_MARGIN,
    .jog_unbounded   = JOG_UNBOUNDED,
};

/* ---------- event producers (post only; ctl decides) ---------- */

static void cal_timeout_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_CAL_TIMEOUT };
    xQueueSend(s_queue, &ev, 0);
}

static void report_tick_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_REPORT_TICK };
    xQueueSend(s_queue, &ev, 0);
}

/* Zigbee stack context (lock held): enqueue only. */
static void zb_network_lost_cb(void)
{
    app_event_t ev = { .type = APP_EVT_ZB_NET_LOST };
    xQueueSend(s_queue, &ev, 0);
}

/* Zigbee stack context (lock held): enqueue only. */
static void zb_network_joined_cb(void)
{
    app_event_t ev = { .type = APP_EVT_ZB_NET_JOINED };
    xQueueSend(s_queue, &ev, 0);
}

static void dispatcher_task(void *pv)
{
    (void)pv;
    app_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        ctl_handle(&s_ctl, &ev);
    }
}

/* ---------- boot ---------- */

void app_main(void)
{
    trace_init();                              /* validate; clear only if cold */
    trace_dump();                              /* history from BEFORE this reset */
    TRACE(TRC_BOOT, esp_reset_reason(), 0);    /* then mark the new session */

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_queue = xQueueCreate(16, sizeof(app_event_t));
    configASSERT(s_queue);

    blind_store_data_t st;
    ESP_ERROR_CHECK(blind_store_init(&st));
    ctl_init(&s_ctl, &PORTS, &CFG, &st);   /* single task so far: safe */

    motion_pins_t pins = { .gpio_step = PIN_STEP, .gpio_dir = PIN_DIR, .gpio_en = PIN_EN };
    ESP_ERROR_CHECK(motion_init(&pins, s_queue));
    motion_set_reversed(s_ctl.reversed);
    ESP_ERROR_CHECK(status_led_init(PIN_LED_EXT));
    ESP_ERROR_CHECK(keypad_init(PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_FN, s_queue));

    const esp_timer_create_args_t cal_t = { .callback = cal_timeout_cb, .name = "cal_to" };
    ESP_ERROR_CHECK(esp_timer_create(&cal_t, &s_cal_timer));
    const esp_timer_create_args_t rep_t = { .callback = report_tick_cb, .name = "report" };
    ESP_ERROR_CHECK(esp_timer_create(&rep_t, &s_report_timer));

    covering_set_queue(s_queue);
    /* The lockout flag needs no Zigbee stack — set it truthfully NOW so a
     * calibrated device that joins slowly doesn't reject remote motion in
     * the meantime; attribute sync still happens after the join wait. */
    covering_set_motion_allowed(ctl_calibrated(&s_ctl));

    zb_core_cfg_t cfg = {
        .role              = ZB_CORE_ROLE_ROUTER,
        .endpoint          = APP_ENDPOINT,
        .app_device_id     = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
        .manufacturer_name = MANUF_NAME,
        .model_identifier  = MODEL_ID,
        .ota = {
            .manufacturer_code = OTA_MANUFACTURER_CODE,
            .image_type        = OTA_IMAGE_TYPE,
            .file_version      = FW_VERSION_U32,
            .version_str       = FW_VERSION_STR,
        },
        .build_clusters    = covering_build_clusters,
        .post_register     = covering_post_register,
        .on_joined         = zb_network_joined_cb,
        .on_network_lost   = zb_network_lost_cb,
        .action_handler    = covering_action_handler,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));

    ESP_LOGI(TAG, "starting %s (calibrated=%d reversed=%d)",
             FW_VERSION_STR, ctl_calibrated(&s_ctl), s_ctl.reversed);

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    /* Unchanged from before the extraction; Task 8 moves this into the
     * dispatcher. */
    status_led_set(ctl_calibrated(&s_ctl) ? LED_OFF : LED_UNCAL);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    covering_report_mode(s_ctl.reversed);
    covering_report_travel_time(ctl_achieved_travel_secs(&s_ctl));
    ctl_refresh_outputs(&s_ctl);

    /* Confirm a pending-verify OTA image once the app is up (join not
     * required) or the bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
```

Also add `#include "ctl.h"` to the include block (after `#include "trace.h"`), and replace the file's header comment with:

```c
/**
 * DFR-RollerBlinds — wiring. Every decision lives in ctl.c; this file owns
 * the GPIO map and constants, module init, the ports table that binds ctl to
 * the real modules, and the dispatcher loop that feeds ctl one event at a
 * time.
 */
```

The `ESP_LOGI` "starting" line moved above `xTaskCreate` so `app_main` reads `s_ctl` only while it is the sole task. `position.h` and `ramp.h` includes may stay (`RAMP_DEFAULT_CRUISE_US` is used in `CFG`).

- [ ] **Step 3: Build**

Run: `rm -rf .pio/build/seeed_xiao_esp32c6_zigbee && pio run -e seeed_xiao_esp32c6_zigbee`
Expected: `SUCCESS`, no warnings from `main.c` or `ctl.c`. A warning about incompatible function-pointer types in `PORTS` means a port signature differs from the real function; fix the port type in `ctl.h` to match.

- [ ] **Step 4: Host tests still pass**

Run: `pio test -e native`
Expected: `119 test cases: 119 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add src/main.c src/CMakeLists.txt
git commit -m "main: run the dispatcher through ctl

main.c keeps the wiring (GPIO map, constants, module init, queue, timers,
Zigbee config) and binds ctl's ports to the real modules. The dispatcher
loop now only hands each event to ctl_handle. Behaviour is unchanged,
including the post-join sync app_main still does itself; that moves into
the dispatcher in a later commit.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 6: HUMAN GATE — bench regression check on `bench2`**

Stop and ask the owner to flash `bench2` and run these checks (this build carries OTA version v0.0.0; that is expected for a local build):

1. `pio run -e seeed_xiao_esp32c6_zigbee -t upload --upload-port /dev/cu.usbmodem1101` (port may differ; B+R nudge if it does not enumerate).
2. Boot log shows `starting` and no panic; LED shows its normal pattern.
3. Tap Down / tap Up: full travels; tap mid-travel stops.
4. Hold Up / Down: jogs, stops on release.
5. z2m: set position 50 %, then 25 % mid-move (preemption runs 25 % after the stop).
6. Fn hold ~3 s: Calibration Mode slow pulse; Fn hold again: exits.
7. Tap Up at Open: ACK flash, no motion.

Do not start Task 5 until the owner reports all seven pass.

---

### Task 4b: Remove the GPIO glitch filter (added 2026-09-26 from the bench gate)

**Why:** during Task 4's bench gate on a fresh board (`bench3`), a key held while the motor jogged stayed "pressed" in firmware for 18 s after release (trace `KEY_ALIVE b=85`: GPIO23 read low) while a meter on the pin read **3.3 V**. Rebuilt with only `install_glitch_filters()` disabled, 30 of 30 jogs stopped on release and every tap flashed ERROR. The C6 flex glitch filter was latching its output. This is the runaway-jog / "stuck line that moves between keys" fault seen since August.

**Files:**
- Modify: `src/keypad.c`

- [ ] **Step 1:** In `src/keypad.c`, delete the `install_glitch_filters()` function and the comment block above it, delete its call in `keypad_init()`, and delete `#include "driver/gpio_filter.h"`. Where the call was, put:

```c
    /* No GPIO glitch filter on the key pins. The C6's flex glitch filter was
     * enabled here until 2026-09-26, when a bench A/B on a fresh board proved
     * it latching a released key "pressed" while the motor ran: the pin
     * measured 3.3 V, the firmware read it low for 18 s, and the jog never
     * stopped. With only the filter removed, 30 of 30 jogs stopped on
     * release. The RC front end and the key_filter integrator reject noise. */
```

- [ ] **Step 2:** `grep -n "glitch" src/keypad.c` shows only the new comment.
- [ ] **Step 3:** `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native` → firmware `SUCCESS`; `119 test cases: 119 succeeded`.
- [ ] **Step 4:** Commit:

```bash
git add src/keypad.c
git commit -m "keypad: remove the GPIO glitch filter — it latched keys pressed

On a fresh bench board a key held while the motor jogged stayed pressed
in firmware for 18 s after release, with the pin measuring 3.3 V: the
C6 flex glitch filter's output was stuck low, so the jog never stopped
and only another key's tap could stop it. With only the filter removed,
30 of 30 jogs stopped on release. This is the runaway jog and the stuck
line that moved between keys since August, previously blamed on cables
and membranes. The RC front end and the integrator reject noise.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

## Phase 2: fixes in the core, test-first

### Task 5: Dispatcher owns "move in progress" (S1, S1b)

**Files:**
- Modify: `include/ctl.h`, `src/ctl.c`, `test/test_ctl/fake_ports.h`, `test/test_ctl/test_ctl.c`, `src/main.c`

**Interfaces:**
- Produces: `ctl_t.move_active` (bool). Removes `ctl_ports_t.motion_is_moving`. `start_move()` now returns `bool` (true = motion started) for Task 6.

- [ ] **Step 1: Write the failing tests** (add above `main()`, register in `main()`):

```c
/* ---------- S1: the end-of-move gap ----------
 * isr_finished(): the ISR has ended the move and cleared its own moving
 * flag, but the dispatcher has not yet handled MOTION_DONE. */
static void isr_finished(void) { F.moving = false; }

static void test_tap_in_end_of_move_gap_is_a_stop_not_a_new_move(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);                 /* 0 -> 24000 */
    isr_finished();
    kp(KP_EVT_TAP, KEY_DOWN);                 /* would restart from stale raw 0 */
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    done(SPAN, true);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, SPAN, 0));
}

static void test_goto_in_end_of_move_gap_is_parked_and_runs_from_done(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    isr_finished();
    zb_goto(50);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, SPAN, 12000));
}

/* Review Focus 2 */
static void test_zb_stop_in_gap_discards_parked_goto(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    isr_finished();
    zb_goto(50);
    ev_type(APP_EVT_ZB_STOP);
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
}

static void test_cal_timeout_in_gap_defers_abort_and_distrusts_position(void)
{
    boot_cal(5000);
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    isr_finished();
    ev_type(APP_EVT_CAL_TIMEOUT);             /* would abort before the jog counted */
    done(9000, false);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, C.pos.cal);
    TEST_ASSERT_FALSE(C.pos.pos_known);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT(0, f_count_ab(F_SAVE_POSITION, true, 9000));
}

static void test_move_flag_not_cleared_by_a_stale_done(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    isr_finished();
    zb_goto(50);
    done(SPAN, true);                          /* runs the parked goto */
    TEST_ASSERT_EQUAL_INT32(1, f_rec(F_SAVE_MOVE_FLAG).a);   /* set for the new move */
    TEST_ASSERT_TRUE(C.move_active);
}
```

```c
    RUN_TEST(test_tap_in_end_of_move_gap_is_a_stop_not_a_new_move);
    RUN_TEST(test_goto_in_end_of_move_gap_is_parked_and_runs_from_done);
    RUN_TEST(test_zb_stop_in_gap_discards_parked_goto);
    RUN_TEST(test_cal_timeout_in_gap_defers_abort_and_distrusts_position);
    RUN_TEST(test_move_flag_not_cleared_by_a_stale_done);
```

- [ ] **Step 2: Run to verify they fail**

Run: `pio test -e native -f test_ctl`
Expected: compile error `no member named 'move_active'` in the last test. Temporarily comment out `test_move_flag_not_cleared_by_a_stale_done` and re-run: the first four FAIL (e.g. `Expected 1 Was 2` on `F_MOTION_START`). Restore the test.

- [ ] **Step 3: Implement**

In `include/ctl.h`: delete the `motion_is_moving` line from `ctl_ports_t`, and add to `ctl_t` after `move_start_pct`:

```c
    bool     move_active;      /* set when motion_start succeeds, cleared only
                                * when this core handles MOTION_DONE. The ISR's
                                * own flag clears earlier, at the last step, so
                                * it cannot answer "may I start a move?" */
```

In `src/ctl.c`:

```c
static bool moving(const ctl_t *c) { return c->move_active; }
```

Change `start_move` to return whether motion started:

```c
static bool start_move(ctl_t *c, int32_t target, const motion_profile_t *prof)
{
    if (target == c->raw) {
        c->io->trace(TRC_MOVE_NOOP, target, c->pos.closed_steps);
        c->io->led_flash(LED_ACK);   /* heard you; already there */
        ctl_refresh_outputs(c);      /* keep reports honest, no NVS churn */
        return false;
    }
    int32_t cap = hard_cap(c);       /* hoisted so a refusal can record it */
    c->move_start_pct = position_lift_pct(&c->pos);
    c->io->trace(TRC_MOVE_START, c->raw, target);
    c->io->save_move_flag(true);
    ctl_err_t err = c->io->motion_start(c->raw, target, prof, cap);
    if (err != CTL_OK) {
        c->io->save_move_flag(false);
        c->io->trace(TRC_MOVE_REFUSED, err, cap);
        c->io->led_flash(LED_ERROR);  /* refusing — needs attention */
        c->io->log("move refused");
        return false;
    }
    c->move_active = true;
    c->io->report_timer_start();
    return true;
}
```

`goto_pct` and `jog` call it as `(void)start_move(...)` for now. In `on_motion_done`, make the first statement:

```c
    c->move_active = false;   /* the only place it clears */
```

In `test/test_ctl/fake_ports.h`: delete `p_motion_is_moving` and its `.motion_is_moving = …` initialiser. Keep `F.moving` (the helpers still use it to model the ISR).

In `src/main.c`: delete the `.motion_is_moving = motion_is_moving,` line from `PORTS`.

- [ ] **Step 4: Run**

Run: `pio test -e native && pio run -e seeed_xiao_esp32c6_zigbee`
Expected: `124 test cases: 124 succeeded`; firmware `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add include/ctl.h src/ctl.c src/main.c test/test_ctl
git commit -m "ctl: the dispatcher owns \"move in progress\"

The ISR clears its moving flag at the last step, but the dispatcher only
learns the final position when it handles MOTION_DONE. Anything handled
in between saw an idle motor and a stale position: a tap restarted a
move from the old origin and could drive past Closed, a goto did the
same, and a calibration timeout aborted before the jog was counted, so
the jog's end was later saved as a trusted position. ctl now tracks the
move itself, set when motion starts and cleared only on MOTION_DONE.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: The hold dead-man lives in the dispatcher (S4, T1)

**Files:**
- Modify: `include/ctl.h`, `src/ctl.c`, `include/trace.h`, `include/keypad.h`, `src/keypad.c`, `src/main.c`, `test/test_ctl/fake_ports.h`, `test/test_ctl/test_ctl.c`

**Interfaces:**
- Consumes: `start_move()` returning `bool`, `move_active` (Task 5).
- Produces: `ctl_ports_t.key_held` (`bool (*key_held)(key_id_t key)`); `bool keypad_key_held(key_id_t key);` in `keypad.h`; `TRC_DEADMAN_STOP` in `trace.h`; `ctl_t.jog_active`, `ctl_t.jog_key`, `ctl_t.jog_stop_sent`.

- [ ] **Step 1: Add the port to the fake first** (so the tests compile). In `test/test_ctl/fake_ports.h` add `bool held[KEY_COUNT];` to `F`, the port

```c
static bool      p_key_held(key_id_t k)         { return F.held[k]; }
```

the initialiser `.key_held = p_key_held,`, and in `fake_reset()` after `F.joined = true;`:

```c
    for (int k = 0; k < KEY_COUNT; k++) F.held[k] = true;   /* a HOLD_START implies held */
```

- [ ] **Step 2: Write the failing tests**

```c
/* ---------- S4: dead-man ---------- */

static void test_lost_hold_end_is_caught_on_the_next_event(void)
{
    boot_cal(12000);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    F.held[KEY_DOWN] = false;                 /* released; HOLD_END was dropped */
    ev_type(APP_EVT_REPORT_TICK);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_TRACE, TRC_DEADMAN_STOP));
}

static void test_hold_start_with_key_already_released_stops_in_same_call(void)
{
    boot_uncal();
    F.held[KEY_UP] = false;                   /* HOLD_END dropped before HOLD_START ran */
    kp(KP_EVT_HOLD_START, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
}

static void test_normal_hold_end_stops_once_without_deadman(void)
{
    boot_cal(12000);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    F.held[KEY_DOWN] = false;
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    ev_type(APP_EVT_REPORT_TICK);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_EQUAL_INT(0, f_count_a(F_TRACE, TRC_DEADMAN_STOP));
}

/* Review Focus 1: a Zigbee goto that preempts a jog is not the jog's to stop. */
static void test_deadman_does_not_stop_a_zigbee_move_after_a_jog(void)
{
    boot_cal(12000);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    zb_goto(25);                              /* parks, stops the jog */
    done(13000, false);                       /* runs the goto */
    int stops = f_count(F_MOTION_STOP);
    F.held[KEY_DOWN] = false;                 /* operator lets go of the key */
    ev_type(APP_EVT_REPORT_TICK);
    TEST_ASSERT_EQUAL_INT(stops, f_count(F_MOTION_STOP));
}
```

Register the four in `main()`.

- [ ] **Step 3: Run to verify they fail**

Run: `pio test -e native -f test_ctl`
Expected: compile error `TRC_DEADMAN_STOP undeclared`. That is the red state for this task.

- [ ] **Step 4: Implement**

`include/trace.h`: append after `TRC_KEY_ALIVE,`:

```c
    /* The dispatcher stopped a jog because its key reads released while no
     * HOLD_END arrived (dropped on a full queue).
     *   a = key_id_t   b = -                                                  */
    TRC_DEADMAN_STOP,
```

`include/ctl.h`: add to `ctl_ports_t` under `/* operator */`:

```c
    bool (*key_held)(key_id_t key);   /* debounced level, safe to poll */
```

and to `ctl_t` after `move_active`:

```c
    bool     jog_active;       /* the current move is a keypad jog */
    key_id_t jog_key;          /* ...held on this key */
    bool     jog_stop_sent;    /* its stop has been requested */
```

`src/ctl.c`: replace `jog()` with

```c
static void jog(ctl_t *c, key_id_t key)
{
    bool up = (key == KEY_UP);
    int32_t target;
    if (ctl_calibrated(c)) {
        target = up ? 0 : c->pos.closed_steps;              /* clamped jog */
    } else {
        target = up ? c->raw - c->cfg.jog_unbounded
                    : c->raw + c->cfg.jog_unbounded;
    }
    if (start_move(c, target, &c->cfg.jog)) {
        c->jog_active    = true;
        c->jog_key       = key;
        c->jog_stop_sent = false;
    }
}

/* Hold-to-jog must stop on release even if HOLD_END never arrives (a full
 * queue drops it). Checked after every event: a full queue means the
 * dispatcher is behind, so it has events to process and catches the release
 * within one of them; the 1 s report tick is the backstop. */
static void deadman(ctl_t *c)
{
    if (c->jog_active && !c->jog_stop_sent && !c->io->key_held(c->jog_key)) {
        c->jog_stop_sent = true;
        c->io->trace(TRC_DEADMAN_STOP, c->jog_key, 0);
        c->io->motion_stop();
    }
}
```

In `handle_keypad`: `case KP_EVT_HOLD_START: if (!moving(c)) jog(c, e.key); break;` and

```c
    case KP_EVT_HOLD_END:
        if (c->jog_active) c->jog_stop_sent = true;
        c->io->motion_stop();
        break;
```

In `zb_goto_request`'s moving branch, and in `APP_EVT_ZB_STOP`, before `motion_stop()`, add `c->jog_stop_sent = true;` only when `c->jog_active` (the jog's stop is already requested; a later release must not trace a dead-man stop):

```c
        if (c->jog_active) c->jog_stop_sent = true;
```

In `on_motion_done`, after `c->move_active = false;`:

```c
    c->jog_active = false;
```

At the end of `ctl_handle` (after the `switch`), add:

```c
    deadman(c);
```

`include/keypad.h`: add `#include "keypad_logic.h"` and

```c
/* Debounced state of one key (true = pressed). One aligned int written by the
 * 5 ms poll; safe to read from the dispatcher task. */
bool keypad_key_held(key_id_t key);
```

`src/keypad.c`: add

```c
bool keypad_key_held(key_id_t key)
{
    return key_filter_level(&s_filt[key]) == 0;   /* active-low */
}
```

and replace `post_kp()` with

```c
static void post_kp(kp_event_t e)
{
    if (e.type == KP_EVT_NONE) return;
    app_event_t ev = { .type = APP_EVT_KEYPAD, .kp = e };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        /* Dropped. A dropped HOLD_END is caught by the dispatcher's dead-man
         * check against keypad_key_held(); this task never touches motion. */
        TRACE(TRC_QUEUE_FULL, e.type, 0);
        ESP_LOGE(TAG, "queue full, dropped kp event type=%d", e.type);
    }
}
```

Delete `#include "motion.h"` from `src/keypad.c`.

`src/main.c`: add `.key_held = keypad_key_held,` to `PORTS` after `.led_flash`.

- [ ] **Step 5: Run**

Run: `pio test -e native && pio run -e seeed_xiao_esp32c6_zigbee`
Expected: `128 test cases: 128 succeeded`; firmware `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add include/ctl.h src/ctl.c include/trace.h include/keypad.h src/keypad.c src/main.c test/test_ctl
git commit -m "Move the hold dead-man into the dispatcher

On a full queue, post_kp() used to call motion_stop() from the keypad's
timer task. That broke the rule that only the dispatcher decides, raced
the dispatcher's own motion_stop(), and did nothing when HOLD_START was
still queued, so that jog then ran with no stop at all. The dispatcher
now checks, after every event, whether a running jog's key still reads
held, and stops it if not. post_kp() only records the drop.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: NVS failure policy and the Motor Reversed save order (S3, S8, T8)

**Files:**
- Modify: `src/ctl.c`, `test/test_ctl/test_ctl.c`

**Interfaces:**
- Consumes: `F.fail[]` injection (Task 2).

- [ ] **Step 1: Write the failing tests**

```c
/* ---------- S3 / S8: persistence ---------- */

static void test_move_refused_when_move_flag_cannot_be_saved(void)
{
    boot_cal(0);
    F.fail[F_SAVE_MOVE_FLAG] = 0x105;
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_FALSE(C.move_active);
}

static void test_reverse_invalidates_calibration_before_saving_direction(void)
{
    boot_cal(5000);
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    int span = f_first(F_SAVE_SPAN), pos = f_first(F_SAVE_POSITION);
    int rev  = f_first(F_SAVE_REVERSED);
    TEST_ASSERT_TRUE(span >= 0 && pos >= 0 && rev >= 0);
    TEST_ASSERT_TRUE(span < rev);
    TEST_ASSERT_TRUE(pos < rev);
}

static void test_reverse_refused_when_invalidation_fails(void)
{
    boot_cal(5000);
    F.fail[F_SAVE_SPAN] = 0x105;
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    TEST_ASSERT_FALSE(C.reversed);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_REVERSED));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SET_REVERSED));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT32(false, f_rec(F_REPORT_MODE).a);   /* z2m told the truth */
}

static void test_reverse_keeps_old_direction_when_direction_save_fails(void)
{
    boot_cal(5000);
    F.fail[F_SAVE_REVERSED] = 0x105;
    ev_on(APP_EVT_ZB_SET_REVERSED, true);
    TEST_ASSERT_FALSE(C.reversed);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SET_REVERSED));
    TEST_ASSERT_FALSE(ctl_calibrated(&C));          /* wiped either way */
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT32(false, f_rec(F_REPORT_MODE).a);
}

static void calibrate_to_mark2(void)
{
    boot_uncal();
    kp(KP_EVT_FN_LONG, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(1000, false);
    kp(KP_EVT_TAP, KEY_FN);
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    done(25000, false);
    F.n = 0;
}

static void test_mark2_span_save_failure_keeps_waiting_for_mark2(void)
{
    calibrate_to_mark2();
    F.fail[F_SAVE_SPAN] = 0x105;
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, C.pos.cal);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_CAL_TIMER_STOP));
}

/* Review Focus 5: span saved, position save failed. What flash now holds
 * must boot as "needs Re-home", never Calibrated. */
static void test_mark2_position_save_failure_boots_needing_rehome(void)
{
    calibrate_to_mark2();
    F.fail[F_SAVE_POSITION] = 0x105;
    kp(KP_EVT_TAP, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, C.pos.cal);
    frec_t span = f_rec(F_SAVE_SPAN);
    TEST_ASSERT_EQUAL_INT32(true, span.a);
    /* flash: new span valid; position last saved untrusted by the first jog */
    boot_full(true, span.b, false, 0, false, 0);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    kp(KP_EVT_FN_LONG, KEY_FN);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_REHOME, C.pos.cal);
}
```

Register the six in `main()`.

- [ ] **Step 2: Run to verify they fail**

Run: `pio test -e native -f test_ctl`
Expected: all six FAIL (e.g. the first with `Expected 0 Was 1` on `F_MOTION_START`; the order test because `save_reversed` comes first).

- [ ] **Step 3: Implement**

In `start_move`, replace `c->io->save_move_flag(true);` with:

```c
    /* Without the flag a power cut mid-move would boot Calibrated with a
     * wrong position — exactly what the flag exists to prevent. */
    if (c->io->save_move_flag(true) != CTL_OK) {
        c->io->trace(TRC_MOVE_REFUSED, -1, cap);
        c->io->led_flash(LED_ERROR);
        c->io->log("move refused: move flag not saved");
        return false;
    }
```

Replace `toggle_reversed` with:

```c
/* Invalidate before flipping: if power is lost between the saves, flash must
 * hold "uncalibrated", never the new direction with the old calibration —
 * that would drive past the limits with full confidence (spec §6). Flash
 * never trusts more than RAM. */
static void toggle_reversed(ctl_t *c)
{
    if (moving(c)) return;
    if (c->io->save_span(false, 0) != CTL_OK ||
        c->io->save_position(false, 0) != CTL_OK) {
        c->io->led_flash(LED_ERROR);
        c->io->log("motor_reversed refused: calibration could not be invalidated");
        c->io->report_mode(c->reversed);    /* z2m sees the unchanged truth */
        return;
    }
    bool was_cal = in_cal(c);
    position_wipe(&c->pos);                 /* direction changes -> steps meaningless */
    if (was_cal) c->io->cal_timer_stop();
    c->raw = 0;
    bool want = !c->reversed;
    if (c->io->save_reversed(want) == CTL_OK) {
        c->reversed = want;
        c->io->motion_set_reversed(want);
        c->io->led_flash(LED_ACK);
    } else {
        /* calibration is gone either way; the old direction is the safe one */
        c->io->led_flash(LED_ERROR);
        c->io->log("motor_reversed not saved: direction unchanged");
    }
    c->io->report_mode(c->reversed);
    apply_travel_time(c);   /* span was wiped: fall back until recalibrated */
    ctl_refresh_outputs(c);
}
```

In `handle_mark`, replace the body from `if (position_cal_mark(` to the end of that `if/else` with:

```c
    position_t before = c->pos;
    if (position_cal_mark(&c->pos, c->raw, c->cfg.min_span_steps)) {
        if (!in_cal(c)) {                               /* calibration finished */
            /* Span first: if the position save then fails, flash holds the
             * new span with the position the session's first jog already
             * saved as untrusted — a Re-home, never a wrong Calibrated. */
            if (c->io->save_span(c->pos.span_valid, c->pos.closed_steps) != CTL_OK ||
                c->io->save_position(c->pos.pos_known, c->pos.cur_steps) != CTL_OK) {
                c->pos = before;                        /* treat as a rejected mark */
                c->io->led_flash(LED_ERROR);
                c->io->log("calibration not saved: mark rejected, try again");
                ctl_refresh_outputs(c);
                return;
            }
            c->io->cal_timer_stop();
            c->raw = c->pos.cur_steps;                  /* re-anchor raw frame */
            apply_travel_time(c);   /* new span -> same duration, new interval */
        }
        c->io->led_flash(LED_ACK);
    } else {
        c->io->led_flash(LED_ERROR);                    /* stay awaiting mark 2 */
    }
    ctl_refresh_outputs(c);
```

In `cal_abort`, replace `c->io->save_position(false, 0);` with:

```c
        if (c->io->save_position(false, 0) != CTL_OK) {
            /* the session's first jog already saved the position untrusted,
             * so flash is on the safe side; record it and carry on */
            c->io->log("abort: position save failed (already untrusted on flash)");
        }
```

- [ ] **Step 4: Run**

Run: `pio test -e native`
Expected: `134 test cases: 134 succeeded`. `test_full_calibration_happy_path` must still pass: it asserts `F_CAL_TIMER_STOP` count 1, which now comes after the saves.

- [ ] **Step 5: Commit**

```bash
git add src/ctl.c test/test_ctl/test_ctl.c
git commit -m "ctl: a failed NVS write refuses the action; flash never trusts more than RAM

Toggling Motor Reversed saved the new direction before invalidating span
and position, unchecked, so a power cut between the saves booted with
the new direction and the old calibration, still claiming Calibrated.
The invalidation now comes first and a failure refuses the toggle; a
failed direction save keeps the old direction. A move whose in-progress
flag can't be saved is refused, and a mark 2 that can't be saved is
treated as a rejected mark. Each failure flashes LED_ERROR.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Boot sync in the dispatcher; NO_NETWORK from boot (S6, S9, T2)

**Files:**
- Modify: `include/app_event.h`, `include/ctl.h`, `src/ctl.c`, `src/main.c`, `test/test_ctl/test_ctl.c`

**Interfaces:**
- Produces: `APP_EVT_BOOT_SYNC` (last value of `app_event_type_t`). `ctl_refresh_outputs()` becomes `static` (removed from `ctl.h`).

- [ ] **Step 1: Write the failing test**

```c
/* ---------- S6 / S9: boot ---------- */

static void test_boot_sync_reports_mode_travel_and_outputs(void)
{
    boot_full(true, 300000, true, 0, false, 30);
    ev_type(APP_EVT_BOOT_SYNC);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_MODE, false));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_TRAVEL, 30));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_MOTION_ALLOWED, true));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_LED_SET));
}

static void test_boot_led_for_calibrated_unjoined_unit_is_no_network(void)
{
    boot_cal(0);
    F.joined = false;
    TEST_ASSERT_EQUAL(LED_NO_NETWORK, ctl_led_pattern(&C));
}
```

Register both.

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_ctl`
Expected: compile error `APP_EVT_BOOT_SYNC undeclared`.

- [ ] **Step 3: Implement**

`include/app_event.h`: append after `APP_EVT_IDENTIFY,`:

```c
    APP_EVT_BOOT_SYNC,       /* app_main: join wait over (joined or timed out) —
                              * push Mode, travel time and all outputs */
```

`src/ctl.c`: make `ctl_refresh_outputs` a `static void refresh_outputs(ctl_t *c)` (rename every call site), and add to `ctl_handle`'s switch:

```c
    case APP_EVT_BOOT_SYNC:
        c->io->report_mode(c->reversed);
        c->io->report_travel_time(ctl_achieved_travel_secs(c));
        refresh_outputs(c);
        break;
```

Since `refresh_outputs` is now defined before use only if it stays above its callers; keep it where `ctl_refresh_outputs` was.

`include/ctl.h`: delete the `ctl_refresh_outputs` declaration and its comment.

`src/main.c`, in `app_main`: delete from `/* Unchanged from before the extraction` through `ctl_refresh_outputs(&s_ctl);` and replace the tail after the `ESP_LOGI(TAG, "starting …` line with:

```c
    /* Still the only task touching ctl state: pick the LED now, so a unit
     * that has not joined shows LED_NO_NETWORK from the first moment. */
    status_led_set(ctl_led_pattern(&s_ctl));

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    /* From here on app_main touches no ctl state: the dispatcher owns it.
     * The post-join sync runs there, as an event. */
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    app_event_t sync = { .type = APP_EVT_BOOT_SYNC };
    xQueueSend(s_queue, &sync, portMAX_DELAY);

    /* Confirm a pending-verify OTA image once the app is up (join not
     * required) or the bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
```

- [ ] **Step 4: Run**

Run: `pio test -e native && pio run -e seeed_xiao_esp32c6_zigbee`
Expected: `136 test cases: 136 succeeded`; firmware `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add include/app_event.h include/ctl.h src/ctl.c src/main.c test/test_ctl/test_ctl.c
git commit -m "Boot: LED before the dispatcher starts, sync after it through an event

app_main used to set OFF or UNCAL, never NO_NETWORK, then after a join
wait of up to 60 s pushed reports and outputs from its own task while
the dispatcher was already running. A calibrated unit that hadn't joined
showed a dark LED for that minute, and Calibration Mode entered during
the wait could be overwritten. The LED is now chosen from ctl while
app_main is still the only task, and the post-join sync is an
APP_EVT_BOOT_SYNC handled by the dispatcher.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Refused gestures flash LED_ERROR (S11)

**Files:**
- Modify: `src/ctl.c`, `test/test_ctl/test_ctl.c`

- [ ] **Step 1: Change the two existing tests to the new behaviour and run them red**

In `test_fn_long_while_moving_does_not_enter`, append:

```c
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
```

In `test_factory_reset_while_moving_is_refused`, append the same line.

Run: `pio test -e native -f test_ctl`
Expected: both FAIL with `Expected 1 Was 0`.

- [ ] **Step 2: Implement**

In `enter_or_exit_cal`, replace `if (moving(c)) return;` with:

```c
    if (moving(c)) {                          /* only from standstill */
        c->io->led_flash(LED_ERROR);          /* heard you; refusing */
        return;
    }
```

In `handle_keypad`'s `KP_EVT_FACTORY_RESET` case, inside `if (moving(c)) {`, add `c->io->led_flash(LED_ERROR);` before the log call.

- [ ] **Step 3: Run**

Run: `pio test -e native`
Expected: `136 test cases: 136 succeeded`.

- [ ] **Step 4: Commit**

```bash
git add src/ctl.c test/test_ctl/test_ctl.c
git commit -m "ctl: say so when refusing a factory reset or Fn long-press mid-move

Both were refused silently. Someone holding three keys for five seconds
to recover a unit then concludes the reset is broken, the worst thing
to believe about a recovery gesture. The keypad-feedback rule already
says LED_ERROR means \"heard you, refusing\"; these two now follow it.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

## Phase 3: outside the core

### Task 10: `MOTION_DONE` gets its own queue in a queue set (S1c)

**Files:**
- Modify: `src/motion.c`, `include/motion.h`, `include/trace.h`, `src/main.c`

**Interfaces:**
- Produces: `uint32_t motion_done_post_failures(void);` in `motion.h`; `TRC_DONE_POST_FAILED` in `trace.h`. `motion_init()` keeps its signature; `main.c` passes the done queue.

- [ ] **Step 1: `include/trace.h`** — append after `TRC_DEADMAN_STOP,`:

```c
    /* The motion ISR could not post MOTION_DONE. Cannot happen while the
     * dispatcher keeps at most one move outstanding (the done queue has one
     * slot); recorded so a broken invariant is visible, not silent.
     *   a = failures so far   b = -                                         */
    TRC_DONE_POST_FAILED,
```

- [ ] **Step 2: `src/motion.c`** — add after the `s_stop_at_idx` declaration:

```c
static DRAM_ATTR volatile uint32_t s_done_post_failed;   /* see TRC_DONE_POST_FAILED */
```

In `timer_cb`, replace `xQueueSendFromISR(s_queue, &ev, &hpw);` with:

```c
        /* The ISR runs from IRAM and may not call the (flash-resident) trace;
         * count here, and the dispatcher reports it. */
        if (xQueueSendFromISR(s_queue, &ev, &hpw) != pdTRUE) s_done_post_failed++;
```

In `motion_start`'s `delta == 0` branch, replace `xQueueSend(s_queue, &ev, 0);` with:

```c
        if (xQueueSend(s_queue, &ev, 0) != pdTRUE) s_done_post_failed++;
```

Append:

```c
uint32_t motion_done_post_failures(void) { return s_done_post_failed; }
```

`include/motion.h`: change the `motion_init` comment to

```c
/* Configure GPIOs (EN̅ high), create the GPTimer. done-events are posted to
 * done_q as APP_EVT_MOTION_DONE {steps=final absolute position, completed}.
 * done_q should be a dedicated one-slot queue: the dispatcher never starts a
 * move while one is outstanding, so the post cannot fail. */
esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t done_q);
```

and add

```c
/* Times a MOTION_DONE post failed (should stay 0). Read by the dispatcher. */
uint32_t motion_done_post_failures(void);
```

- [ ] **Step 3: `src/main.c`** — add after `static QueueHandle_t s_queue;`:

```c
static QueueHandle_t      s_done_q;       /* MOTION_DONE only, one slot */
static QueueSetHandle_t   s_qset;         /* s_queue + s_done_q */
```

Replace `dispatcher_task` with:

```c
static void dispatcher_task(void *pv)
{
    (void)pv;
    app_event_t ev;
    uint32_t done_fail_seen = 0;
    for (;;) {
        /* The set hands back queues in the order their items arrived, so
         * event order is unchanged by MOTION_DONE having its own queue. */
        QueueSetMemberHandle_t q = xQueueSelectFromSet(s_qset, portMAX_DELAY);
        if (q == NULL || xQueueReceive(q, &ev, 0) != pdTRUE) continue;
        ctl_handle(&s_ctl, &ev);
        uint32_t f = motion_done_post_failures();
        if (f != done_fail_seen) {
            done_fail_seen = f;
            TRACE(TRC_DONE_POST_FAILED, f, 0);
            ESP_LOGE(TAG, "MOTION_DONE post failed (%lu)", (unsigned long)f);
        }
    }
}
```

In `app_main`, after `configASSERT(s_queue);` add:

```c
    s_done_q = xQueueCreate(1, sizeof(app_event_t));
    configASSERT(s_done_q);
    s_qset = xQueueCreateSet(16 + 1);        /* sum of the member lengths */
    configASSERT(s_qset);
    xQueueAddToSet(s_queue, s_qset);         /* both empty here, as required */
    xQueueAddToSet(s_done_q, s_qset);
```

and change `motion_init(&pins, s_queue)` to `motion_init(&pins, s_done_q)`.

`include/app_event.h`: in the top comment, change "the motion ISR (MOTION_DONE)" to "the motion ISR (MOTION_DONE, on its own one-slot queue)".

- [ ] **Step 4: Build**

Run: `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: firmware `SUCCESS`; `136 test cases: 136 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add src/motion.c include/motion.h include/trace.h include/app_event.h src/main.c
git commit -m "Give MOTION_DONE its own one-slot queue in a queue set

The ISR posted MOTION_DONE into the shared 16-deep queue and ignored a
failure, so a full queue lost it: the move flag stayed set, the position
never updated, and with ctl now waiting on MOTION_DONE the unit would
refuse every move until reboot. ctl never starts a move while one is
outstanding, so a dedicated one-slot queue is always empty when the ISR
posts. The dispatcher waits on a queue set holding both queues, which
preserves arrival order. A failed post is still counted and traced.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 11: One ramp implementation, in IRAM (T6)

**Files:**
- Modify: `include/ramp.h`, `src/ramp.c`, `src/motion.c`, `test/test_ramp/test_ramp.c`

**Interfaces:**
- Produces: `int32_t ramp_stop_index(const ramp_plan_t *r, int32_t idx);`, `uint32_t ramp_interval_us_stopping(const ramp_plan_t *r, int32_t idx, int32_t stop_at);`, macro `RAMP_HOT`.

- [ ] **Step 1: Write the failing tests** in `test/test_ramp/test_ramp.c` (above `main`, registered in `main`):

```c
/* ---------- stop path (used by the motion ISR) ---------- */

static void test_stop_index_decelerates_as_far_as_it_accelerated(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_INT32(1, ramp_stop_index(&r, 0));       /* at rest: one step */
    TEST_ASSERT_EQUAL_INT32(800, ramp_stop_index(&r, 400));   /* mid-accel */
    TEST_ASSERT_EQUAL_INT32(5800, ramp_stop_index(&r, 5000)); /* cruise: full ramp */
    TEST_ASSERT_EQUAL_INT32(10000, ramp_stop_index(&r, 9990));/* capped by the plan */
}

static void test_stop_index_triangle_profile(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 1000, CRUISE_US, START_US, 800);       /* accel clamps to 500 */
    TEST_ASSERT_EQUAL_INT32(500, ramp_stop_index(&r, 250));
}

static void test_stopping_interval_is_continuous_at_the_stop(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    /* stop requested mid-accel at 400: no jump in speed at that step */
    TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, 400),
                             ramp_interval_us_stopping(&r, 400, 800));
    /* stop requested at cruise */
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us_stopping(&r, 5000, 5800));
}

static void test_stopping_interval_mirrors_the_accel_ramp(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    for (int32_t k = 0; k < 800; k += 13) {
        TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, 800 - k),
                                 ramp_interval_us_stopping(&r, 5000 + k, 5800));
    }
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us_stopping(&r, 5800, 5800));
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us_stopping(&r, 5900, 5800));
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `pio test -e native -f test_ramp`
Expected: compile error, implicit declaration of `ramp_stop_index`.

- [ ] **Step 3: Implement in `ramp.h` / `ramp.c`**

`include/ramp.h`: replace the top comment's last sentence "Pure math: the motion ISR asks for the interval of the step it is about to schedule." with "Pure math, and the motion ISR's only ramp code: it asks for the interval of the step it is about to schedule." Add after the includes:

```c
/* The ISR calls the per-step functions, so on the device they must live in
 * IRAM (flash writes during OTA or NVS commits must never stall stepping).
 * Host builds have no IRAM. */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define RAMP_HOT IRAM_ATTR
#else
#define RAMP_HOT
#endif
```

and after `ramp_interval_us`'s declaration:

```c
/* A stop requested at step idx: the step index to halt at. Decelerates over
 * as many steps as the move is into its ramp (at least one), capped by what
 * remains of the plan. */
int32_t ramp_stop_index(const ramp_plan_t *r, int32_t idx);

/* Interval for step idx while decelerating to halt at stop_at: the mirror of
 * how far into the ramp the remaining steps are, so speed is continuous at
 * the moment of the stop. */
uint32_t ramp_interval_us_stopping(const ramp_plan_t *r, int32_t idx,
                                   int32_t stop_at);
```

`src/ramp.c`: replace the file header comment with

```c
/**
 * @file ramp.c
 * @brief Pure trapezoid/triangle profile math — and the motion ISR's only
 *        copy of it. The per-step functions carry RAMP_HOT, which places
 *        them in IRAM on the device; host tests compile the same code.
 */
```

Add `RAMP_HOT` to `interval_at` (`static RAMP_HOT uint32_t interval_at(...)`) and to `ramp_interval_us` (`RAMP_HOT uint32_t ramp_interval_us(...)`), and append:

```c
RAMP_HOT int32_t ramp_stop_index(const ramp_plan_t *r, int32_t idx)
{
    int32_t from_end = r->total - idx;
    int32_t into     = idx < r->accel_steps ? idx : r->accel_steps;
    int32_t decel    = into < from_end ? into : from_end;
    if (decel < 1) decel = 1;
    return idx + decel;
}

RAMP_HOT uint32_t ramp_interval_us_stopping(const ramp_plan_t *r, int32_t idx,
                                            int32_t stop_at)
{
    int32_t remaining = stop_at - idx;
    int32_t from_end  = remaining < 0 ? 0 : remaining;
    int32_t i = (idx < from_end) ? idx : from_end;
    return interval_at(r, i);
}
```

- [ ] **Step 4: Run the ramp tests**

Run: `pio test -e native -f test_ramp`
Expected: all pass (previous count + 4).

- [ ] **Step 5: `src/motion.c` uses them.** Delete `isr_interval` and its comment. In `timer_cb`, replace `.alarm_count = isr_interval(s_step_idx),` with:

```c
            .alarm_count = s_stop_req
                ? ramp_interval_us_stopping(&s_plan, s_step_idx, s_stop_at_idx)
                : ramp_interval_us(&s_plan, s_step_idx),
```

Replace the body of `motion_stop` with:

```c
void motion_stop(void)
{
    if (!s_moving || s_stop_req) return;
    /* Halt after decelerating from the current speed (ramp_stop_index). */
    s_stop_at_idx = ramp_stop_index(&s_plan, s_step_idx);
    s_stop_req    = true;
}
```

- [ ] **Step 6: Build and verify IRAM placement**

Run:

```bash
rm -rf .pio/build/seeed_xiao_esp32c6_zigbee && pio run -e seeed_xiao_esp32c6_zigbee
~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm .pio/build/seeed_xiao_esp32c6_zigbee/firmware.elf | grep -E " (timer_cb|interval_at|ramp_interval_us|ramp_interval_us_stopping|ramp_stop_index|__udivdi3)$"
```

Expected: firmware `SUCCESS`, and every address for `timer_cb`, `interval_at`, `ramp_interval_us`, `ramp_interval_us_stopping` and `ramp_stop_index` starts with `408` (IRAM) (`interval_at` may be absent: the compiler can inline it into its IRAM callers, which is fine); `__udivdi3` starts with `400` (ROM). Any `420…` address is flash: the change is not done. `isr_interval` must not appear.

- [ ] **Step 7: Run everything and commit**

Run: `pio test -e native`
Expected: `140 test cases: 140 succeeded`.

```bash
git add include/ramp.h src/ramp.c src/motion.c test/test_ramp/test_ramp.c
git commit -m "Make ramp.c the motion ISR's only ramp code, in IRAM

motion.c kept its own copy of the ramp maths (isr_interval) plus the
stop maths in motion_stop(), none of it tested, while ramp.c's header
claimed the ISR already used it. The stop logic moves into ramp.c as
ramp_stop_index() and ramp_interval_us_stopping(), with tests, and a
RAMP_HOT macro places the per-step functions in IRAM on the device.
The linker output confirms the placement.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: `status_led` state under a critical section (S10)

**Files:**
- Modify: `src/status_led.c`

- [ ] **Step 1: Implement.** Add after the includes: `#include "freertos/FreeRTOS.h"` and after `static esp_timer_handle_t s_timer;`:

```c
/* base+tick and trans+tick are each written as a pair by the dispatcher and
 * read by the timer task; without the lock a flash landing between the two
 * writes could be ended by a stale tick and never show. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
```

Replace `tick_cb`, `status_led_set` and `status_led_flash` with:

```c
static void tick_cb(void *arg)
{
    (void)arg;
    bool lvl;
    portENTER_CRITICAL(&s_mux);
    if (s_trans != LED_OFF) {
        lvl = pattern_level(s_trans, s_trans_tick);
        s_trans_tick++;
        /* transient ends after its flash train (ACK 12, ERROR 14 ticks) */
        if ((s_trans == LED_ACK && s_trans_tick >= 12) ||
            (s_trans == LED_ERROR && s_trans_tick >= 14)) {
            s_trans = LED_OFF;
        }
    } else {
        lvl = pattern_level(s_base, s_base_tick);
        s_base_tick = (s_base_tick + 1) % FRAME;
    }
    portEXIT_CRITICAL(&s_mux);
    gpio_set_level(s_ext, lvl);
}

void status_led_set(led_pattern_t base)
{
    portENTER_CRITICAL(&s_mux);
    if (base != s_base) { s_base = base; s_base_tick = 0; }
    portEXIT_CRITICAL(&s_mux);
}

void status_led_flash(led_pattern_t transient)
{
    if (transient != LED_ACK && transient != LED_ERROR) {
        return;   /* only flash trains are transients; base patterns never overlay */
    }
    portENTER_CRITICAL(&s_mux);
    s_trans = transient;
    s_trans_tick = 0;
    portEXIT_CRITICAL(&s_mux);
}
```

`pattern_level` is a pure switch with no calls, so holding the lock across it is a few instructions.

- [ ] **Step 2: Build**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add src/status_led.c
git commit -m "status_led: write and read pattern+tick pairs under a lock

status_led_flash() wrote the transient and then its tick, and the timer
task could run between the two, see the new pattern with the old tick
and end it at once: an ACK straight after an ERROR could vanish. Both
sides now take a short critical section.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

## Phase 4: converter, docs, release

### Task 13: Converter reads `motor_reversed` back (S5)

**Files:**
- Modify: `z2m/dfr_roller_blinds.js`

- [ ] **Step 1: Implement.** Replace `tzMotorReversed.convertSet` with:

```js
    convertSet: async (entity, key, value, meta) => {
        // ZBOSS (device-side stack) has an inverted-mask bug in its Mode
        // value check: any write where (value & 0x1f) == 0 gets INVALID_VALUE,
        // so a plain 0x00 is unwritable. Keep bit3 ("LEDs will display
        // feedback" — true for this device) set so 'false' stays writable;
        // the firmware reads only bit0 and reports back the canonical 0/1.
        const mode = value ? 0x09 : 0x08;
        await entity.write('closuresWindowCovering', {windowCoveringMode: mode});
        // Deliberately no optimistic state: the device refuses the toggle
        // while the blind moves (and on an NVS failure), and Mode cannot be
        // reported, so read back what it actually holds.
        await entity.read('closuresWindowCovering', ['windowCoveringMode']);
        return {};
    },
```

- [ ] **Step 2: Syntax check**

Run: `node --check z2m/dfr_roller_blinds.js`
Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add z2m/dfr_roller_blinds.js
git commit -m "Converter: read motor_reversed back instead of assuming the write took

The device refuses the toggle while moving, and Mode can't be reported
because of the ZBOSS bug, so z2m kept showing the value it asked for.
Same pattern travel_time already uses: write, then read back.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

Deployment to the z2m box is **not** part of this task: it restarts zigbee2mqtt. It happens in Task 16, with the owner's go-ahead.

---

### Task 14: Documentation

**Files:**
- Modify: `CLAUDE.md`, `CONTEXT.md`, `DEVELOPER_GUIDE.md`, `docs/superpowers/specs/2026-07-18-roller-blinds-design.md`, `include/app_event.h`

- [ ] **Step 1: Calibration timeout is 10 min everywhere.**
  - `include/app_event.h`: `APP_EVT_CAL_TIMEOUT` comment → `/* 10-min calibration timeout (CAL_TIMEOUT_US) */`.
  - `CLAUDE.md` line with "5-minute timeout" → "10-minute timeout".
  - `DEVELOPER_GUIDE.md:84` → 10 minutes.
  - Design spec §6 (search "5 min"/"5-minute"): change to 10 min and add one sentence: "Raised from 5 min in commit 747e2fc so a full-span calibration jog can never outlive it."

  Run: `git grep -nE "5[- ]min" -- CLAUDE.md DEVELOPER_GUIDE.md include docs/superpowers/specs/2026-07-18-roller-blinds-design.md`
  Expected: no hits about the calibration timeout.

- [ ] **Step 2: Design spec §5 known deviation.** After the §5 text that says OTA applies only when idle, add:

```markdown
> **Known deviation (2026-09-25).** `esp-zb-common`'s `ota_client` reboots as
> soon as a download finishes, even mid-move, and the app has no hook to defer
> it. A reboot that lands mid-move leaves `moving` set, so the unit boots to
> Position Unknown and needs a Re-home. Deferring the reboot until idle needs a
> library change and is future work; see
> [2026-09-25-review-fixes-design.md](2026-09-25-review-fixes-design.md#deferred).
```

- [ ] **Step 3: `CLAUDE.md` architecture.**
  - Module table: add a row `| ctl | src/ctl.c | Pure: every dispatcher decision (gesture matrix, calibration flow, preemption, persistence policy) behind ctl_ports_t | test/test_ctl/ |`; change `main`'s purpose to "Wiring: GPIO map, constants, ports table binding ctl to the real modules, queues, dispatcher loop"; change the converter row to "(cover + motor_reversed + calibrated + travel_time)".
  - Concurrency section: replace "happens in `dispatcher_task` (`src/main.c`), which owns `s_pos` and all mutable state and processes one `app_event_t` at a time off a single FreeRTOS queue" with "happens in `ctl_handle()` (`src/ctl.c`), called only by `dispatcher_task` (`src/main.c`), one `app_event_t` at a time. The dispatcher waits on a FreeRTOS queue set: the main queue plus a one-slot queue for `MOTION_DONE`, which cannot overflow because ctl never starts a move while one is outstanding. ctl owns \"move in progress\" (`move_active`): the ISR's own flag clears at the last step, before the final position reaches the dispatcher, so it must not be used to decide anything."
  - Architecture step list: step 6 becomes "`ctl_init()` restores state (an unclean mid-move shutdown drops to Position Unknown here); `app_main` sets the LED from `ctl_led_pattern()`, creates the dispatcher, waits for the join, then posts `APP_EVT_BOOT_SYNC`."
  - Re-copy the "Key Configuration Constants" block verbatim from `src/main.c` (GPIO map and motion tuning `#define`s with their comments exactly).

- [ ] **Step 3b: `HARDWARE.md` — Schottky diode (added 2026-09-26).** On bench3, USB alone put 4.47 V on `VM` (USB VBUS → XIAO 5V pin → backwards through the buck) and ran the motor with 24 V off. In the power-chain diagram and text, add a Schottky diode in series from the buck's 5 V output to the XIAO 5V pin (anode at the buck, cathode/band at the XIAO; 1N5817/SS14, or 1N5822 — oversized, DO-201AD leads ~1.3 mm), with that reason; add it to the BOM; and add a Troubleshooting entry "Motor turns with 24 V off" pointing at it. Until it is fitted: flash with USB only and the motor disconnected; run on 24 V only.

- [ ] **Step 4: `CONTEXT.md` vocabulary.**
  - Span entry: add "Also wiped by a Motor Reversed toggle."
  - Position Unknown entry: add "Also entered when a Calibration Mode session in which the blind was jogged is aborted (Fn long-press or timeout)."

- [ ] **Step 5: `DEVELOPER_GUIDE.md`** — add a bench-checklist block for this release (under the existing bench checklist):

```markdown
**v2.4.0 (review fixes) bench checks**
- [ ] Calibrated unit powered with the coordinator off: LED shows NO_NETWORK from power-on
- [ ] Tap exactly as a full travel ends: no second travel from the old position
- [ ] Hold a jog, release: stops; hold during heavy z2m traffic, release: stops
- [ ] Motor Reversed toggled from z2m while idle: ACK, calibration wiped, z2m shows the new value
- [ ] Motor Reversed toggled from z2m while moving: refused, z2m shows the unchanged value
- [ ] Fn held 3 s during a travel: ERROR flash, no Calibration Mode
- [ ] Three-key reset during a travel: ERROR flash, no reset
- [ ] `riscv32-esp-elf-nm` shows the ramp functions and timer_cb at 0x408… (IRAM)
```

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md CONTEXT.md DEVELOPER_GUIDE.md docs/superpowers/specs/2026-07-18-roller-blinds-design.md include/app_event.h
git commit -m "Docs: ctl architecture, 10-min timeout, OTA deviation, vocabulary

The calibration timeout has been 10 min since 747e2fc but four documents
still said 5. CLAUDE.md now describes ctl, its ports and the queue set,
lists travel_time among the converter's outputs, and carries the
constants block verbatim again. Spec §5 records that an OTA reboot can
still land mid-move. CONTEXT.md gains the two other ways Span and
Position Unknown change.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 15: Whole-branch verification

- [ ] **Step 1: Clean build and full host run**

Run: `rm -rf .pio/build/seeed_xiao_esp32c6_zigbee && pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: `SUCCESS`; `140 test cases: 140 succeeded`.

- [ ] **Step 2: Nothing outside ctl decides**

Run: `git grep -n "motion_is_moving\|motion_stop" -- src/main.c src/keypad.c src/covering.c`
Expected: only the `PORTS` initialiser line `.motion_stop = motion_stop,` in `src/main.c`.

- [ ] **Step 3: Push**

```bash
git push
```

---

### Task 16: Release (HUMAN GATES throughout)

Every step here needs the owner present and saying go.

- [ ] **Step 1: Bench `bench2`.** Owner flashes (as Task 4 Step 6) and runs Task 4's seven checks plus the v2.4.0 checklist in `DEVELOPER_GUIDE.md`.
- [ ] **Step 2: Deploy the converter** (owner's go-ahead; restarts zigbee2mqtt). On the z2m box: back up `data/external_converters/dfr_roller_blinds.js` to `dfr_roller_blinds.js.bak-2026-09-25`, `scp` the new file over it, `sudo systemctl restart zigbee2mqtt`, then toggle `motor_reversed` on `bench2` while it moves and confirm z2m shows the unchanged value.
- [ ] **Step 3: Unattended soak** on `bench2`: owner notes the start time and leaves the rig untouched; afterwards check z2m for travels with no preceding `/set`, and dump the trace (press RESET, do not unplug) if any appear.
- [ ] **Step 4: Merge and tag** (owner's go-ahead): merge `review-fixes` into `main` with `--no-ff`, delete the branch, then `git tag -a v2.4.0 -m "v2.4.0: review fixes" && git push origin main v2.4.0`. Watch the `Release OTA` workflow to success and confirm `ota/index.json` on `main` lists `fileVersion` `0x24000000` (603979776).
- [ ] **Step 5: Update `blinds lounge side`** from z2m (Device → OTA → Update) with the owner present to Re-home if the OTA reboot lands mid-move.

---

### Task 17: Follow-up on branch `fn-to-d3`

Run only after Task 16 Step 4 (so the rebase has a target).

- [ ] **Step 1:** `git switch fn-to-d3 && git rebase main` (resolve `src/main.c` conflicts: keep this branch's pin `#define`s and `main`'s new body).
- [ ] **Step 2:** Fix the drift the review found on this branch:
  - `src/main.c` GPIO-map comment: "keypad on D3-D5, LED on D2; D6 spare"; delete the sentence that says D6/D7 carry UART0 pins in use (only D7 is used now).
  - `DEVELOPER_GUIDE.md:232`: append "Pin map changed in v2.5.0: Fn D3/GPIO21, LED D2/GPIO2 (both previously confirmed on hardware)."
  - `tools/pinwalk/src/pinwalk.c`: change `{16,"D6","Keypad Fn"}` to `{21,"D3","Keypad Fn"}` and `{21,"D3","LED"}` to `{2,"D2","LED"}`.
  - Replace "v2.4.0" with "v2.5.0" in every pin-map reference: `git grep -n "v2.4.0" -- HARDWARE.md CLAUDE.md DEVELOPER_GUIDE.md src` and edit each hit that refers to the pin map.
- [ ] **Step 3:** `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native` → `SUCCESS`, all pass.
- [ ] **Step 4:** Commit ("fn-to-d3: fix stale pin references; the pin move ships as v2.5.0") and `git push --force-with-lease` (the branch was rebased; confirm with the owner first).
