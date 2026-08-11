# Keypad Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a keypad gesture that produces no motion say so, instead of being indistinguishable from a dead device.

**Architecture:** Five call sites in `src/main.c`'s dispatcher flash the existing transient LED patterns — `LED_ACK` where there was simply nothing to do, `LED_ERROR` where the device is refusing. The dead-zone case (a jog too small to see) is detected by comparing the reported lift percentage before and after the move; unchanged means invisible to the operator and to z2m alike.

**Tech Stack:** C11, ESP-IDF 5.5.1 via PlatformIO, ESP32-C6 (`seeed_xiao_esp32c6`).

**Spec:** [docs/superpowers/specs/2026-08-11-keypad-feedback-design.md](../specs/2026-08-11-keypad-feedback-design.md)

## Global Constraints

- **Only `src/main.c` may be modified.** `status_led`, `keypad`, `keypad_logic`, `position`, `ramp`, `motion`, `trace` and every test file stay untouched. `esp-zb-common` is off limits.
- **No new LED patterns.** `LED_ACK` (three flashes) and `LED_ERROR` (five rapid) already exist as transient overlays via `status_led_flash()`.
- **No gesture-timing changes.** `HOLD_MS` stays 400, `LONG_MS` 3000, `RESET_MS` 5000. Raising the threshold would move the dead zone rather than remove it, and all 15 `test_keypad` cases must keep passing untouched.
- **No new tuning constants.** The dead-zone threshold is "the lift percentage did not change" — the device's own resolution.
- **No behaviour changes beyond adding flashes.** Every existing control-flow path, ordering and side effect stays exactly as it is.
- **This is not host-testable.** `main.c` has no test suite and the spec deliberately declined to extract the predicate into `position.c`. Acceptance is bench-only.
- Commit messages are prose explaining *why*, not bullet lists.
- All work on branch **`keypad-feedback`**, cut from `main` at the start of Task 1, merged `--no-ff` in Task 3. Do not push or tag.

## File Structure

| File | Change |
|---|---|
| `src/main.c` (modify) | `s_move_start_pct` static; flashes in `start_move` (2), `handle_mark` (1), `handle_keypad` (1), `APP_EVT_MOTION_DONE` (1) |

Nothing else. No files created.

---

### Task 1: The four synchronous signals

The four cases where the dispatcher already knows, at the moment of the
gesture, that nothing will move.

**Files:**
- Modify: `src/main.c` — `start_move`, `handle_mark`, `handle_keypad`

**Interfaces:**
- Consumes: `status_led_flash(led_pattern_t)` and the `LED_ACK` / `LED_ERROR` enumerators from `include/status_led.h`. Both already used elsewhere in `main.c`, so no new include is needed.
- Produces: nothing. Task 2 is independent of this task's changes.

- [ ] **Step 0: Cut the branch**

```bash
git checkout main
git checkout -b keypad-feedback
```

- [ ] **Step 1: Flash on the no-op and refused branches of `start_move`**

Replace `start_move` in `src/main.c` entirely:

```c
static void start_move(int32_t target, const motion_profile_t *prof)
{
    if (target == s_raw) {
        TRACE(TRC_MOVE_NOOP, target, s_pos.closed_steps);
        status_led_flash(LED_ACK);   /* heard you; already there */
        refresh_outputs();   /* already there — keep reports honest, no NVS churn */
        return;
    }
    int32_t cap = hard_cap();          /* hoisted so a refusal can record it */
    TRACE(TRC_MOVE_START, s_raw, target);
    blind_store_set_move_flag(true);
    esp_err_t err = motion_start(s_raw, target, prof, cap);
    if (err != ESP_OK) {
        blind_store_set_move_flag(false);
        TRACE(TRC_MOVE_REFUSED, err, cap);
        status_led_flash(LED_ERROR);   /* refusing — needs attention */
        ESP_LOGW(TAG, "move refused: %s", esp_err_to_name(err));
    } else {
        esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US);
    }
}
```

Note this function is shared with the Zigbee path, so a z2m `goto` to the
current position also flashes. That is accepted in the spec — harmless, and
separating the callers would mean plumbing caller identity through for no
benefit.

- [ ] **Step 2: Flash on the inert Fn tap in `handle_mark`**

In `handle_mark`, change **only** the `!s_cal_mode` early return:

```c
    if (!s_cal_mode) {
        status_led_flash(LED_ACK);   /* heard you; nothing to mark */
        return;                                          /* idle taps inert */
    }
```

**Do not touch the other early return** (`if (motion_is_moving()) {
motion_stop(); return; }`). That one stops a move, which the operator can
plainly see, and flashing there would signal "nothing happened" when something
very much did.

