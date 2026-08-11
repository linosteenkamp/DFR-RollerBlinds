# Keypad debounce: phantom taps run the blind end to end

**Date:** 2026-08-11
**Status:** approved, not yet implemented
**Repos:** `DFR-RollerBlinds` only (no `esp-zb-common` change)

## Problem

The night after `lounge-blind-3` was installed, it ran up and down by itself
six times between 02:57 and 04:08, until the unit was unplugged at the wall.

Nothing on the network commanded it. Every frame the coordinator sent the
device in the logged window (02:46 → 04:08) was accounted for: 117 Default
Responses to the device's own lift reports, 8 `genBasic` reads for
availability, 2 OTA query-next-image responses. No Window Covering command,
and no MQTT `set` for the device all night. Every other Zigbee device kept
talking after 04:08, so it was not a power event — that was the unplug.

In v2.1.0 motion can only begin in `goto_pct()` or `jog()`. Their only
non-network callers are keypad events. The device therefore saw keypad
presses that nobody made.

### Mechanism

`src/keypad.c` polls every 20 ms and feeds each raw sample straight into the
shared library's change detector:

```c
int level = gpio_get_level(s_gpio[k]);
if (debounce_settle(&s_db[k], level)) {
    post_kp(kp_on_change(&s_kp, (key_id_t)k, level == 0, now));
}
```

`debounce_settle()` (`esp-zb-common/src/debounce.c`) does no debouncing. It
is a pure change detector:

```c
if (sampled_level == d->stable_level) return false;
d->stable_level = sampled_level;
return true;
```

Its header states the contract: *"the ISR restarts a one-shot timer on every
edge; when the timer finally expires the settled GPIO level is passed to
debounce_settle()"*. The settling belongs to the caller. DoorSensor's
`door_contact.c` supplies exactly that — a both-edge ISR restarting a
one-shot `esp_timer`, sampling only after the line goes quiet.

`keypad.c` has no such timer. It polls. So one aberrant sample is a complete
press and the next sample is the release: 20 ms apart, far under `HOLD_MS`
(400), which `kp_on_change` classifies as a **TAP**. On a calibrated blind a
tap is a full travel. Noise rejection is zero — a spike of any width that
lands on a poll instant runs the blind end to end.

The library module is not at fault and is not being changed. This is a
`keypad.c` defect.

### Why the bench never saw it

Bench leads are short. Installation put the keypad harness alongside the
stepper drive, and the inputs hang on the ESP32's internal pull-up
(~45 kΩ, `keypad.c:65`) with no external pull-up and no filtering — a
high-impedance node beside a cable carrying chopped current at 1.7 A.

It also sits in a test blind spot: `test/test_keypad/` covers
`keypad_logic.c` thoroughly, but the bug is in `keypad.c`, the untested glue.

### The log

| Time | Event |
|---|---|
| 02:57:58 | starts closing, stopped after 6 s at 87 (a second phantom tap — any tap stops) |
| 03:01:54 | reverses, runs to the 100 limit, completes |
| 03:57:36 | full travel 100 → 0, 45 s, completes cleanly |
| 04:05:51 | 0 → 27, interrupted at 11 s |
| 04:06:14 | back down to 0 |
| 04:08:24 | 0 → 78, then the unit was unplugged |

Complete travels are single phantom taps; interrupted ones are a second
phantom arriving mid-move. The 45 s full travel matches `travel_time` 42
plus the known ~3 s overhead, confirming these ran on `PROF_MOVE` — taps,
not jogs.

## Goal

A single noise glitch, of any width the wiring can produce, cannot start or
stop a move.

## Non-goals

- **Guarding the destructive gestures individually.** A sustained burst long
  enough to fake a press could in principle reach Fn-long (Calibration Mode)
  or the Up+Down chord (wipes calibration). Deliberately out of scope: the
  observed failure is entirely single-glitch taps, and requiring a clean
  uninterrupted press for those gestures would add state to `keypad_logic.c`
  and new tests for a failure mode not yet seen.

  The accepted exposure, made explicit — what a noise burst must sustain to
  reach each outcome, all measured against the 12-sample/60 ms integrator:

  | Outcome | What it takes | Effect |
  |---|---|---|
  | Phantom press | net-low ≥ 60 ms | harmless alone |
  | TAP | the above, then net-high, total < 400 ms | full travel (idle only) |
  | HOLD_START | net-low ≥ 460 ms on Up or Down | jog, clamped to the limits |
  | FN_LONG | net-low ≥ 3060 ms on Fn alone | enters Calibration Mode |
  | CHORD_REVERSE | net-low ≥ 3060 ms on Up AND Down | wipes calibration |
  | FACTORY_RESET | net-low ≥ 5060 ms on all three | leaves the Zigbee network |

  The accepted exposure is "≥3 s of sustained net-low on two or three
  lines," not "no phantom gestures" — this design does not claim the latter.
  Every destructive gesture is already guarded by `motion_is_moving()`
  elsewhere in the firmware, so the noisiest phase (motor running, coupling
  at its worst) can only ever produce a benign `motion_stop()`, not one of
  the outcomes above.
