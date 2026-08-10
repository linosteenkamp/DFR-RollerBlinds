# Zigbee rejoin and local recovery

**Date:** 2026-08-10
**Status:** approved, not yet implemented
**Repos:** `esp-zb-common` (v0.1.1 → v0.2.0) and `DFR-RollerBlinds`

## Problem

A DFR device removed from zigbee2mqtt can never rejoin, and recovery needs
physical USB access. This cost several hours on 2026-08-10 with a blind
already installed and unreachable by laptop.

The mechanism, confirmed in code: `esp_zb_app_signal_handler` in
`zb_core.c` handles `SKIP_STARTUP`, `DEVICE_FIRST_START`/`DEVICE_REBOOT` and
`BDB_SIGNAL_STEERING`. **`ESP_ZB_ZDO_SIGNAL_LEAVE` falls into the log-only
`default:` case.** The device therefore never restarts BDB steering after a
leave.

Power-cycling does not help, and the reason is worth stating: on reboot the
handler asks `esp_zb_bdb_is_factory_new()`, the ignored leave never cleared
the stack's network state, so it answers "not factory new" and the handler
calls `on_joined()`. The device reports itself joined to a network that has
forgotten it. Meanwhile the coordinator discards its frames
("Data is from unknown device with address 'NNNNN', skipping").

`zb_core.h` already documents the symptom in prose:

> The flag is set on join and never cleared — a coordinator-initiated leave
> is not detected in v0.1.x, so this can report stale true after the device
> is removed from the network.

## Goal

1. A device that receives a leave clears its state and rejoins by itself.
2. A device that is off-network for any reason can be recovered at the
   keypad, with no USB and no laptop.

## Non-goals