- [ ] **Step 3: Flash on the uncalibrated Up/Down tap in `handle_keypad`**

In `handle_keypad`'s `KP_EVT_TAP` case, add an `else` to the existing
`if (cal_dev)`:

```c
    case KP_EVT_TAP:
        if (motion_is_moving()) { motion_stop(); break; }   /* any tap stops */
        if (e.key == KEY_FN) { handle_mark(); break; }
        if (cal_dev) {                                       /* full travel */
            goto_pct(e.key == KEY_UP ? 0 : 100);
        } else {
            status_led_flash(LED_ERROR);   /* lockout: needs calibration */
        }
        break;
```

The `if (e.key == KEY_FN)` line above already `break`s, so `KEY_FN` can never
reach this `else` — the lockout signal is for Up and Down only, which is
correct, because an Fn tap outside Calibration Mode is benign and gets
`LED_ACK` from Step 2.

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 5: Verify no host regressions**

Run: `pio test -e native`
Expected: PASS — five suites, 59 tests. Nothing here touches tested code, so
any failure means something was changed that should not have been.

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -F - <<'EOF'
Signal the keypad gestures that deliberately do nothing

A blind spends most of its life parked at a limit, and at a limit the key
pointing further into it returns early from start_move without a sound. Add an
uncalibrated device, where taps are inert by design, and a move refused by the
hard-cap watchdog, which logs only to a console nobody is attached to, and
"the keypad is dead" becomes the only conclusion available to someone standing
in front of the blind. It was the conclusion drawn, repeatedly, across a day
of chasing a fault that did not exist.

LED_ACK where there was simply nothing to do, LED_ERROR where the device is
refusing and wants attention. Both patterns already existed and were already
used to acknowledge calibration marks; they were just never wired to the paths
that needed them most.

The motion_is_moving early return in handle_mark is deliberately left silent:
it stops a move, which is visible, and claiming "nothing happened" there would
be worse than saying nothing at all.
EOF
```

---

### Task 2: The dead-zone signal

A hold just past `HOLD_MS` produces a jog too short to see. Measured on the
bench: a 575 ms press moved 464 steps of a 109 192-step span — 0.4%, and
invisible.

**Files:**
- Modify: `src/main.c` — new static, `start_move`, `APP_EVT_MOTION_DONE`

**Interfaces:**
- Consumes: `uint8_t position_lift_pct(const position_t *p)` from `include/position.h` — returns 0 (Open) to 100 (Closed), or `POSITION_LIFT_UNKNOWN` (`0xFF`) when uncalibrated. Already used in `refresh_outputs`.
- Produces: nothing.

- [ ] **Step 1: Add the static**

In `src/main.c`, alongside the other file-statics (next to `s_raw`), add:

```c
static uint8_t       s_move_start_pct;  /* lift % when the current move began */
```

- [ ] **Step 2: Record the lift % at move start**

In `start_move`, capture it immediately before `motion_start` — after the
no-op branch has already returned, so it is only set for moves that actually
start:

```c
    int32_t cap = hard_cap();          /* hoisted so a refusal can record it */
    s_move_start_pct = position_lift_pct(&s_pos);
    TRACE(TRC_MOVE_START, s_raw, target);
```

- [ ] **Step 3: Compare at move completion**

In `dispatcher_task`'s `case APP_EVT_MOTION_DONE:`, inside the **`else`**
branch (the non-calibration path), after the existing
`blind_store_save_position` line:

```c
            } else {
                position_set_current(&s_pos, position_clamp(&s_pos, ev.steps));
                perr = blind_store_save_position(s_pos.pos_known, s_pos.cur_steps);
                /* Dead-zone feedback: a move too small to change the reported
                 * lift % is invisible to the operator and to z2m alike, so it
                 * is indistinguishable from a dead key unless we say so. */
                if (position_calibrated(&s_pos) &&
                    position_lift_pct(&s_pos) == s_move_start_pct) {
                    status_led_flash(LED_ACK);
                }
            }