- **An input plausibility/rate layer.** Rejected as complexity without
  evidence.
- **Changing the gesture matrix or its semantics.** A tap remains a full
  travel.
- **Fixing `esp-zb-common`'s `debounce` module.** Correct for its contract
  and its one other consumer.
- **The hardware change itself.** Specified below as a deliverable for the
  next build; not implemented in this work.

## Approach

An integrator (Kuhn) filter between the GPIO and `kp_on_change`, replacing
`debounce_settle` at the call site.

Considered and rejected:

- **N consecutive identical samples.** Smallest diff, but it only rejects
  glitches shorter than the window; a line that rattles gets through as soon
  as N samples happen to agree. Firmware has to cope alone here, and that
  failure mode is "enough noise and it's back".
- **DoorSensor's edge-ISR + one-shot settle timer.** Symmetric with the
  sibling and reuses the library as intended, but needs per-key timer state,
  moves work into ISR context, resists host testing, and is *weaker* on a
  noisy line: it samples once after the settle window, so a glitch coincident
  with that single sample still passes.

## Design

### 1. New pure module: `key_filter`

`src/key_filter.c`, `include/key_filter.h`. Drop-in at `keypad.c:47` with the
same shape as `debounce_settle`, so the active-low mapping (`level == 0`) and
everything downstream are untouched.

```c
typedef struct {
    uint8_t count;         /* integrator, 0..max */
    uint8_t max;
    int     stable_level;  /* debounced output */
} key_filter_t;

void key_filter_init(key_filter_t *f, uint8_t max, int initial_level);
bool key_filter_sample(key_filter_t *f, int sampled_level);  /* true = output changed */
int  key_filter_level(const key_filter_t *f);
```

Polarity-free — it integrates the raw level, not a notion of "pressed":

- sample high → `count = min(count + 1, max)`
- sample low → `count = max(count - 1, 0)`
- `count == max` → output 1; `count == 0` → output 0; in between the output
  stays latched
- returns true only when the output actually flips

`key_filter_init` seeds the counter at the rail matching `initial_level`
(`initial_level ? max : 0`) and sets `stable_level` to it, so a filter
starting on a settled line needs a full traverse before its first event.

The latching band is the point: the line must travel the full distance to
change the output, so noise that rattles the counter never crosses.

**Constants: poll at 5 ms, `max = 12`** → 60 ms of sustained level to
register a change.

Two properties:

- **Durations are preserved.** 60 ms of latency is added at both the press
  and the release edge, so the interval `keypad_logic` measures is unchanged.
  `HOLD_MS` 400 and `LONG_MS` 3000 keep their current feel and no existing
  gesture test shifts.
- **Rejection is about energy, not alignment.** A one-sample spike lifts the
  counter to 1 and it decays in 5 ms. Faking a press needs the line low for a
  *net* 12 samples more than high — 60 ms of predominantly-low, against
  today's one sample.

### 2. `keypad.c` integration

- `POLL_MS` 20 → 5; add `FILTER_SAMPLES 12`
- `debounce_t s_db[]` → `key_filter_t s_filt[]`; `#include "debounce.h"` →
  `"key_filter.h"`
- `keypad_init` seeds each filter from the current pin level, as it seeds the
  debouncer today, so a key held at boot fires no spurious edge
- poll body otherwise unchanged

The 5 ms tick also drives `kp_on_tick`, improving gesture timing resolution
from 20 ms to 5 ms. Cost is three GPIO reads and a few comparisons at 200 Hz.

### 3. Hardware glitch filter

Added in `keypad_init` after `gpio_config`, on all three pins. The *flex*
filter rather than the pin filter: the pin filter's window is fixed at two
IO-MUX clocks (~50 ns), while flex takes an explicit threshold. The C6 has 8
(`SOC_GPIO_FLEX_GLITCH_FILTER_NUM`); we need 3. Verified available on
ESP-IDF 5.5.1.

```c
gpio_flex_glitch_filter_config_t fcfg = {
    .clk_src         = GLITCH_FILTER_CLK_SRC_XTAL,
    .gpio_num        = s_gpio[k],
    .window_width_ns = 1500,
    .window_thres_ns = 1500,
};
```

