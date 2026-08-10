# Zigbee Rejoin and Local Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A DFR device that is removed from its Zigbee network rejoins by itself, and any device can be recovered at the keypad without USB.

**Architecture:** Two changes in two repos. `esp-zb-common` gains a handler for the `ESP_ZB_ZDO_SIGNAL_LEAVE` signal it currently ignores, steering backoff, and a `zb_core_factory_reset()` entry point. `DFR-RollerBlinds` gains a three-key 5-second keypad gesture that calls it, and an LED pattern that shows the device is off-network.

**Tech Stack:** C / ESP-IDF via PlatformIO, Unity host tests (`pio test -e native`), ESP Zigbee SDK 1.6.x.

**Spec:** [docs/superpowers/specs/2026-08-10-zigbee-rejoin-recovery-design.md](../specs/2026-08-10-zigbee-rejoin-recovery-design.md)

**Repos:**
- `/Users/lino/Developer/499/esp-zb-common` — Tasks 1-2 (library, v0.1.1 → v0.2.0)
- `/Users/lino/Developer/499/DFR-RollerBlinds` — Tasks 3-4 (app)

## Global Constraints

- **Library callbacks run in Zigbee stack context with the lock already held.** `on_network_lost` must only enqueue and return — never block, never call `esp_zb_lock_acquire`. Same rule the existing `action_handler` and `on_joined` follow.
- **`zb_core_factory_reset()` must NEVER branch on `zb_core_is_joined()`.** That flag is exactly what goes stale in the failure this recovers from; a device wrongly believing itself joined would take a path that silently does nothing. Always `esp_zb_factory_reset()`.
- **Zigbee state only.** Nothing in this plan may touch the `nvs` partition, where calibration, position and `travel_time` live. `esp_zb_factory_reset()` erases only `zb_storage`.
- **Library additions must be optional and `NULL`-safe** — DoorSensor and MoistureTracker consume this component and must keep compiling against v0.2.0 untouched.
- Steering retry: **1 s doubling to a 60 s ceiling**, indefinitely, reset to 1 s on join.
- Reset gesture: **all three keys, 5 s**, timed from the third press.
- `LED_NO_NETWORK`: **2 s on, 1 s off**.
- Host tests must pass in both repos (`pio test -e native`). DFR-RollerBlinds is 36 cases before this work.
- DFR-RollerBlinds firmware build must stay green: `pio run -e seeed_xiao_esp32c6_zigbee`.

### Deliberate behaviour change — read before Task 3

`keypad_logic` today **intentionally** lets `FN_LONG` fire during an Up+Down chord. The code says so (`"Fn is independent of the Up/Down chord — checked first so a chord attempt can never starve FN_LONG"`) and there is a passing test asserting it, `test_fn_long_fires_during_chord` at `test/test_keypad/test_keypad.c:122`.

The spec reverses this. That test is **replaced, not deleted** — it becomes the test for the new behaviour. Task 3 does this explicitly. Do not treat the removal as accidental.

The reason matters: without suppression, holding all three keys fires `CHORD_REVERSE` at 3 s, which **wipes the calibration**, before the 5 s reset is ever reached.

### Outward-facing step

Task 2 pushes a `v0.2.0` tag to the public `esp-zb-common` repo. Confirm with the user before pushing; committing and tagging locally is fine without asking.

---

### Task 1: Handle the LEAVE signal and back off steering

**Repo:** `/Users/lino/Developer/499/esp-zb-common`

**Files:**
- Modify: `include/zb_core.h`
- Modify: `src/zb_core.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `on_network_lost` callback field on `zb_core_cfg_t`; `zb_core_is_joined()` becomes truthful after a leave.

There are no host tests for `zb_core` — it is bound to the Zigbee stack. Verification is the library's compile-proof example.

- [ ] **Step 1: Add the callback to the config struct**

In `include/zb_core.h`, add to `zb_core_cfg_t` immediately after the `on_joined` field:

```c
    /* Optional (NULL to skip): the device has left or been removed from the
     * network and is now steering for a new one. Zigbee stack context — the
     * lock is already held; do NOT block, and do NOT call
     * esp_zb_lock_acquire (self-deadlock). Enqueue and return. */
    void (*on_network_lost)(void);
