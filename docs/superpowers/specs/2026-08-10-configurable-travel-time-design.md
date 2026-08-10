# Configurable travel time via zigbee2mqtt

**Date:** 2026-08-10
**Status:** approved, not yet implemented

## Problem

Cruise speed is the compile-time constant `CRUISE_US` in `src/main.c`
(currently 150 µs/step, ≈55 s full travel on the first installed blind).
Changing it means cutting a release.

The first unit is now installed and **not reachable with a laptop**, so the
only update path is OTA — about an hour per cycle. Tuning speed by flashing
is therefore impractical: finding a limit by bisection would take most of a
day.

Two further pressures make this structural rather than a one-off:

- **Fleet variation.** The blinds differ in fabric weight, roll diameter and
  span, and the fleet uses two motors (17HS4401 on the smaller blinds,
  2HS60-1504JA05-020-03 on the larger). A single compile-time constant must
  be tuned for the worst case, leaving every other unit slower than it could
  be.
- **Install-time tuning.** Each new blind wants its own figure, and wants it
  found in minutes at commissioning rather than via a release cycle.

## Goal

Set full-travel time per unit from zigbee2mqtt, persisted across reboots,
with a firmware-side safety clamp — no reflash.

## Non-goals

- Jog speed stays a compile-time constant (`JOG_CRUISE_US`). It is primarily
  a calibration tool, works as-is, and exposing it adds surface for no
  benefit.
- Acceleration and deceleration stay compile-time. `ACCEL_STEPS` is ~800 out
  of ~600 000 steps in a full move and has negligible effect on travel time.
- No stall detection. The design is open-loop with no encoder and no
  StallGuard wiring, so the device cannot detect a stall to recover from a
  bad setting.

## The exposed quantity: seconds of full travel

The user sets **full-travel time in seconds**, not µs/step.

Reasons:

- It is how the blinds are actually reasoned about ("55 s today, want 30 s").
- It is **self-normalising across the fleet**: the same number produces the
  same feel on a small blind and a large one, so a sensible fleet-wide
  default exists. A µs/step figure is meaningless without also knowing the
  span.
- µs/step is inverted (bigger is slower) and leaks the implementation.

The consequence to accept deliberately: because duration is what is stored,
**re-calibrating to a different span changes the underlying step rate** to
hold the duration constant. That is the intended behaviour, not a bug.

## Conversion and clamping

```c
us = (secs * 1000000ULL) / span_steps;
clamp us to [RAMP_MIN_CRUISE_US, RAMP_MAX_CRUISE_US]
```

Signature, in `ramp.h` alongside the constants:

```c
/* Convert a requested full-travel duration into a per-step cruise interval,
 * clamped to what the hardware can be trusted to do. Returns
 * RAMP_DEFAULT_CRUISE_US if span_steps <= 0. Pure. */
uint32_t ramp_us_from_travel_time(uint16_t secs, int32_t span_steps);
```

| Constant | Value | Rationale |
|---|---|---|
| `RAMP_MIN_CRUISE_US` | 60 | ~625 motor RPM, ~22 s on the first blind. Leaves room past the 30 s target to find the real limit by experiment, while rejecting absurd requests. |
| `RAMP_MAX_CRUISE_US` | 1000 | Sanity ceiling. |
| `RAMP_DEFAULT_CRUISE_US` | 150 | Current tuned value; the fallback when nothing is stored and the `span_steps <= 0` guard result. Replaces `CRUISE_US` in `main.c`. |

Use `uint64_t` for the multiply. The largest realistic numerator
(300 s × 10⁶) fits in 32 bits, but the wider type removes the question.

**The clamp lives on the derived µs/step, not on the seconds**, because
µs/step is the quantity that physically stalls the motor. A duration that is
achievable on one blind may be unreachable on another.

### `START_US` inversion

`ramp_plan_init()` assumes `start_us >= cruise_us` — the first step is the
slowest. A long travel time can derive a cruise interval above the fixed
`START_US` (500 µs) and invert that assumption.

The plan therefore uses `start = MAX(START_US, us)`, degenerating to no
acceleration phase when cruise is already slower than the normal start rate.

## Architecture

The concurrency rule is unchanged: **the action handler validates and
enqueues; the dispatcher decides.** Nothing here touches the step ISR.

| Layer | Change |
|---|---|
| `ramp.c` / `ramp.h` | New pure function `ramp_us_from_travel_time()`. All arithmetic and clamping; host-testable. |
| `covering.c` | Register attribute `0x0014`; extend the existing `SET_ATTR_VALUE` case to match it and enqueue. |
| `include/app_event.h` | `APP_EVT_ZB_SET_SPEED` plus a `uint16_t secs` member in the existing union. |
| `main.c` | Dispatcher: clamp, persist, update `PROF_MOVE`, write achieved value back. `PROF_MOVE` stops being `const`. |
| `blind_store.c` / `.h` | New NVS key `spd` (u16 seconds) in namespace `blind`; field on `blind_store_data_t`; `blind_store_save_travel_time()`. |
| `z2m/dfr_roller_blinds.js` | `travel_time` numeric expose, `tz` write and `convertGet`. |

The expose:

```js
e.numeric('travel_time', ea.ALL)
    .withUnit('s')
    .withValueMin(10).withValueMax(300)
    .withDescription('Time for a full open/close travel. The device clamps
                      this to what its motor can drive and reports back the
                      value actually applied.')
```

The z2m bounds (10–300 s) are a UI guard only and are deliberately wider than
any blind will use. **The authoritative limit is the firmware clamp on
µs/step**, because the achievable duration depends on the span and so differs
per blind — a bound z2m cannot know.

### Why attribute `0x0014`

`ESP_ZB_ZCL_ATTR_WINDOW_COVERING_VELOCITY_ID = 0x0014` is a **standard
optional** Window Covering attribute, confirmed present in both
`esp-zigbee-lib` and `esp-zboss-lib` headers.

This project has **no manufacturer-specific attributes**, deliberately:
`motor_reversed` rides `Mode` (0x0017) bit 0 and `calibrated` rides
`ConfigStatus` (0x0007) bit 0. The prebuilt ZBOSS stack has already produced
two attribute-layer defects here (`Mode == 0x00` unwritable; the reporting
engine crash-looping on non-lift attributes), so staying on standard,
stack-known attributes is the low-risk path.

**Semantic note:** ZCL defines 0x0014 as a velocity; this design stores a
duration in it. That is a deliberate pragmatic reuse, consistent with how
`Mode` and `ConfigStatus` are already used. The converter presents it
correctly. Recorded so it is not later "corrected".

**The attribute must NOT carry `ESP_ZB_ZCL_ATTR_ACCESS_REPORTING.`** That flag
is what makes the stack's reporting engine crash-loop on anything but lift.
Like `motor_reversed`, this value is read on demand.

## Data flow

**Write.** z2m writes `0x0014` → `covering_action_handler` matches the
attribute and posts `APP_EVT_ZB_SET_SPEED` → dispatcher converts, clamps,
saves to NVS, updates `PROF_MOVE`, and calls the existing `set_attr()` to
write the **achieved** seconds back.

Writing back the achieved value is what makes the clamp discoverable: ask for
30 s on a blind that can only do 34 s and the field settles at 34. No error
to interpret, and the physical limit is found by nudging the number down
until the blind stops moving — which is the workflow this feature exists for.

**Move.** `PROF_MOVE` already holds the derived interval. The ISR and the
motion module are untouched.

**Boot.** Load `spd`. Absent or `0` → fall back to `RAMP_DEFAULT_CRUISE_US`.
This matters: **existing units taking this change as an OTA keep their
current ~55 s behaviour** instead of jumping to a new speed.

So that z2m does not show a meaningless `0` on a device that has never been
set, boot also initialises the attribute to the equivalent seconds —
`span_steps × cruise_us / 10⁶` — whenever the span is known. A freshly
updated unit therefore reads ~55 s, which is both true and a sensible
starting point to tune down from. On an uncalibrated device the attribute
stays `0` until calibration provides a span.

**After calibration.** A new span changes the mapping, so recompute and write
the achieved value back when calibration commits. This is what keeps the
duration semantics honest.

## Edge cases

| Case | Behaviour |
|---|---|
| Uncalibrated (no span) | Remote motion is already locked out and taps are inert, so the setting cannot take effect. The write is stored and echoed back as requested; the clamp applies — and the attribute is corrected — when calibration commits a span. |
| Write mid-move | Applies to the **next** move; the current move keeps its plan. |
| Request unreachable | Clamped; the attribute reports what was applied. |
| `span_steps == 0` | Guarded in the pure function — return the compile-time default rather than dividing by zero. |
| Value too fast for a replacement motor | Stored value persists across a motor swap. A stall is possible; recovery is to set a slower value from z2m. See risk below. |

## Risk: a bad value needs a physical re-home

A too-fast setting stalls the motor. Position is open-loop step counting, so
a stall **silently desyncs the stored position** — and re-homing requires the
keypad (Fn 3 s → jog Open → Fn), which means physical access to an installed
blind.

Mitigations: the `RAMP_MIN_CRUISE_US` floor bounds how badly it can be set, and
the setting itself remains changeable from z2m even after a stall (Zigbee is
unaffected). The residual cost of experimenting too aggressively is one trip
to the keypad. Accepted knowingly.

## Testing

**Host tests** (`test/test_ramp/`, runs under `pio test -e native`, no
hardware) on `ramp_us_from_travel_time()`:

- nominal conversion against a realistic span
- clamping at `RAMP_MIN_CRUISE_US` and at `RAMP_MAX_CRUISE_US`
- `span_steps == 0` returns the default rather than dividing by zero
- a duration that maps exactly to a bound

That is the entire risk surface of the arithmetic.

**On-device**, one OTA cycle: write 30 from z2m, confirm the attribute either
holds or settles at the clamped value, observe travel time change, power
cycle and confirm it persisted, and confirm an uncalibrated device still
rejects motion.

## Open question deliberately left open

Whether one travel-time default serves both motor types is still unmeasured —
the 2HS60 has only run unmounted and at the wrong Vref. This feature makes
that question cheap to answer per unit rather than answering it, which is the
point.
