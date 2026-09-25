# Review fixes: one owner for every motion decision

**Date:** 2026-09-25
**Status:** draft, awaiting approval
**Repos:** `DFR-RollerBlinds` only (the OTA deferral that would need
`esp-zb-common` is deliberately out of scope, see [Deferred](#deferred))
**Ships as:** v2.4.0, on the current pin map. The Fn/LED pin move on branch
`fn-to-d3` becomes v2.5.0 and is rebased on top of this work.

## Problem

A whole-firmware adversarial review on 2026-09-25 (two independent reviewers,
one against the documented standards, one against every spec in
`docs/superpowers/specs/`) found defects that the 74 host tests could not see,
because the code that contains them has no tests: `src/main.c`'s dispatcher and
`src/motion.c`'s ISR.

Most of them share one root cause. **"Is a move in progress?" is answered by
the motion ISR, but acted on by the dispatcher**, and the two disagree for a
window at the end of every move:

```
ISR:         last step ── s_moving = false ── post MOTION_DONE ─────────────────►
Dispatcher:  … busy (Zigbee lock, NVS commit) … handles TAP: motion_is_moving()
             == false → "idle" → start_move() from s_raw, which MOTION_DONE
             has not updated yet …… then handles the late MOTION_DONE
```

Findings this spec closes (review numbering, S = spec axis, T = standards axis):

| # | Finding | Consequence |
|---|---|---|
| S1 | Stale origin: a tap or remote goto queued before `MOTION_DONE` starts a move from the old `s_raw` | Blind drives past its limit; the hard cap doesn't catch it. The late DONE then clears the NVS `moving` flag mid-move, so a power cut boots Calibrated with a wrong position |
| S1b | Same gap, `APP_EVT_CAL_TIMEOUT`: sees "idle", aborts at once; the late DONE then lands outside Calibration Mode and saves the jog's end as a **trusted** position | Violates the jogged-then-aborted → Position Unknown rule |
| S1c | `xQueueSendFromISR` result ignored; `MOTION_DONE` lost when the 16-deep queue is full | Move flag stays set; `s_raw` never updates |
| S3 | `toggle_reversed()` saves `rev` before invalidating span and position, errors unchecked | Power loss in between → new direction with old calibration, still Calibrated; "Open" drives toward and past Closed |
| S4 / T1 | Queue full: `post_kp()` calls `motion_stop()` from the keypad timer task | Breaks the dispatcher-decides rule; races the dispatcher's own `motion_stop()` (can halt at speed, losing steps); and if `HOLD_START` is still queued the jog starts afterwards with no stop at all |
| S6 | No `LED_NO_NETWORK` for up to 60 s after boot | Rejoin spec: "LED shows LED_NO_NETWORK from boot until it joins" |
| S9 / T2 | `app_main` calls `refresh_outputs()` and reads dispatcher state after the dispatcher starts | Stale LED and ConfigStatus if Calibration Mode is entered during the join wait |
| S10 | `status_led_flash` writes two variables unlocked | A flash straight after another can vanish |
| S11 | Three-key reset and Fn long-press refused while moving, silently | Keypad-feedback spec: `LED_ERROR` means "heard you, refusing" |
| S5 | Converter returns `motor_reversed` optimistically | z2m shows a value the device refused |
| S8 / T8 | NVS write results ignored at four sites | See [NVS failure policy](#4-nvs-failure-policy) |
| T3 / S12 | Calibration timeout is 10 min in code, 5 min in spec §6, `CLAUDE.md`, `app_event.h`, `DEVELOPER_GUIDE.md` | Docs wrong |
| T6 | `motion.c` re-implements `ramp.c` (`isr_interval`) plus the stop maths in `motion_stop()`; none of it tested | The tested ramp code is not the code that runs |
| T5, T7, smells | `CLAUDE.md` constants block not verbatim; converter outputs incomplete; `CONTEXT.md` drift; triplicated abort sequence; `s_cal_mode` mirroring `s_pos.cal` | Docs and maintainability |

## Goal

1. Exactly one place decides whether a move is in progress, starts or stops
   motion, and persists state: a pure, host-tested core.
2. No event the correctness of motion depends on can be lost.
3. Every review scenario above exists as a host test that failed before its fix.
4. Flash never trusts more than RAM.

## Non-goals

- **OTA reboot while moving** (review S2). See [Deferred](#deferred).
- **Renaming** `spd`, `APP_EVT_ZB_SET_SPEED`, `PROF_MOVE`, `cal_dev` and similar.
  Renaming NVS keys breaks stored data on installed units; the rest isn't worth
  the churn in this change.
- **Changing gestures, timing or motion constants.** `HOLD_MS` and the
  ordinary-press-becomes-a-tiny-jog question stay open, as before.
- **The pin move.** Its doc drift (`src/main.c:36-40` comments,
  `DEVELOPER_GUIDE.md:232`, `tools/pinwalk`) is fixed on `fn-to-d3`, together
  with renumbering that branch's "v2.4.0 pin map" references to v2.5.0.

## Design

### 1. A pure dispatcher core behind ports

New module **`src/ctl.c` / `include/ctl.h`**, pure C, host-tested like
`position` and `ramp`. It owns all the state `main.c` holds today:

- `position_t pos`, the raw step frame (`s_raw`), reversed, travel time,
  identifying, the calibration-session flags (moved, abort-pending)
- the **move-active flag**, the **jog key**, the parked Zigbee goto
- the start-of-move lift % used for dead-zone feedback

`s_cal_mode` is dropped: it mirrors `pos.cal != POS_CAL_NONE` and every exit
had to keep the two in sync by hand. The abort sequence, written out three
times today (one copy missing its timer stop), becomes one function.

Entry points:

```c
void ctl_init(ctl_t *c, const ctl_ports_t *ports, const blind_store_data_t *boot);
void ctl_handle(ctl_t *c, const app_event_t *ev);
led_pattern_t ctl_led_pattern(const ctl_t *c);   /* see §5 */
```

Everything with a side effect goes through **`ctl_ports_t`**, a struct of
function pointers:

| Group | Ports |
|---|---|
| Motion | `motion_start(from, target, profile, cap) → esp_err_t`, `motion_stop()`, `motion_steps()`, `motion_set_reversed(bool)` |
| Persistence | `save_move_flag(bool)`, `save_span(bool, int32)`, `save_position(bool, int32)`, `save_reversed(bool)`, `save_travel_time(u16)`, all `→ esp_err_t` |
| Zigbee | `set_motion_allowed(bool)`, `set_operational(bool)`, `report_lift(u8)`, `report_mode(bool)`, `report_travel_time(u16)`, `net_joined() → bool`, `factory_reset()` |
| Operator | `led_set(pattern)`, `led_flash(pattern)`, `key_held(key) → bool` |
| Timers | `cal_timer_start()`, `cal_timer_stop()`, `report_timer_start()`, `report_timer_stop()` |
| Diagnostics | `trace(code, a, b)` |

`main.c` becomes wiring: GPIO map, constants, module init, the ports table
pointing at the real modules, queue and timer creation, the dispatcher loop,
and `app_main`. The concurrency rule in `CLAUDE.md` is unchanged in spirit and
becomes structural: only the dispatcher task calls `ctl_handle`, and only
`ctl_handle` calls the ports.

**Host tests** use a fake ports table that records every call in order and can
be told to fail a named save. That makes the ordering bugs (S1, S1b, S3), the
NVS policy and the start-up sync testable. It also covers the `blind_store`
fake that design spec §8 asked for, and `status_led` pattern selection.

### 2. The dispatcher owns "move in progress"

`ctl` sets `move_active` when `motion_start` returns `ESP_OK` and clears it
**only** when it handles `APP_EVT_MOTION_DONE`. Every place that asks "moving?"
(eleven `motion_is_moving()` calls in today's `main.c`) asks `move_active`.

During the end-of-move gap, `move_active` is still true, so:

- a tap is treated as a stop request, and `motion_stop()` on an ISR that has
  already finished is a no-op (it returns early on `!s_moving`);
- a Zigbee goto is parked and runs from the correct origin once DONE is handled;
- a calibration timeout sets abort-pending, and the abort happens on DONE with
  the jog accounted for (fixes S1b).

`motion_is_moving()` stays in `motion.h` for the motion layer's own guards but
is no longer called from `ctl` or `main.c`.

Invariant this creates: **at most one `MOTION_DONE` is ever outstanding**,
because `ctl` never starts a move while `move_active` is set.

### 3. `MOTION_DONE` cannot be lost

The ISR posts `MOTION_DONE` to its **own one-slot queue**. The dispatcher
blocks on a **FreeRTOS queue set** containing the main queue and the done queue
(`configUSE_QUEUE_SETS` is 1 in this ESP-IDF). By the invariant above, the
one-slot queue is always empty when the ISR posts, so the send cannot fail.
The ISR still checks the result and, on failure, records a new
`TRC_DONE_POST_FAILED` trace code: a "can't happen" that would be visible in
the next boot dump rather than silent.

The queue set returns events in arrival order, so event ordering is unchanged.

**The hold dead-man moves into `ctl`.** `keypad.h` gains
`bool keypad_key_held(key_id_t k)`, returning the key's debounced
(`key_filter`) output. It's a single aligned `bool` written by the 5 ms poll,
safe to read from the dispatcher. `ctl` remembers which key started a jog, and
at the end of **every** `ctl_handle` call checks: jog running and its key no
longer held → `motion_stop()`, once. A full queue means the dispatcher is
behind, so it has events to process and catches a dropped `HOLD_END` within one
event. The 1 s report tick is the backstop. This also covers "`HOLD_END`
dropped while `HOLD_START` is still queued": the jog starts, and the same
`ctl_handle` call that started it sees the key already released and stops it.

`post_kp()` stops calling `motion_stop()`; on a full queue it only traces the
drop, as it does now.

### 4. NVS failure policy

One rule: **a failed write refuses the action and flashes `LED_ERROR`; flash
never ends up trusting more than RAM.**

| Site | On failure |
|---|---|
| Move start: `save_move_flag(true)` | Refuse the move, before `motion_start`. Without the flag a power cut mid-move would boot Calibrated with a wrong position |
| Motor Reversed | New order: `save_span(false)` and `save_position(false)` first, then `save_reversed(new)`. If an invalidation fails: refuse, direction unchanged. If `save_reversed` fails: revert the direction in RAM; calibration is wiped either way, so the unit is uncalibrated in its old direction, which is safe |
| Mark 2 accepted: `save_span`, `save_position` | Treat as a rejected mark: `LED_ERROR`, stay in Calibration Mode waiting for mark 2, operator retries |
| Calibration abort policy | Log (trace) only. The first jog of the session already saved the position untrusted, so flash is on the safe side |
| `MOTION_DONE` position save | Unchanged: the move flag is left set so the next boot re-homes |

### 5. Boot sequence

- The LED choice currently inside `refresh_outputs()` becomes
  `ctl_led_pattern()`, pure and tested.
- `app_main` initialises `ctl` and sets the LED from `ctl_led_pattern()`
  **before** creating the dispatcher task, while it is the only task touching
  state. A calibrated but unjoined unit shows `LED_NO_NETWORK` from the first
  moment (fixes S6).
- After `xTaskCreate(dispatcher…)`, `app_main` touches no `ctl` state. When the
  join wait ends (joined or 60 s timeout) it posts a new
  **`APP_EVT_BOOT_SYNC`**. `ctl` handles it by reporting Mode and travel time
  and refreshing all outputs (fixes S9/T2).
- `ota_client_mark_valid()` stays last in `app_main`; it touches no `ctl` state.

### 6. Operator feedback

- Three-key factory reset refused while a move is active → `LED_ERROR`.
- Fn long-press refused while a move is active → `LED_ERROR`.
- `status_led_flash()` writes the pattern and its tick inside a
  `portENTER_CRITICAL` section, and `tick_cb` reads them inside one (fixes S10).

### 7. One ramp implementation

`ramp.c` becomes the only ramp maths:

- A `RAMP_HOT` macro expands to `IRAM_ATTR` in ESP-IDF builds and to nothing in
  the native build. The per-step functions carry it.
- Two new pure functions carry the stop logic now duplicated in `motion.c`:
  - `int32_t ramp_stop_index(const ramp_plan_t *r, int32_t idx)`: where to halt
    when a stop is requested at step `idx` (decelerate over as many steps as
    the move is into its ramp, capped by what remains);
  - `uint32_t ramp_interval_us_stopping(const ramp_plan_t *r, int32_t idx, int32_t stop_at)`:
    the interval for step `idx` during that deceleration.
- `motion.c` deletes `isr_interval` and the maths in `motion_stop()` and calls
  these. The header comments in `ramp.c`/`ramp.h` that claim the ISR already
  calls them become true.
- **Verification:** the build's linker map must show these functions in IRAM
  (`.iram0.text`). That is the reason the copy existed: flash writes during an
  OTA download or NVS commit must never stall stepping.

### 8. Converter

`tzMotorReversed.convertSet` writes, then reads `windowCoveringMode` back and
returns no optimistic state, the same pattern `tzTravelTime` already uses.
Deploying the converter to the z2m box is a separate step that the owner
triggers, with the current converter backed up first; it restarts zigbee2mqtt.

### 9. Documentation

- Calibration timeout **stays at 10 min** (commit `747e2fc` doubled it so a
  full-span calibration jog could not outlive it). Spec §6, `CLAUDE.md:147`,
  `include/app_event.h:24` and `DEVELOPER_GUIDE.md:84` change to 10 min.
- Design spec §5 gains a **known deviation** note: the library reboots as soon
  as an OTA download finishes, even mid-move, which can cause Position Unknown;
  deferral is tracked as future work.
- `CLAUDE.md`: architecture and module table gain `ctl`; the concurrency
  section describes ports and the queue set; the constants block is re-copied
  verbatim; the converter row lists `travel_time`.
- `CONTEXT.md`: Span is also wiped by a Motor Reversed toggle; Position Unknown
  also follows a jogged-then-aborted calibration.
- `ramp.c`/`ramp.h` header comments made true (§7).

## Order of work

1. **Pure refactor.** Move the dispatcher logic into `ctl` behind the ports
   with **no behaviour change**, with characterisation tests pinning today's
   behaviour: gesture matrix, calibration / Re-home / abort, lockout, Zigbee
   preemption, dead-zone ACK, travel time. **Bench gate:** flash `bench2`,
   run the existing keypad and calibration bench checks.
2. **Fixes, test-first, one commit each** (red test reproducing the review
   scenario, then the fix): S1, S1b, dead-man (S4 core side), S3 and the NVS
   policy, boot sync and S6, refusal flashes.
3. **Outside the core:** done queue and queue set (S1c), `post_kp` (S4/T1),
   ramp consolidation with the IRAM check (T6), `status_led` critical section
   (S10).
4. **Converter** (S5) and **documentation** (§9).
5. **Release:** bench checklist on `bench2`, then an unattended soak with the
   start time noted, then tag v2.4.0 and publish the OTA, then update
   `blinds lounge side` from z2m when the owner is present to Re-home if the
   OTA reboot lands mid-move.

## Testing

**Host (`pio test -e native`)**, new suite `test/test_ctl/`, one test per
scenario, each written to fail against the pre-fix core:

| Scenario | Expected |
|---|---|
| TAP handled after the ISR finished but before `MOTION_DONE` | Treated as stop; no `motion_start`; after DONE, position = DONE steps |
| GOTO in the same gap | Parked; `motion_start` from the DONE position once DONE is handled |
| CAL_TIMEOUT in the gap after a jog | Abort deferred to DONE; position saved untrusted; not Calibrated |
| `HOLD_END` dropped, key released, other events keep arriving | `motion_stop` on the next event |
| `HOLD_START` handled with the key already released | Jog started and stopped in the same call |
| Motor Reversed toggle | Port call order: `save_span(false)`, `save_position(false)`, then `save_reversed` |
| Each NVS failure site in §4 | Refusal, `LED_ERROR`, and the save calls prove flash never trusts more than RAM |
| Boot, calibrated, not joined | `ctl_led_pattern()` = `LED_NO_NETWORK` |
| `APP_EVT_BOOT_SYNC` | Mode and travel time reported; outputs refreshed |
| Factory reset / Fn long-press while moving | `LED_ERROR` flash |

Plus `test_ramp` cases for `ramp_stop_index` and `ramp_interval_us_stopping`:
stop during acceleration, cruise and deceleration; stop at step 0; stop within
one step of the end; a triangle profile.

**Bench (`bench2`)**, after step 1 and again before release: the existing
keypad-feedback and calibration checks; a tap exactly as a travel ends (the S1
scenario, best effort by hand); a hold released during heavy Zigbee traffic;
Motor Reversed toggle from z2m and the keypad; `LED_NO_NETWORK` visible from
power-on with the coordinator off; the linker map IRAM check.

## Deferred

**OTA reboot while moving (review S2).** Spec §5 and §7 say the update applies
"only when idle … OTA can never cause Position Unknown". `esp-zb-common`'s
`ota_client.c` calls `esp_restart()` as soon as the download finishes, and the
app has no hook to defer it. `DEVELOPER_GUIDE.md` already accepts the result as
recoverable with a Re-home. Fixing it needs a library change and release
(an app-supplied "may reboot now?" callback or a deferred-restart API), then a
re-pin here. Recorded as a known deviation in spec §5 (§9 above) and in
project memory, so it is picked up next time `esp-zb-common` is touched.
