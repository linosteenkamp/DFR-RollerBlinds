# Keypad feedback: telling "nothing to do" apart from "dead"

**Date:** 2026-08-11
**Status:** implemented and **fully bench-verified 2026-08-13**; released in
v2.3.0. All eight acceptance checks pass, including check 8 — the Calibration
Mode regression gate — which was confirmed in the trace rather than only by
eye: ten taps inside the mode, none producing a `MOVE_` record, so no motion
and no flash.
**Repos:** `DFR-RollerBlinds` only

## Problem

A full day was spent chasing a "dead keypad" that was never faulty. Five
mechanisms were proposed and all five were wrong — a hardware glitch filter,
swapped key wiring, `s_raw` drift against the hard cap, intermittent key
mismapping, and a blocked dispatcher. The RTC trace
([2026-08-11-diagnostic-trace-design.md](2026-08-11-diagnostic-trace-design.md))
eventually showed every keypress registering correctly and being acted on
correctly.

The real cause: **the same key does four different things depending on state
the operator cannot see, and three of them are silent.**

| State when pressed | What happens | Visible? |
|---|---|---|
| At rest, mid-travel | full travel | yes |
| At rest, already at that limit | `start_move` returns early | **no** |
| While moving | stops the move | yes, but reads as "cancelled" |
| Held past `HOLD_MS` | jog — near a limit, imperceptible | **usually not** |

A blind spends most of its life parked at one limit or the other, which is
exactly when two of three keys legitimately do nothing. Add an uncalibrated
device (taps inert) and a refused move (logged only to a serial console nobody
is attached to), and "the keypad is dead" becomes the only reasonable
conclusion an operator can draw.

Concretely measured on the bench: a 575 ms press became a hold, jogged 175 ms,
and moved **464 steps of a 109 192-step span — 0.4%**. Invisible. The operator
reported a dead key; the firmware had done exactly what it was asked.

## Goal

An operator standing at the blind, with no laptop, can tell these apart:

- "I heard you, there is nothing to do"
- "I heard you and I am refusing, something needs attention"
- "I am dead"

## Non-goals

- **Changing `HOLD_MS` or any gesture timing.** Raising the threshold does not
  remove the dead zone, it moves it: at 700 ms, a 750 ms press still yields a
  50 ms jog. The defect is that the dead zone is *invisible*, not that it
  exists. Gesture timing and its host tests stay untouched.
- **Converting a too-short jog into a tap.** A gesture that retroactively
  becomes a different gesture is surprising, and turning a 464-step nudge into
  a 45-second full travel would be alarming on an installed blind.
- **New LED patterns.** `LED_ACK` and `LED_ERROR` already exist and already
  work.
- **A separate feedback module.** Three call sites do not need a lookup table
  wrapped in a file.
- **Feedback over Zigbee.** The gap is for someone standing at the device. z2m
  already sees position; the trace already records every outcome.
- **Extracting the visibility predicate as a pure, host-tested function.**
  Considered and deliberately declined in favour of the smaller diff — see
  Testing for the cost this incurs.

## Design

### 1. The five signals

Every keypad gesture ends in exactly one of these. Today four of the five are
silent.

| Where | Condition | Signal | Meaning |
|---|---|---|---|
| `start_move` no-op branch | `target == s_raw` (tap, jog, or z2m goto) | `LED_ACK` | already at that limit |
| `start_move` error branch | `motion_start` refused | `LED_ERROR` | hard cap or busy |
| `handle_mark`, the `if (!s_cal_mode) return;` path | Fn tapped outside Calibration Mode | `LED_ACK` | nothing to mark |
| `handle_keypad`, `KP_EVT_TAP` on Up/Down | `!cal_dev` — needs a new `else` on the existing `if (cal_dev)` | `LED_ERROR` | lockout, needs calibration |
| `APP_EVT_MOTION_DONE` | lift % unchanged | `LED_ACK` | the dead-zone jog |