```

- [ ] **Step 2: Correct the `zb_core_is_joined()` doc comment**

It currently documents the bug being fixed. Replace the comment above `zb_core_is_joined` with:

```c
/* True while the device is joined to a network. Set on join, cleared when the
 * device leaves or is removed (ESP_ZB_ZDO_SIGNAL_LEAVE). */
```

- [ ] **Step 3: Add steering backoff state**

In `src/zb_core.c`, add above `steering_alarm_cb`:

```c
/* Steering retry backoff. An unjoined mains-powered router retrying every
 * second forever is antisocial on a shared channel, and after a deliberate
 * removal it may retry for days. */
#define STEER_DELAY_MIN_MS 1000U
#define STEER_DELAY_MAX_MS 60000U

static uint32_t s_steer_delay_ms = STEER_DELAY_MIN_MS;
```

And add below `steering_alarm_cb`:

```c
static void schedule_steering(void)
{
    ESP_LOGW(TAG, "steering retry in %ums", (unsigned)s_steer_delay_ms);
    esp_zb_scheduler_alarm(steering_alarm_cb,
                           (uint8_t)ESP_ZB_BDB_MODE_NETWORK_STEERING,
                           s_steer_delay_ms);
    s_steer_delay_ms *= 2U;
    if (s_steer_delay_ms > STEER_DELAY_MAX_MS) {
        s_steer_delay_ms = STEER_DELAY_MAX_MS;
    }
}
```

- [ ] **Step 4: Reset the backoff on a successful join**

In `on_joined()`, add as the first line of the function body:

```c
    s_steer_delay_ms = STEER_DELAY_MIN_MS;
```

- [ ] **Step 5: Use the backoff in the steering-failure path**

In `esp_zb_app_signal_handler`, in the `ESP_ZB_BDB_SIGNAL_STEERING` case, replace the whole `else` branch:

```c
        } else {
            ESP_LOGW(TAG, "steering failed (%d)", err);
            schedule_steering();
        }
```

- [ ] **Step 6: Handle the LEAVE signal**

In the same `switch`, add a new case immediately before `default:`:

```c
    case ESP_ZB_ZDO_SIGNAL_LEAVE: {
        /* Without this the signal fell into default: and was only logged, so
         * the device never re-steered — and because the stack's network state
         * was never cleared, esp_zb_bdb_is_factory_new() stayed false on the
         * next boot and the device reported itself joined to a network that
         * had forgotten it. */
        const esp_zb_zdo_signal_leave_params_t *lp =
            (const esp_zb_zdo_signal_leave_params_t *)
                esp_zb_app_signal_get_params(p_sg_p);
        unsigned leave_type = lp ? lp->leave_type
                                 : (unsigned)ESP_ZB_NWK_LEAVE_TYPE_RESET;
        ESP_LOGW(TAG, "left the network (leave_type %u) — steering", leave_type);
        s_joined = false;
        s_steer_delay_ms = STEER_DELAY_MIN_MS;
        if (s_cfg.on_network_lost) {
            s_cfg.on_network_lost();   /* stack context: lock already held */
        }
        /* RESET and REJOIN get the same treatment: either way the device is
         * off-network and wants back on. */
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    }
```

- [ ] **Step 7: Verify the library still compiles**

Run: `pio run -d examples/minimal_router`
Expected: SUCCESS.

- [ ] **Step 8: Verify the host tests still pass**

Run: `pio test -e native`
Expected: PASS (the `test_debounce` suite; unaffected but must not regress).

- [ ] **Step 9: Commit**

```bash
git add include/zb_core.h src/zb_core.c
git commit -m "zb_core: handle LEAVE and back off steering retries

ESP_ZB_ZDO_SIGNAL_LEAVE fell into the log-only default case, so a device
removed from its network never restarted BDB steering. Worse, the stack's
network state was never cleared, so esp_zb_bdb_is_factory_new() stayed false
on the next boot and the device reported itself joined to a network that had
forgotten it — power-cycling could not recover it.