```

**Placement refinement over the spec, deliberate:** the spec expressed the
guard as `!s_cal_mode && position_calibrated(...)`. Putting the check inside
the existing `else` of `if (s_cal_mode)` achieves the same thing structurally
and is strictly more robust — the `s_cal_abort_pending` path *inside* that
block sets `s_cal_mode = false`, so a condition evaluated afterwards would see
the mutated value and could fire on what was in fact a calibration jog. The
`else` branch cannot.

**The `position_calibrated()` guard is load-bearing.** `position_lift_pct`
returns `POSITION_LIFT_UNKNOWN` (`0xFF`) when uncalibrated, so `0xFF == 0xFF`
reads as "nothing changed" and would flash on *every* uncalibrated jog. But
jogging is the normal way to move an uncalibrated blind and it visibly moves,
so without this guard the feature is at its most annoying exactly when the
device is least useful.

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS, with no warning about `s_move_start_pct` being unused or
possibly-uninitialised.

- [ ] **Step 5: Verify no host regressions**

Run: `pio test -e native`
Expected: PASS — five suites, 59 tests.

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -F - <<'EOF'
Acknowledge a jog too small to see

A press a little over HOLD_MS becomes a hold, and a hold that short jogs for a
few hundred milliseconds. Measured on the bench, a 575 ms press moved 464 steps
of a 109 192-step span: four tenths of one percent, which nobody can see and
z2m does not report. The operator reads it as a dead key, which is what
happened.

The threshold for "too small to see" is the lift percentage not changing. That
is the device's own resolution rather than an invented constant, and it is the
same number z2m would have to notice before it could tell anyone.

Two guards matter. The check sits in the non-calibration branch because the LED
is the entire interface during calibration and flashing over LED_CAL_MARK would
corrupt the one signal the operator depends on. And it requires a calibrated
device, because an uncalibrated lift reads 0xFF, which compares equal to itself
and would otherwise flash on every uncalibrated jog — the normal way to move a
blind that has not been calibrated yet.
EOF
```

---

### Task 3: Bench acceptance and merge

**Files:** none — this task produces evidence.

**Interfaces:**
- Consumes: the complete firmware from Tasks 1-2.
- Produces: a verified feature.

Nothing in this change is reachable by host tests. Every check below needs a
human watching an LED on a real board.

- [ ] **Step 1: Confirm the host suite is green**

Run: `pio test -e native`
Expected: PASS — `test_position`, `test_ramp`, `test_keypad`, `test_key_filter`,
`test_trace_ring`, 59 tests.

- [ ] **Step 2: Flash a calibrated bench board**

```bash
PORT=/dev/$(ls /dev/ | grep -i "^cu\.usbmodem" | head -1)
echo "using $PORT"
pio run -e seeed_xiao_esp32c6_zigbee -t upload --upload-port "$PORT"
```

Do not use a bare `pio run -t upload` without a port: with nothing enumerated,
PlatformIO auto-detect falls back to `/dev/cu.Bluetooth-Incoming-Port` and
reports "Failed to connect to ESP32-C6", which reads like a board fault when
it is an absent port. If `ls` finds nothing, the XIAO often needs a nudge to
enumerate behind a USB hub: hold **B**, tap **R**, release **B**.

The board must be **calibrated** for checks 3, 5, 6 and 7. If it is not,
calibrate it first: Fn long-press, jog to Open, Fn tap, jog to Closed, Fn tap.

- [ ] **Step 3: At a limit, tap toward that limit**

Drive the blind to a limit, then tap the key pointing further into it.
Expected: **three flashes**, no motion.

- [ ] **Step 4: Uncalibrated, tap Up or Down**

Requires an uncalibrated board — either a fresh one, or wipe calibration with
the Up+Down chord (which sets `motor_reversed` and wipes the span).
Expected: **five rapid flashes**, no motion.

- [ ] **Step 5: Idle and calibrated, tap Fn**

With the blind stopped and not in Calibration Mode, tap Fn.
Expected: **three flashes**, no motion.

- [ ] **Step 6: Hold about half a second and release**

Away from a limit, press and hold for roughly 500 ms, then release.
Expected: a barely-perceptible jog, then **three flashes** on release.

- [ ] **Step 7: An ordinary full travel**

Away from a limit, quick-tap Up or Down.
Expected: a full travel and **no flash at all**.

- [ ] **Step 8: Calibration Mode is not disturbed — do not skip this**

Enter Calibration Mode (Fn long-press) and jog repeatedly with short holds.
Expected: **no flashes**, and the `LED_CAL_MARK1` / `LED_CAL_MARK2` blink
pattern continues uninterrupted throughout.

This is the regression this change could plausibly introduce, and it would
damage the workflow that is hardest to recover from. If flashes appear here,
the guard placement in Task 2 Step 3 is wrong — stop and fix it.

- [ ] **Step 9: Merge**

```bash
git checkout main
git merge --no-ff keypad-feedback
git branch -d keypad-feedback
```

- [ ] **Step 10: Stop and hand back**

Do **not** push or tag. Report that the branch is merged locally and the
feature is bench-verified.