Precise placement matters for two of these. In `handle_mark`, the flash belongs
on the `!s_cal_mode` early return **only** — its other early return
(`motion_is_moving()`) stops a move, which is visible and needs no signal. In
`handle_keypad`, the `KP_EVT_TAP` case currently reads `if (cal_dev) {
goto_pct(...) }` with no `else`; the `LED_ERROR` goes in a new `else`, and must
not fire for `KEY_FN`, which the preceding `break` already routes away.

`LED_ACK` is three flashes, `LED_ERROR` five rapid; both are transient overlays
that revert to the base pattern. No change to `status_led`.

**The uncalibrated Up/Down tap gets `LED_ERROR`, not `LED_ACK`.** The device is
refusing, not shrugging. The persistent `LED_UNCAL` base pattern already says
the device is uncalibrated; this makes the refusal legible at the moment of
pressing.

**`start_move` is shared across taps, jogs, and the Zigbee path**, so a hold
that jogs toward a limit already reached, or a z2m `goto` to the current
position, will also flash. Accepted: harmless, arguably useful, and separating
the callers would mean plumbing caller identity through for no real benefit.
The jog case is the one that matters most in practice — holding Up at the
open limit with no feedback was the actual reproduction that motivated this
work.

### 2. The dead-zone threshold

**The threshold is "the lift percentage did not change."** No new constant.
This is the device's own resolution: if lift % is unchanged, the operator
cannot see the movement and z2m will not report it either. The measured
464-step jog is 0.4% of span and rounds to the same percent, so it is caught.

`start_move` records the lift % before starting; `APP_EVT_MOTION_DONE`
compares after `position_set_current`:

```c
if (!s_cal_mode && position_calibrated(&s_pos) &&
    position_lift_pct(&s_pos) == s_move_start_pct) {
    status_led_flash(LED_ACK);
}
```

`s_move_start_pct` is a new file-static in `main.c`, set in `start_move`
immediately before `motion_start`.

**Both guards are load-bearing, and each prevents a way this would misfire:**

- **`!s_cal_mode`** — during calibration the LED *is* the interface.
  `LED_CAL_MARK1`/`LED_CAL_MARK2` tell the operator which mark is expected, and
  calibration consists of nothing but jogs. Flashing `LED_ACK` over that
  pattern on every small jog would corrupt the one signal the operator depends
  on, in the workflow that is hardest to recover from.
- **`position_calibrated()`** — `position_lift_pct` returns
  `POSITION_LIFT_UNKNOWN` (`0xFF`) when uncalibrated, so `0xFF == 0xFF` reads
  as "nothing changed" and would flash on *every* uncalibrated jog. But
  jogging is the normal way to move an uncalibrated blind and it visibly moves.
  Without this guard the feature is at its most annoying exactly when the
  device is least useful.

A refused move never reaches `MOTION_DONE`, so there is no double-signalling
against the `LED_ERROR` path.

### 3. Files touched

| File | Change |
|---|---|
| `src/main.c` | `s_move_start_pct` static; flashes in `start_move` (two branches), `handle_keypad` (two paths), `APP_EVT_MOTION_DONE` (one) |

Nothing else. `status_led`, `keypad`, `keypad_logic`, `position` and `ramp` are
untouched.

## Testing

**None of this is host-testable.** It lives entirely in `main.c`, which has no
test suite, and the visibility predicate was deliberately not extracted into
`position.c` where `test_position` could have covered it. That was a considered
trade for a smaller diff, and the cost is that every check below is bench-only
and must be performed by a human watching an LED.

Bench acceptance, six checks:

1. At a limit, tap toward that limit → three flashes
2. Uncalibrated, tap Up or Down → five rapid flashes
3. Idle and calibrated, tap Fn → three flashes
4. Hold ~500 ms and release → three flashes
5. Ordinary full travel → **no** flash
6. **Calibration Mode, jog repeatedly → no flashes, `LED_CAL_MARK` pattern
   uninterrupted**

Check 6 is the regression this design could plausibly introduce and is the one
not to skip.

## Relationship to the trace

Every outcome signalled here is *already* recorded by the RTC trace as
`MOVE_NOOP` or `MOVE_REFUSED`. This change adds no new information to the
system; it makes information the device already holds visible to someone
standing in front of it without a laptop. That gap is what cost 2026-08-11.