Now the device clears its joined flag, tells the app via the new optional
on_network_lost callback, and steers for a network again. Retries back off
from 1s to a 60s ceiling rather than hammering the channel every second
forever, and reset to 1s on a successful join."
```

---

### Task 2: Add `zb_core_factory_reset()` and release v0.2.0

**Repo:** `/Users/lino/Developer/499/esp-zb-common`

**Files:**
- Modify: `include/zb_core.h`
- Modify: `src/zb_core.c`
- Modify: `idf_component.yml`
- Modify: `README.md`

**Interfaces:**
- Consumes: `s_joined` handling from Task 1.
- Produces: `void zb_core_factory_reset(void);` and the `v0.2.0` tag that Task 4 pins to.

- [ ] **Step 1: Declare the API**

In `include/zb_core.h`, add after `zb_core_is_joined`:

```c
/* Erase Zigbee persistent state and restart, so the device comes back
 * factory-new and steers for a network. Works regardless of the current join
 * state. Zigbee state only — application NVS (calibration, settings) lives in
 * a different partition and is untouched. Does not return. */
void zb_core_factory_reset(void);
```

- [ ] **Step 2: Implement it**

In `src/zb_core.c`, add after the `zb_core_is_joined` definition:

```c
void zb_core_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset: erasing Zigbee state and restarting");
    /* Always the full erase-and-restart, never
     * esp_zb_bdb_reset_via_local_action(). That call "only takes effect when
     * the device is on a network", so choosing it would mean consulting
     * zb_core_is_joined() — the very flag that goes stale in the failure this
     * function exists to recover from. A device wrongly believing itself
     * joined would then silently do nothing. */
    esp_zb_factory_reset();
}
```

- [ ] **Step 3: Bump the component version**

In `idf_component.yml`, change the first line:

```yaml
version: "0.2.0"
```

- [ ] **Step 4: Document the new interface in the README**

In `README.md`, find the section listing the `zb_core` interface and add these two entries alongside the existing ones:

```markdown
- `void zb_core_factory_reset(void)` — erase Zigbee state and restart, so the
  device comes back factory-new and steers for a network. Works whether or not
  it is currently joined. Application NVS is untouched. Does not return.
- `cfg.on_network_lost` (optional) — called when the device leaves or is
  removed from the network and begins steering. Stack context, lock held:
  enqueue and return.
```

- [ ] **Step 5: Verify build and tests**

Run: `pio run -d examples/minimal_router && pio test -e native`
Expected: build SUCCESS, host tests PASS.

- [ ] **Step 6: Commit and tag locally**

```bash
git add include/zb_core.h src/zb_core.c idf_component.yml README.md
git commit -m "zb_core_factory_reset + v0.2.0

Gives consumers a way to put the device back to factory-new Zigbee state
from a local action — a keypad gesture, say — so a device that has lost its
network can be recovered without USB.

Deliberately always esp_zb_factory_reset() rather than choosing
esp_zb_bdb_reset_via_local_action() when joined: the latter only takes effect
on a network, so the choice would rest on zb_core_is_joined(), which is
precisely the flag that goes stale in the failure being recovered from.

Additive and NULL-safe, so DoorSensor and MoistureTracker compile unchanged
against v0.2.0."
git tag -a v0.2.0 -m "v0.2.0 — LEAVE handling, steering backoff, zb_core_factory_reset()"
```

- [ ] **Step 7: Ask the user before pushing**

Pushing publishes to the public repo and is what Task 4 pins against. Confirm with the user, then:

```bash
git push origin main && git push origin v0.2.0
```

---

### Task 3: Three-key factory-reset gesture in `keypad_logic`

**Repo:** `/Users/lino/Developer/499/DFR-RollerBlinds`

**Files:**
- Modify: `include/keypad_logic.h`
- Modify: `src/keypad_logic.c`
- Modify: `src/keypad.c:72`
- Test: `test/test_keypad/test_keypad.c`

**Interfaces:**
- Consumes: nothing from Tasks 1-2.
- Produces: `KP_EVT_FACTORY_RESET` on `kp_event_type_t`; `kp_init` gains a fourth parameter `uint32_t reset_ms`.

`test/test_keypad/test_keypad.c` includes `src/keypad_logic.c` directly, so no build wiring is needed.

- [ ] **Step 1: Write the failing tests**

In `test/test_keypad/test_keypad.c`, add `#define RESET 5000` under the existing `#define LONG 3000`, and change the helper:

```c
static void init(void) { kp_init(&s, HOLD, LONG, RESET); }
```

**Replace** the existing `test_fn_long_fires_during_chord` function entirely — it asserts the behaviour this task reverses — with:

```c
static void test_all_three_suppress_chord_and_fn_long(void)
{
    /* Replaces test_fn_long_fires_during_chord. Holding all three keys is now
     * the factory-reset gesture, so neither the calibration long-press nor the
     * reverse chord may fire on the way — CHORD_REVERSE would wipe the
     * calibration before the 5 s reset ever lands. */
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + LONG + 10).type);
}
```

Then add these four new tests above the `main()` runner:

```c
static void test_all_three_held_emits_factory_reset_once(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);          /* third press at 200 */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + RESET - 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET, kp_on_tick(&s, 200 + RESET + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + RESET + 500).type);
}

static void test_all_three_released_early_emits_nothing(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 3000).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 3050).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, 3100).type);
}

static void test_fn_long_suppressed_while_up_held(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_FN, true, 100);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, 500).type);   /* Up jogs */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 100 + LONG + 10).type);
}

static void test_reset_gesture_rearms_after_full_release(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET, kp_on_tick(&s, 200 + RESET + 10).type);
    kp_on_change(&s, KEY_FN, false, 9000);
    kp_on_change(&s, KEY_UP, false, 9010);
    kp_on_change(&s, KEY_DOWN, false, 9020);
    kp_on_change(&s, KEY_FN, true, 10000);
    kp_on_change(&s, KEY_UP, true, 10010);
    kp_on_change(&s, KEY_DOWN, true, 10020);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET,
                      kp_on_tick(&s, 10020 + RESET + 10).type);
}
```

Register all five in `main()`, replacing the `RUN_TEST(test_fn_long_fires_during_chord);` line with:

```c
    RUN_TEST(test_all_three_suppress_chord_and_fn_long);
    RUN_TEST(test_all_three_held_emits_factory_reset_once);
    RUN_TEST(test_all_three_released_early_emits_nothing);
    RUN_TEST(test_fn_long_suppressed_while_up_held);
    RUN_TEST(test_reset_gesture_rearms_after_full_release);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native -f test_keypad`
Expected: compile failure — `kp_init` takes 3 arguments, and `KP_EVT_FACTORY_RESET` is not declared.

- [ ] **Step 3: Extend the header**

In `include/keypad_logic.h`, add to `kp_event_type_t` after `KP_EVT_CHORD_REVERSE`:

```c
    KP_EVT_FACTORY_RESET, /* all three keys held past reset_ms (Zigbee reset) */
```

Add to `kp_state_t` after `in_chord`:

```c
    uint32_t reset_ms;              /* all-three-keys hold for factory reset */
    bool     in_reset;              /* all three seen down together */
    bool     reset_fired;           /* RESET emitted for this combination */
    uint32_t t_reset;               /* timestamp of the third press */
```

Change the `kp_init` declaration:

```c
void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms, uint32_t reset_ms);
```

- [ ] **Step 4: Implement in `keypad_logic.c`**

Update `kp_init`:

```c
void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms, uint32_t reset_ms)
{
    *s = (kp_state_t){ .hold_ms = hold_ms, .long_ms = long_ms,
                       .reset_ms = reset_ms };
}
```

In `kp_on_change`, in the **press** branch, insert the all-three latch immediately after the `if (key == KEY_FN) s->long_fired = false;` line and **before** the existing chord block — the chord block can return early, and this must not be skipped:

```c
        if (s->down[KEY_UP] && s->down[KEY_DOWN] && s->down[KEY_FN] &&
            !s->in_reset) {
            s->in_reset    = true;
            s->reset_fired = false;
            s->t_reset     = now_ms;   /* timed from the third press */
        }
```

In the **release** path, insert immediately before the existing `if (s->in_chord && ...)` block:

```c
    if (s->in_reset) {
        if (!s->down[KEY_UP] && !s->down[KEY_DOWN] && !s->down[KEY_FN]) {
            s->in_reset = false;
            s->in_chord = false;   /* the chord latch clears with it */
        }
        return evt(KP_EVT_NONE, key);   /* reset attempt suppresses everything */
    }
```

In `kp_on_tick`, add the reset check as the **first** block in the function, above the `FN_LONG` check:

```c
    /* All three keys: the factory-reset gesture. Suppresses every other
     * gesture while latched — CHORD_REVERSE in particular would wipe the
     * calibration at long_ms, well before reset_ms. */
    if (s->in_reset) {
        if (!s->reset_fired &&
            s->down[KEY_UP] && s->down[KEY_DOWN] && s->down[KEY_FN] &&
            now_ms - s->t_reset >= s->reset_ms) {
            s->reset_fired = true;
            return evt(KP_EVT_FACTORY_RESET, KEY_FN);
        }
        return evt(KP_EVT_NONE, KEY_FN);
    }
```

Change the `FN_LONG` condition to require Up and Down to be up:

```c
    if (s->down[KEY_FN] && !s->long_fired &&
        !s->down[KEY_UP] && !s->down[KEY_DOWN] &&
        now_ms - s->t_press[KEY_FN] >= s->long_ms) {
```

Change the `CHORD_REVERSE` condition to require Fn to be up:

```c
    if (s->in_chord && !s->chord_fired && !s->down[KEY_FN] &&
        s->down[KEY_UP] && s->down[KEY_DOWN] &&
        now_ms - s->t_press[KEY_UP] >= s->long_ms) {
```

Finally, update the file's header comment to describe the new gesture and both suppression rules, replacing the existing bullet list:

```c
 *        - Tap: press+release < hold_ms.
 *        - Hold (jog): Up/Down held >= hold_ms -> HOLD_START, release -> HOLD_END.
 *        - Fn long-press >= long_ms -> FN_LONG (Fn never jogs). Suppressed
 *          while Up or Down is down.
 *        - Up+Down both held >= long_ms (from the second press) -> CHORD_REVERSE.
 *          Suppressed while Fn is down.
 *          A chord attempt (both down together) suppresses the individual keys'
 *          TAP/HOLD events entirely, fired or not — fat-finger safety.
 *        - All three held >= reset_ms (from the third press) -> FACTORY_RESET,
 *          and every other gesture is suppressed while latched. Without that
 *          suppression CHORD_REVERSE fires at long_ms and wipes the
 *          calibration before the reset is ever reached.
```

- [ ] **Step 5: Update the only other `kp_init` caller**

In `src/keypad.c`, add near the other timing defines:

```c
#define RESET_MS 5000
```

and change line 72:

```c
    kp_init(&s_kp, HOLD_MS, LONG_MS, RESET_MS);
```

- [ ] **Step 6: Run the tests**

Run: `pio test -e native`
Expected: PASS, 40 cases (36 before, minus the replaced test, plus five new).

- [ ] **Step 7: Verify the firmware still builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add include/keypad_logic.h src/keypad_logic.c src/keypad.c test/test_keypad/test_keypad.c
git commit -m "keypad_logic: all-three-keys factory reset gesture

Holding Up+Down+Fn for 5 s emits KP_EVT_FACTORY_RESET, the local recovery
for a device that has lost its Zigbee network.

Two suppression rules come with it: FN_LONG now requires Up and Down to be
up, and CHORD_REVERSE requires Fn to be up. The second is load-bearing —
without it a three-key hold trips the motor-reversed chord at 3 s and wipes
the calibration before the 5 s reset is ever reached.