**The clock source is not the default, deliberately.** The C6 caps the window
at 63 ticks (`GPIO_LL_GLITCH_FILTER_MAX_WINDOW`), and
`GLITCH_FILTER_CLK_SRC_DEFAULT` is PLL_F80M at 12.5 ns/tick, which caps the
window at 787 ns and rejects anything longer with `ESP_ERR_INVALID_ARG`.
XTAL runs at 40 MHz / 25 ns/tick, so 1500 ns is 60 ticks — inside the limit
and a wider rejection window than the default clock can express at all.

Real presses are milliseconds, so genuine input is unaffected.

**This does not close the bug on its own.** A 45 kΩ pull-up against a metre
of cable capacitance has an RC recovery on the order of microseconds, so a
coupled spike can hold the line low past any window these filters can set.
The integrator is the fix; the glitch filter is cheap insurance under it.

### 4. Error handling

Glitch-filter creation failure is logged at `ESP_LOGW` and init continues.
Deliberate: the integrator is load-bearing and the filter is opportunistic,
so a device that cannot allocate one should still come up with working
debounced keys rather than fail `keypad_init` and leave the blind with no
local control. Handles are created once and never deleted, matching the
existing `static` poll timer. `post_kp`'s queue-full handling is untouched.

### 5. Documentation

- `keypad.h`'s header comment — currently promises "every 20 ms" and "the
  library debounce module", both about to be false
- CLAUDE.md: the `keypad` row of the module table, plus a new `key_filter`
  row

## Testing

New host suite `test/test_key_filter/`, added to the `native` env's
`test_filter` in `platformio.ini`. Written test-first, regression case first:

1. **single-sample glitch rejected** — steady high, one low, high → no change
2. sustained low for `max` samples → exactly one change, on the expected sample
3. no repeat events while the level holds
4. **rattling line never flips** — alternating low/high for hundreds of samples
5. **partial excursion retreats** — 11 lows then a high → no change
6. symmetric release from a stable low
7. **counter clamps at the rails** — 100 lows, then one high must not flip; a
   full 12 highs are required
8. init seeding from either level produces no spurious first event

`test_position`, `test_ramp` and `test_keypad` must pass unchanged — the
evidence that the gesture matrix and its timing survived the poll-rate change.

**Not covered by host tests:** `keypad.c`'s glue and the glitch filter are
hardware-bound, and that untested glue is exactly where this bug lived. Bench
verification is therefore part of acceptance, not optional:

- meter on `EN` (D9/GPIO20): 3.3 V idle, 0 V for the duration of a hold
- walk the full gesture matrix: tap, hold-to-jog, Fn-long into Calibration
  Mode, Up+Down chord
- soak: repeated full travels, watching for phantom events

## Rollout

Lands on `main`, which already carries the unreleased rejoin/recovery work,
so this ships as **v2.2.0** and the installed blind takes both in one OTA.

- **Do not Re-home until the fix is on.** The unplug was mid-move, so the
  device boots to Position Unknown, and taps are inert on an uncalibrated
  device (`handle_keypad` gates on `cal_dev`). In that state phantom taps
  cannot run the blind end to end. Re-homing before the OTA re-arms the bug.
  Leave it uncalibrated, take the OTA, then Re-home.
- Resolve the board-identity discrepancy first: z2m holds
  `0xa0f262fffe878e0c` → USB serial `A0:F2:62:87:8E:0C`, recorded elsewhere
  as the bare/test board rather than the installed one. Know which board is
  being flashed.

## Hardware fix for the next build

Not implemented here. Listed in priority order.

1. **External pull-ups, 4.7 kΩ to 3V3** on all three keypad lines. The
   biggest single win: ~45 kΩ → ~4.3 kΩ at the node, roughly a 10× cut in
   coupled noise voltage.
2. **RC low-pass at the MCU pin**: 1 kΩ in series from the connector, 100 nF
   from pin to GND. τ ≈ 100 µs — invisible to a human press, fatal to coupled
   spikes.
3. **Route the keypad harness away from the motor cable.** Separate bundles;
   cross at right angles where they must meet.
4. **Twisted or shielded keypad cable**, ground return alongside the signals,
   shield grounded at the MCU end only.
5. **Move Fn off D6 in the next board revision.** The physical-layout pin map
   puts keypad on D4-D6 and driver on D7-D9, landing Fn (D6/GPIO16)
   immediately adjacent to DIR (D7/GPIO17), with STEP (D8) — switching at
   kilohertz with fast edges — two pins over. That is a coupling path on the
   board itself, not only in the harness. D0, D1, D2 and D10 are spare.