- **Detecting that the coordinator is ignoring us** (orphan detection). This
  is the case where the leave was never *received*, and it is deliberately
  out of scope — see [Limitations](#limitations). It needs its own design.
- **OTA resilience on marginal links.** Separate concern, separate spec.
- **Retrofitting the siblings.** The library changes are additive so
  DoorSensor and MoistureTracker keep compiling, but re-pinning and
  re-releasing them is a later decision.

## Two triggers, two paths, one outcome

An earlier draft had both triggers converge on a single handler, by making
the keypad gesture call `esp_zb_bdb_reset_via_local_action()` — which emits
the same `LEAVE`/`RESET` signal a coordinator removal does. That is tidier,
and it is wrong here; see [the API rationale](#library--esp-zb-common-v020)
for why it cannot depend on the join state.

So there are two paths, each already simple:

| Trigger | Path |
|---|---|
| Coordinator removal | `LEAVE` signal → clear state → steer (**new handler**) |
| Keypad gesture | erase `zb_storage` → restart → factory-new → steer (**existing path**) |

Both end in the same place: steering with backoff until it joins. Only the
first needs new signal-handling code; the second reuses the
`DEVICE_FIRST_START` path that already works.

## Design

### Library — `esp-zb-common` v0.2.0

**1. Handle `ESP_ZB_ZDO_SIGNAL_LEAVE`.** Read
`esp_zb_zdo_signal_leave_params_t` via `esp_zb_app_signal_get_params()`.
For both `ESP_ZB_NWK_LEAVE_TYPE_RESET` and `..._REJOIN`: clear `s_joined`,
invoke the app callback, and begin network steering. The two leave types get
the same treatment — in both cases the device is off-network and wants back
on. They are distinguished only in the log line.

**2. Steering backoff.** `ESP_ZB_BDB_SIGNAL_STEERING` currently reschedules
every 1000 ms forever. Replace with a ramp: **1 s, doubling to a 60 s
ceiling**, retrying indefinitely, reset to 1 s on a successful join. An
unjoined mains-powered router retrying every second forever is antisocial on
a shared channel, and after a deliberate removal it may retry for days.

Retrying indefinitely is a deliberate choice over giving up after a window:
the device becomes self-healing, silently rejoining the next time
permit-join opens. The accepted cost is that a device removed *deliberately*
will come back if the network is later opened; correcting that means
removing it again with permit-join closed. Given the alternative failure
costs a ladder, this is the right trade.

**3. New API:**

```c
/* Erase Zigbee persistent state and restart, so the device comes back
 * factory-new and steers for a network. Works regardless of the current
 * join state. Zigbee state only — application NVS (calibration, settings)
 * lives in a different partition and is untouched. Does not return. */
void zb_core_factory_reset(void);
```

Implementation: **always `esp_zb_factory_reset()`**, which erases the
`zb_storage` partition and restarts the device.

The tempting alternative — `esp_zb_bdb_reset_via_local_action()` when
joined, falling back otherwise — must be rejected, and the reason is the
whole point of this feature. Local action "only takes effect when the device
is on a network", and the decision would rest on `zb_core_is_joined()` —
**the very flag that goes stale in the failure this gesture exists to
recover from**. A device that wrongly believes it is joined would take the
local-action path and it would silently do nothing. The gesture must work in
exactly that state, so it cannot depend on that flag.

The cost is a reboot instead of a live re-steer. For a deliberate recovery
action that is acceptable, arguably preferable, and it needs no new code on
the far side: the device boots factory-new and the existing
`DEVICE_FIRST_START` path starts steering.

Erasing `zb_storage` also discards the outgoing NWK frame counter, which
`esp_zb_bdb_reset_via_local_action()` preserves. In practice this is
harmless here — the coordinator has already forgotten the device in every
scenario where the gesture is used.

**4. New optional callback** on `zb_core_cfg_t`:

```c
/* Optional (NULL to skip): the device has left or been removed from the
 * network and is now steering. Zigbee stack context — the lock is already
 * held; do NOT block or call esp_zb_lock_acquire. Enqueue and return. */
void (*on_network_lost)(void);
```

Additive and `NULL`-safe, so the siblings compile unchanged.

**5. Correct the `zb_core_is_joined()` doc comment**, which currently
documents the bug being fixed.

### App — `DFR-RollerBlinds`

**6. `keypad_logic` — new gesture.** `KP_EVT_FACTORY_RESET`, emitted when all
three keys are held together for **5 s**, timed from the third press.

Two suppression rules are required so no existing gesture fires on the way:

- `CHORD_REVERSE` fires only when Fn is **not** down
- `FN_LONG` fires only when Up and Down are **not** down

Without the first, a three-key hold would trigger the motor-reversed chord at
3 s and **wipe the calibration** before ever reaching 5 s.

The gesture fires once per press combination (a latch, like `chord_fired`),
and the keys are suppressed until all three are released.

**7. `main.c`** — `KP_EVT_FACTORY_RESET` calls `zb_core_factory_reset()`.
`on_network_lost` enqueues a new `APP_EVT_ZB_NET_LOST`, whose only job is to
call `refresh_outputs()`.

**8. `status_led` — new `LED_NO_NETWORK`:** 2 s on, 1 s off. The pattern
player's frame is 60 ticks × 50 ms = exactly 3 s, so this is `t < 40` with no
change to the timing engine.

**LED precedence** in `refresh_outputs()` becomes:

1. Identify
2. Calibration mode (awaiting mark 1 / re-home)
3. Calibration mode (awaiting mark 2)
4. **No network** ← new
5. Uncalibrated / Position Unknown
6. Off (calibrated, idle)

Identify and the calibration modes are active user interactions and must
win. At rest, "off the network" outranks "uncalibrated" because an
uncalibrated device is already locked out remotely, so the network state is
the more actionable fault.

## Data flow

**Coordinator removes the device:** leave frame → `LEAVE` signal → clear
`s_joined`, fire `on_network_lost`, start steering → app enqueues → LED shows
`LED_NO_NETWORK` → steering retries with backoff → rejoins when permit-join
next opens → `on_joined()` → LED returns to its resting pattern.

**Operator holds all three keys 5 s:** `KP_EVT_FACTORY_RESET` →
`zb_core_factory_reset()` → `esp_zb_factory_reset()` erases `zb_storage` and
restarts. On boot `esp_zb_bdb_is_factory_new()` is now true, so the existing
`DEVICE_FIRST_START` path starts steering — no new code on that side. The LED
shows `LED_NO_NETWORK` from boot until it joins.

This path works identically whether the device was correctly joined, wrongly
believed itself joined, or knew it was off-network. That is the point: it
consults no state that could be stale.

## Limitations

**This does not fix the 2026-08-10 failure.** The evidence says that device
never *received* its leave: had ZBOSS processed it, the stack would have
cleared itself and steered on the next boot, and it did not. RF was marginal
at the time.

| Scenario | Recovered by |
|---|---|
| Leave received | Item 1 — self-healing, no human |
| Leave missed, keypad reachable | Item 2 — 5-second gesture |
| Leave missed, keypad **not** reachable | **Neither — needs orphan detection** |

Item 1 makes the common case self-healing and item 2 removes USB from
recovery, but a device that misses its leave and cannot be reached is still
stuck. That is the case for a future orphan-detection spec, and it should not
be discovered later as a surprise.

## Testing

**Host tests** (`test/test_keypad/`, `pio test -e native`) on `keypad_logic`:

- all three held 5 s emits `KP_EVT_FACTORY_RESET` exactly once
- released before 5 s emits nothing
- `CHORD_REVERSE` does **not** fire while Fn is down (the calibration-wipe
  guard — the most important test here)
- `FN_LONG` does **not** fire while Up or Down is down
- Up+Down alone still emits `CHORD_REVERSE`; Fn alone still emits `FN_LONG`
  (no regression in the existing gestures)

`zb_core` cannot be host-tested — it is bound to the Zigbee stack. Its
verification is on-device.

**On-device acceptance**, reproducing the failure that motivated this work:

1. Remove the blind from z2m. It should clear, show `LED_NO_NETWORK`, and
   steer — with no power cycle and no USB.
2. Open permit-join. It should rejoin by itself and the LED should return to
   its resting pattern.
3. Confirm calibration and `travel_time` survived the round trip.
4. Hold all three keys 5 s on a healthy device: same sequence, deliberately
   triggered.
5. Confirm the reverse chord and Fn-long still behave when used normally —
   in particular that calibration is **not** wiped by the reset gesture.

## Fleet impact

`esp-zb-common` goes to **v0.2.0**. `DFR-RollerBlinds` re-pins in
`src/idf_component.yml`. DoorSensor and MoistureTracker compile unchanged
against v0.2.0 because every addition is optional, but they gain the fix only
when re-pinned and re-released — a separate decision.