That reverses a deliberate earlier decision (Fn long-press was explicitly
allowed to fire during a chord), so test_fn_long_fires_during_chord is
replaced rather than deleted: it now asserts that neither gesture fires."
```

---

### Task 4: Wire the gesture, the LED, and re-pin the library

**Repo:** `/Users/lino/Developer/499/DFR-RollerBlinds`

**Files:**
- Modify: `include/status_led.h`
- Modify: `src/status_led.c`
- Modify: `include/app_event.h`
- Modify: `src/main.c`
- Modify: `src/idf_component.yml`
- Modify: `DEVELOPER_GUIDE.md`

**Interfaces:**
- Consumes: `zb_core_factory_reset()` and `cfg.on_network_lost` (Tasks 1-2); `KP_EVT_FACTORY_RESET` (Task 3).
- Produces: nothing.

**Requires the `v0.2.0` tag to be pushed** (Task 2, Step 7).

- [ ] **Step 1: Add the LED pattern**

In `include/status_led.h`, add to `led_pattern_t` after `LED_IDENTIFY`:

```c
    LED_NO_NETWORK,     /* 2 s on, 1 s off: not joined to a Zigbee network */
```

In `src/status_led.c`, add to `pattern_level`'s switch before `case LED_OFF:`:

```c
    case LED_NO_NETWORK: return t < 40;                    /* 2 s on, 1 s off */
```

The frame is 60 ticks × 50 ms = 3 s, so `t < 40` is exactly 2 s on and 1 s off.

- [ ] **Step 2: Add the event type**

In `include/app_event.h`, add to `app_event_type_t` after `APP_EVT_ZB_SET_SPEED`:

```c
    APP_EVT_ZB_NET_LOST,     /* device left/was removed from the network */
```

- [ ] **Step 3: Wire the callback, the gesture and the LED in `main.c`**

Add above `dispatcher_task`:

```c
/* Zigbee stack context (lock held): enqueue only, never touch s_pos here. */
static void zb_network_lost_cb(void)
{
    app_event_t ev = { .type = APP_EVT_ZB_NET_LOST };
    xQueueSend(s_queue, &ev, 0);
}
```

In `refresh_outputs()`, insert a branch between the `POS_CAL_WAIT_MARK2` case and the final `else`:

```c
    } else if (!zb_core_is_joined()) {
        status_led_set(LED_NO_NETWORK);
    } else {
```

so the precedence reads Identify → mark 1 → mark 2 → no network → uncalibrated/off.

In `handle_keypad()`, add a case before `default:`:

```c
    case KP_EVT_FACTORY_RESET:
        ESP_LOGW(TAG, "keypad factory reset: erasing Zigbee state");
        zb_core_factory_reset();   /* does not return */
        break;
```

In `dispatcher_task`, add a case after `APP_EVT_ZB_SET_SPEED`:

```c
        case APP_EVT_ZB_NET_LOST:
            refresh_outputs();
            break;
```

In `app_main`, add to the `zb_core_cfg_t` initialiser after the `on_joined` line:

```c
        .on_network_lost   = zb_network_lost_cb,
```

- [ ] **Step 4: Re-pin the library**

In `src/idf_component.yml`, change the version:

```yaml
    version: v0.2.0
```

- [ ] **Step 5: Document the recovery procedure**

In `DEVELOPER_GUIDE.md`, add this row to the LED pattern table after the Identify row:

```markdown
| 2 s on, 1 s off | Not joined to a Zigbee network (steering) |
```

And add a section immediately before `## Bench verification checklist`:

```markdown
## Recovering a device that has lost the network

A device removed from zigbee2mqtt now clears its Zigbee state and steers for
a network again by itself, retrying with backoff from 1 s to 60 s
indefinitely. It rejoins on its own the next time permit-join is open — no
power cycle, no USB. The LED shows **2 s on, 1 s off** while it is searching.

If it does not come back — most likely because it never received the leave
frame — hold **all three keys for 5 seconds**. That erases the device's
Zigbee state and restarts it, so it comes up factory-new and steers.

**Calibration, position and `travel_time` all survive**: they live in the
`nvs` partition, and the reset erases only `zb_storage`. The device rejoins
already knowing where its blind is.

The gesture deliberately suppresses the other long-press gestures while all
three keys are held. In particular the Up+Down reverse chord — which wipes
calibration — cannot fire on the way to the 5-second mark.

**Known limitation:** a device that misses its leave *and* cannot be reached
at the keypad is still stuck. Detecting that the coordinator is ignoring us
is out of scope here and needs its own design.
```

- [ ] **Step 6: Verify build and tests**

Run: `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: build SUCCESS (it will re-resolve the component to v0.2.0), 40 host test cases pass.

- [ ] **Step 7: Commit**

```bash
git add include/status_led.h src/status_led.c include/app_event.h src/main.c src/idf_component.yml DEVELOPER_GUIDE.md
git commit -m "Wire Zigbee recovery: reset gesture, no-network LED, esp-zb-common v0.2.0

Holding all three keys for 5 s now erases Zigbee state and restarts, and the
LED shows 2 s on / 1 s off whenever the device is off-network — a state that
was previously invisible, since the LED only ever reflected calibration.

Places no-network above uncalibrated in LED precedence but below Identify and
the calibration modes: those two are active user interactions and must win,
while an uncalibrated device is already locked out remotely, making the
network fault the more actionable one at rest."
```

- [ ] **Step 8: On-device acceptance (needs the user)**

This reproduces the failure that motivated the work. Flash, then:

1. Remove the blind from z2m. It should show `LED_NO_NETWORK` and steer, **without** a power cycle.
2. Open permit-join. It should rejoin by itself; the LED should return to its resting pattern.
3. Confirm calibration and `travel_time` survived.
4. On a healthy device, hold all three keys 5 s: same sequence, deliberately triggered.
5. Confirm the reverse chord and Fn long-press still work normally — and that the reset gesture did **not** wipe calibration.

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| Handle `ESP_ZB_ZDO_SIGNAL_LEAVE`, both leave types | 1 |
| Clear `s_joined`; fix its doc comment | 1 |
| Steering backoff 1 s → 60 s, indefinite, reset on join | 1 |
| `on_network_lost` optional callback | 1 (library), 4 (app) |
| `zb_core_factory_reset()`, always erase-and-restart | 2 |
| Never branch on `zb_core_is_joined()` | 2 (code + comment), Global Constraints |
| v0.2.0, additive so siblings compile | 2 |
| Three-key 5 s gesture | 3 |
| `CHORD_REVERSE` suppressed while Fn down | 3 |
| `FN_LONG` suppressed while Up/Down down | 3 |
| `LED_NO_NETWORK` 2 s on / 1 s off | 4 |
| LED precedence | 4 |
| Re-pin RollerBlinds | 4 |
| Host tests: gesture, both suppression rules, no regression | 3 |
| On-device acceptance | 4, Step 8 |
| Limitation documented | 4, Step 5 |

No gaps.

**Placeholder scan:** No TBD/TODO. Every code step contains literal code. The one judgement call left to a human is Task 2 Step 7 (pushing a public tag), which is explicit and deliberate.

**Type consistency:** `kp_init` takes four `uint32_t` in the header, the implementation, `keypad.c` and the test helper. `KP_EVT_FACTORY_RESET` is spelled identically in the enum, the implementation, the tests and `handle_keypad`. `on_network_lost` matches `void (*)(void)` in the struct, the library call site and `zb_network_lost_cb`. `zb_core_factory_reset` matches between header, implementation and caller. `LED_NO_NETWORK` matches between enum, `pattern_level` and `refresh_outputs`. `APP_EVT_ZB_NET_LOST` matches between the enum, the producer and the dispatcher case.

**Scope:** Two repos but one coherent deliverable — the library change is unusable without the consumer change, and the consumer change cannot compile without the library. Correctly one plan.
