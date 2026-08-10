# Configurable Travel Time Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the full-travel time of each blind be set from zigbee2mqtt and persisted in NVS, so speed can be tuned per unit without an hour-long OTA cycle.

**Architecture:** A duration in seconds is written to standard ZCL Window Covering attribute `Velocity` (0x0014). The action handler only enqueues; the dispatcher converts the duration to a per-step cruise interval using the calibrated span, clamps it to a safe range, persists it, updates the move profile, and writes the *achieved* duration back to the attribute so z2m converges on the truth. All arithmetic lives in two pure functions in `ramp.c` and is host-tested.

**Tech Stack:** C / ESP-IDF via PlatformIO, Unity host tests (`pio test -e native`), zigbee2mqtt external converter (JavaScript).

**Spec:** [docs/superpowers/specs/2026-08-10-configurable-travel-time-design.md](../specs/2026-08-10-configurable-travel-time-design.md)

## Global Constraints

- **The action handler never decides.** `covering_action_handler` validates and enqueues only. Every decision happens in `dispatcher_task` in `src/main.c`. This is the project's core concurrency rule.
- **The new attribute must NOT carry `ESP_ZB_ZCL_ATTR_ACCESS_REPORTING`.** That flag makes the prebuilt ZBOSS reporting engine crash-loop on any attribute other than lift.
- **Never call `esp_zb_zcl_report_attr_cmd_req`.** It asserts inside the prebuilt stack. Use the existing `set_attr()` helper in `covering.c`.
- **Attribute writes are task-context only** — `set_attr()` takes the Zigbee lock.
- `RAMP_MIN_CRUISE_US` = **60**, `RAMP_MAX_CRUISE_US` = **1000**, `RAMP_DEFAULT_CRUISE_US` = **150**.
- NVS namespace is **`blind`**; the new key is **`spd`** (u16 seconds). `0` means "never set".
- Host tests must keep passing: `pio test -e native` (26 cases today).
- Firmware build must stay green: `pio run -e seeed_xiao_esp32c6_zigbee`.

### Correction to the spec

The spec's "`START_US` inversion" section says the plan must apply `start = MAX(START_US, us)`. **This is already handled** — `ramp_plan_init()` at `src/ramp.c:17-20` clamps `start_us` up to `cruise_us` when cruise is slower. No work is required for this; do not add a second clamp.

---

### Task 1: Pure conversion functions in `ramp`

**Files:**
- Modify: `include/ramp.h`
- Modify: `src/ramp.c`
- Test: `test/test_ramp/test_ramp.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `uint32_t ramp_us_from_travel_time(uint16_t secs, int32_t span_steps);`
  - `uint16_t ramp_travel_time_from_us(uint32_t cruise_us, int32_t span_steps);`
  - `RAMP_MIN_CRUISE_US`, `RAMP_MAX_CRUISE_US`, `RAMP_DEFAULT_CRUISE_US`

Note `test/test_ramp/test_ramp.c` includes the implementation directly (`#include "../../src/ramp.c"`), so no build wiring is needed for new functions.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_ramp/test_ramp.c`, above the `main()` runner:

```c
/* Travel-time conversion. Span 300000 steps chosen so the arithmetic is
 * exact: 30 s -> 100 us, 60 s -> 200 us. */
#define SPAN_STEPS 300000

static void test_travel_time_converts_to_cruise_interval(void)
{
    TEST_ASSERT_EQUAL_UINT32(100, ramp_us_from_travel_time(30, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT32(200, ramp_us_from_travel_time(60, SPAN_STEPS));
}

static void test_travel_time_clamps_to_min_cruise(void)
{
    /* 10 s over this span wants 33 us — below the floor. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MIN_CRUISE_US,
                             ramp_us_from_travel_time(10, SPAN_STEPS));
}

static void test_travel_time_clamps_to_max_cruise(void)
{
    /* 400 s wants 1333 us — above the ceiling. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MAX_CRUISE_US,
                             ramp_us_from_travel_time(400, SPAN_STEPS));
}

static void test_travel_time_just_above_floor_passes_through(void)
{
    /* 18 s lands exactly on the floor, 19 s just above it. The second
     * assertion is the one that carries weight: a value near the bound must
     * come through the arithmetic unchanged rather than being flattened to
     * the clamp, which asserting only the exact-bound case cannot show. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MIN_CRUISE_US,
                             ramp_us_from_travel_time(18, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT32(63, ramp_us_from_travel_time(19, SPAN_STEPS));
}

static void test_travel_time_without_span_returns_default(void)
{
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(30, 0));
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(30, -1));
}

static void test_zero_travel_time_returns_default(void)
{
    /* 0 = "never set" — the device keeps its compile-time speed. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(0, SPAN_STEPS));
}

static void test_cruise_interval_converts_back_to_travel_time(void)
{
    TEST_ASSERT_EQUAL_UINT16(30, ramp_travel_time_from_us(100, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT16(45, ramp_travel_time_from_us(150, SPAN_STEPS));
}

static void test_travel_time_round_trips(void)
{
    uint32_t us = ramp_us_from_travel_time(30, SPAN_STEPS);
    TEST_ASSERT_EQUAL_UINT16(30, ramp_travel_time_from_us(us, SPAN_STEPS));
}

static void test_travel_time_from_us_without_span_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, ramp_travel_time_from_us(150, 0));
}
```

Register them inside the existing `main()` runner alongside the current `RUN_TEST` lines:

```c
    RUN_TEST(test_travel_time_converts_to_cruise_interval);
    RUN_TEST(test_travel_time_clamps_to_min_cruise);
    RUN_TEST(test_travel_time_clamps_to_max_cruise);
    RUN_TEST(test_travel_time_just_above_floor_passes_through);
    RUN_TEST(test_travel_time_without_span_returns_default);
    RUN_TEST(test_zero_travel_time_returns_default);
    RUN_TEST(test_cruise_interval_converts_back_to_travel_time);
    RUN_TEST(test_travel_time_round_trips);
    RUN_TEST(test_travel_time_from_us_without_span_is_zero);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_ramp`
Expected: compile failure — `ramp_us_from_travel_time` and `ramp_travel_time_from_us` are not declared.

- [ ] **Step 3: Declare the interface**

Add to `include/ramp.h`, immediately after the `ramp_plan_t` typedef and before `ramp_plan_init`:

```c
/* Cruise-interval bounds for a runtime-configurable travel time.
 * The floor is a safety limit, not a hardware maximum: below it the motor is
 * expected to stall under real blind load, and a stall silently desyncs the
 * open-loop position. See
 * docs/superpowers/specs/2026-08-10-configurable-travel-time-design.md */
#define RAMP_MIN_CRUISE_US      60u
#define RAMP_MAX_CRUISE_US      1000u
#define RAMP_DEFAULT_CRUISE_US  150u

/* Convert a requested full-travel duration into a per-step cruise interval,
 * clamped to [RAMP_MIN_CRUISE_US, RAMP_MAX_CRUISE_US]. Returns
 * RAMP_DEFAULT_CRUISE_US when secs is 0 ("never set") or the span is unknown. */
uint32_t ramp_us_from_travel_time(uint16_t secs, int32_t span_steps);

/* Inverse: the duration a given cruise interval produces over span_steps.
 * Returns 0 when the span is unknown. Used to report back the value actually
 * applied, so z2m shows reality rather than the request. */
uint16_t ramp_travel_time_from_us(uint32_t cruise_us, int32_t span_steps);
```

- [ ] **Step 4: Implement**

Append to `src/ramp.c`:

```c
uint32_t ramp_us_from_travel_time(uint16_t secs, int32_t span_steps)
{
    if (secs == 0 || span_steps <= 0) return RAMP_DEFAULT_CRUISE_US;
    uint64_t us = ((uint64_t)secs * 1000000ull) / (uint64_t)span_steps;
    if (us < RAMP_MIN_CRUISE_US) return RAMP_MIN_CRUISE_US;
    if (us > RAMP_MAX_CRUISE_US) return RAMP_MAX_CRUISE_US;
    return (uint32_t)us;
}

uint16_t ramp_travel_time_from_us(uint32_t cruise_us, int32_t span_steps)
{
    if (span_steps <= 0) return 0;
    uint64_t secs = ((uint64_t)cruise_us * (uint64_t)span_steps) / 1000000ull;
    if (secs > 65535ull) return 65535;
    return (uint16_t)secs;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native`
Expected: PASS, 35 cases (26 existing + 9 new).

- [ ] **Step 6: Commit**

```bash
git add include/ramp.h src/ramp.c test/test_ramp/test_ramp.c
git commit -m "ramp: pure travel-time <-> cruise-interval conversion

Duration is the quantity the user sets; cruise interval is what the motion
planner needs and what physically stalls the motor, so the clamp lives on
the interval. Both directions are needed: forward to apply a setting,
inverse to report back the value actually applied after clamping."
```

---

### Task 2: Persist the setting in NVS

**Files:**
- Modify: `include/blind_store.h`
- Modify: `src/blind_store.c`
- Modify: `CLAUDE.md` (NVS key table)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
  - `blind_store_data_t.travel_secs` (`uint16_t`)
  - `esp_err_t blind_store_save_travel_time(uint16_t secs);`

There are no host tests for this module — it is a thin NVS wrapper with no logic, consistent with the rest of the project. Verification is that the firmware builds and the value survives a power cycle on device (Task 6).

- [ ] **Step 1: Add the struct field and function declaration**

In `include/blind_store.h`, add to `blind_store_data_t` after `move_in_progress`:

```c
    uint16_t travel_secs;       /* full-travel time in seconds; 0 = never set */
```

And after `blind_store_save_motor_reversed`:

```c
esp_err_t blind_store_save_travel_time(uint16_t secs);
```

- [ ] **Step 2: Implement load and save**

In `src/blind_store.c`, add this helper after `get_i32`:

```c
static uint16_t get_u16(const char *key, uint16_t dflt)
{
    uint16_t v = dflt;
    nvs_get_u16(s_nvs, key, &v);   /* NOT_FOUND leaves default */
    return v;
}
```

Add to `blind_store_init`, after the `move_in_progress` line:

```c
    out->travel_secs      = get_u16("spd", 0);
```

And append the save function after `blind_store_save_motor_reversed`:

```c
esp_err_t blind_store_save_travel_time(uint16_t secs)
{
    return commit2(nvs_set_u16(s_nvs, "spd", secs), ESP_OK);
}
```

Note there is no torn-write concern here, unlike span and position: a single
value with no companion validity flag either lands or does not, and a
missing key falls back to the compile-time default.

- [ ] **Step 3: Update the NVS table in CLAUDE.md**

In `CLAUDE.md`, add a row to the NVS key table after the `rev` row:

```markdown
| `spd` | u16 | full-travel time in seconds; `0` = never set, use `RAMP_DEFAULT_CRUISE_US` |
```

- [ ] **Step 4: Verify the build**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add include/blind_store.h src/blind_store.c CLAUDE.md
git commit -m "blind_store: persist travel time under key spd

0 means never set, so a unit taking this as an OTA keeps its compile-time
speed rather than jumping to a new one."
```

---

### Task 3: Zigbee attribute and event plumbing

**Files:**
- Modify: `include/app_event.h`
- Modify: `src/covering.c`
- Modify: `include/covering.h`

**Interfaces:**
- Consumes: nothing from Tasks 1–2.
- Produces:
  - `APP_EVT_ZB_SET_SPEED` with `uint16_t secs` payload on `app_event_t`
  - `void covering_report_travel_time(uint16_t secs);`

- [ ] **Step 1: Add the event type and payload**

In `include/app_event.h`, add to `app_event_type_t` after `APP_EVT_ZB_SET_REVERSED`:

```c
    APP_EVT_ZB_SET_SPEED,    /* .secs: Velocity attr (0x0014) written from z2m */
```

And add to the union in `app_event_t`, after `uint8_t pct;`:

```c
        uint16_t   secs;
```

- [ ] **Step 2: Add attribute storage and registration**

In `src/covering.c`, add after the `s_cfg_status` declaration:

```c
/* Full-travel time in seconds, held in the standard Velocity attribute
 * (0x0014). ZCL defines that attribute as a velocity; storing a duration in
 * it is a deliberate reuse, consistent with how Mode and ConfigStatus are
 * already used here, and avoids a manufacturer-specific attribute on a stack
 * that has already produced two attribute-layer defects. */
static uint16_t s_travel_secs = 0;
```

In `covering_build_clusters`, add after the lift-percentage attribute block and before `esp_zb_cluster_list_add_window_covering_cluster`:

```c
    /* Travel time: u16, read+write, NO REPORTING flag — the reporting engine
     * crash-loops on anything but lift. z2m reads this on demand. */
    esp_zb_cluster_add_attr(attrs, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        ESP_ZB_ZCL_ATTR_WINDOW_COVERING_VELOCITY_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &s_travel_secs);
```

- [ ] **Step 3: Handle the write in the action handler**

In `covering_action_handler`, inside `case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:`, add after the existing Mode `if` block and before `return ESP_OK;`:

```c
        if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING &&
            msg->attribute.id == ESP_ZB_ZCL_ATTR_WINDOW_COVERING_VELOCITY_ID &&
            msg->attribute.data.value) {
            /* Enqueue only — the dispatcher owns clamping and persistence. */
            post((app_event_t){ .type = APP_EVT_ZB_SET_SPEED,
                                .secs = *(uint16_t *)msg->attribute.data.value });
        }
```

- [ ] **Step 4: Add the write-back function**

Append to `src/covering.c`, after `covering_report_mode` and before `#endif /* USE_ZIGBEE */`:

```c
void covering_report_travel_time(uint16_t secs)
{
    s_travel_secs = secs;
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_VELOCITY_ID, &s_travel_secs);
}
```

Declare it in `include/covering.h`, after `covering_report_mode`:

```c
void covering_report_travel_time(uint16_t secs);  /* Velocity attr 0x0014 */
```

- [ ] **Step 5: Verify the build**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add include/app_event.h include/covering.h src/covering.c
git commit -m "covering: expose travel time on standard Velocity attr 0x0014

Standard optional attribute rather than a manufacturer-specific one, and
deliberately without the REPORTING access flag — both follow from ZBOSS
stack defects this project has already hit. The handler only enqueues."
```

---

### Task 4: Dispatcher wiring

**Files:**
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `ramp_us_from_travel_time`, `ramp_travel_time_from_us`, `RAMP_DEFAULT_CRUISE_US` (Task 1); `blind_store_data_t.travel_secs`, `blind_store_save_travel_time` (Task 2); `APP_EVT_ZB_SET_SPEED`, `covering_report_travel_time` (Task 3).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Replace the CRUISE_US constant**

In `src/main.c`, delete the `CRUISE_US` define (currently line 47, the multi-line bench-tuned comment block) and make `PROF_MOVE` mutable. Replace:

```c
static const motion_profile_t PROF_MOVE = { CRUISE_US, START_US, ACCEL_STEPS };
```

with:

```c
/* Not const: cruise_us is set at boot from NVS and whenever the travel-time
 * setting or the calibrated span changes. Read at move start, so a change
 * mid-move applies to the NEXT move. */
static motion_profile_t PROF_MOVE = { RAMP_DEFAULT_CRUISE_US, START_US, ACCEL_STEPS };
```

Add `#include "ramp.h"` to the includes if not already present, and add this state variable after `static bool s_reversed;`:

```c
static uint16_t      s_travel_secs;   /* requested full-travel time; 0 = unset */
```

- [ ] **Step 2: Add the apply helper**

In `src/main.c`, add after `refresh_outputs()`:

```c
/* Derive the cruise interval from the requested travel time and the current
 * span, clamp it, and tell z2m what was actually applied. Called at boot, on
 * a z2m write, and whenever the span changes (calibration, direction wipe) —
 * the stored value is a duration, so its meaning moves with the span. */
static void apply_travel_time(void)
{
    PROF_MOVE.cruise_us = ramp_us_from_travel_time(s_travel_secs,
                                                   s_pos.closed_steps);
    /* With no span there is nothing to clamp against, so echo the request
     * back unchanged; calibration will correct it. */
    uint16_t achieved = (s_pos.closed_steps > 0)
        ? ramp_travel_time_from_us(PROF_MOVE.cruise_us, s_pos.closed_steps)
        : s_travel_secs;
    covering_report_travel_time(achieved);
}
```

- [ ] **Step 3: Handle the event**

In `dispatcher_task`, add a case after `APP_EVT_ZB_SET_REVERSED`:

```c
        case APP_EVT_ZB_SET_SPEED:
            s_travel_secs = ev.secs;
            blind_store_save_travel_time(s_travel_secs);
            apply_travel_time();
            break;
```

- [ ] **Step 4: Re-apply when the span changes**

In `handle_mark()`, inside the `if (s_pos.cal == POS_CAL_NONE)` block, add after the `s_raw = s_pos.cur_steps;` line:

```c
            apply_travel_time();   /* new span -> same duration, new interval */
```

In `toggle_reversed()`, add immediately before the closing `refresh_outputs();`:

```c
    apply_travel_time();   /* span was wiped: fall back until recalibrated */
```

- [ ] **Step 5: Load at boot**

In `app_main`, add after `s_reversed = st.motor_reversed;`:

```c
    s_travel_secs = st.travel_secs;
```

And at the very end of `app_main`, immediately after the existing `covering_report_mode(s_reversed);` call, add:

```c
    apply_travel_time();
```

This placement matters: `apply_travel_time` calls `covering_report_travel_time`, which takes the Zigbee lock, so it must run in task context after `zb_core_init` — the same position and for the same reason as `covering_report_mode`.

- [ ] **Step 6: Verify build and tests**

Run: `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: build SUCCESS, 35 test cases pass.

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "main: apply travel time from NVS and z2m writes

PROF_MOVE.cruise_us is now derived rather than compile-time. Because the
stored value is a duration, its meaning depends on the span, so it is
re-derived whenever the span changes — calibration commit and the direction
wipe — not only when written."
```

---

### Task 5: zigbee2mqtt converter

**Files:**
- Modify: `z2m/dfr_roller_blinds.js`

**Interfaces:**
- Consumes: attribute 0x0014 behaviour from Tasks 3–4.
- Produces: a `travel_time` expose in z2m.

- [ ] **Step 1: Confirm the herdsman attribute name**

The converter refers to attribute 0x0014 by its zigbee-herdsman name, expected to be `velocityLift`. Verify before writing code:

```bash
ssh lino@499.steenkamps.org \
  "grep -rn '0x0014\|velocityLift' /opt/zigbee2mqtt/node_modules/zigbee-herdsman/dist/zspec/zcl/definition/cluster.js | head"
```

Expected: a `closuresWindowCovering` attribute whose ID is `0x0014`. **If the name differs, use the name reported there** in place of `velocityLift` throughout this task.

- [ ] **Step 2: Add the converters**

In `z2m/dfr_roller_blinds.js`, add after `tzCalibrated`:

```js
const fzTravelTime = {
    cluster: 'closuresWindowCovering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.velocityLift !== undefined) {
            return {travel_time: msg.data.velocityLift};
        }
    },
};

const tzTravelTime = {
    key: ['travel_time'],
    convertSet: async (entity, key, value, meta) => {
        await entity.write('closuresWindowCovering',
                           {velocityLift: Math.round(Number(value))});
        // Deliberately no optimistic state: the device clamps the request to
        // what its motor can drive and reports the value it actually applied,
        // so read it back rather than echoing what was asked for.
        await entity.read('closuresWindowCovering', ['velocityLift']);
        return {};
    },
    convertGet: async (entity, key, meta) => {
        await entity.read('closuresWindowCovering', ['velocityLift']);
    },
};
```

- [ ] **Step 3: Register them and add the expose**

In the `module.exports` definition, extend the three lists:

```js
        fromZigbee: [fzCalibrated, fzMotorReversed, fzTravelTime],
        toZigbee: [tzMotorReversed, tzCalibrated, tzTravelTime],
```

Add to `exposes`, after the `motor_reversed` entry:

```js
            e.numeric('travel_time', ea.ALL)
                .withUnit('s')
                .withValueMin(10).withValueMax(300)
                .withDescription('Time for one full open/close travel. The ' +
                    'device clamps this to what its motor can actually drive ' +
                    'and reports back the value applied, so the field may ' +
                    'settle higher than requested. Too low a value stalls the ' +
                    'motor, which loses position and needs a keypad re-home.'),
```

The 10–300 bounds are a UI guard only. The authoritative limit is the
firmware clamp on µs/step, which depends on the blind's span — something z2m
cannot know.

- [ ] **Step 4: Read it during configure**

In `configure()`, extend the existing read to include the new attribute:

```js
            await ep.read('closuresWindowCovering',
                ['configStatus', 'windowCoveringMode',
                 'currentPositionLiftPercentage', 'velocityLift']);
```

- [ ] **Step 5: Deploy and verify z2m loads it**

```bash
scp z2m/dfr_roller_blinds.js lino@499.steenkamps.org:/opt/zigbee2mqtt/data/external_converters/
ssh lino@499.steenkamps.org "sudo systemctl restart zigbee2mqtt && sleep 20 && journalctl -u zigbee2mqtt -n 40 --no-pager"
```

Expected: z2m restarts with no converter syntax error in the log.

- [ ] **Step 6: Commit**

```bash
git add z2m/dfr_roller_blinds.js
git commit -m "z2m: expose travel_time

convertSet deliberately returns no optimistic state and reads the attribute
back instead, because the device clamps the request and reports what it
actually applied."
```

---

### Task 6: On-device verification and documentation

**Files:**
- Modify: `DEVELOPER_GUIDE.md`
- Modify: `HARDWARE.md`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

This task needs the firmware on real hardware. The unit is not reachable by
USB, so it requires a tagged release and an OTA (about an hour).

- [ ] **Step 1: Document the setting in DEVELOPER_GUIDE.md**

Add a section immediately before `## Bench verification checklist`:

```markdown
## Tuning travel time from z2m

Full-travel time is a per-unit setting, not a compile-time constant. Set
`travel_time` (seconds) on the device in z2m; it persists in NVS and survives
OTA updates.

The device clamps the request to what its motor can drive — the floor is
`RAMP_MIN_CRUISE_US` (60 µs/step) — and **writes back the value it actually
applied**, so the field settling higher than you asked for means you hit the
clamp, not that the write failed.

To find a blind's limit, step the value down until it stalls, then back off
comfortably. Torque margin varies with temperature, supply sag, and how much
fabric is wound on the roll, so a value that just works on a warm afternoon
with a near-empty roll can stall on a cold morning with a full one.

**A stall silently desyncs position** (step counting is open-loop) and
recovery is a keypad re-home — physical access to the blind. Tune in steps,
not leaps.

A device that has never been set reports the duration implied by its
compile-time default, so a freshly updated unit shows its existing speed
rather than `0`.
```

- [ ] **Step 2: Cross-reference from HARDWARE.md**

In `HARDWARE.md`, in the `### CRUISE_US is fleet-wide but the motors are not` section, replace the sentence beginning "Don't add the configuration machinery before the second measurement exists." with:

```markdown
**Resolved 2026-08-10:** travel time is now a per-unit runtime setting written
from z2m and persisted in NVS, so no fleet-wide compromise value is needed —
see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md#tuning-travel-time-from-z2m).
`RAMP_DEFAULT_CRUISE_US` remains the fallback for a unit that has never been
set. The trigger was not fleet variation but iteration cost: with units
installed and unreachable by USB, every speed change had become an hour-long
OTA cycle.
```

- [ ] **Step 3: Verify build and tests, then commit the docs**

Run: `pio run -e seeed_xiao_esp32c6_zigbee && pio test -e native`
Expected: build SUCCESS, 35 cases pass.

```bash
git add DEVELOPER_GUIDE.md HARDWARE.md
git commit -m "Docs: travel time is now a per-unit runtime setting"
```

- [ ] **Step 4: Release and OTA**

```bash
git push origin main
git tag -a v2.1.0 -m "v2.1.0 - configurable travel time via z2m"
git push origin v2.1.0
gh run watch $(gh run list --limit 1 --json databaseId -q '.[0].databaseId') --exit-status
```

Then trigger the OTA from z2m and wait for the device to reboot into v2.1.0.

- [ ] **Step 5: Verify on device**

Confirm each of these, in order:

1. After the update, `travel_time` in z2m reads roughly **55** — the duration implied by the pre-existing default — and travel speed is **unchanged**.
2. Write **30**. The blind's next full travel is visibly faster. The field holds at 30, or settles higher if clamped.
3. Write **5**. It must settle at the clamped value (around 22 s on this blind), *not* 5.
4. Power-cycle the device. `travel_time` still reads the value set in step 2/3.
5. Re-home or recalibrate, then confirm `travel_time` still reads the same duration — the span changed, the duration held.

If step 2 causes the motor to buzz without turning, the value is past this
blind's limit: write a larger number, then re-home from the keypad to recover
the lost position.

- [ ] **Step 6: Record the result**

Add the value that worked to the bench log in `DEVELOPER_GUIDE.md`, and commit.

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| Exposed quantity is seconds of full travel | 3 (attribute), 5 (expose) |
| Conversion + clamp on derived µs/step | 1 |
| `RAMP_MIN_CRUISE_US` 60 / `MAX` 1000 / `DEFAULT` 150 | 1 |
| `START_US` inversion | Already handled at `ramp.c:17-20` — noted under Global Constraints |
| Pure function in `ramp.c`, host-tested | 1 |
| Attribute 0x0014, `READ_WRITE`, no `REPORTING` | 3 |
| Handler enqueues only; dispatcher decides | 3 (enqueue), 4 (decide) |
| NVS key `spd`, u16 | 2 |
| Boot fallback to default when unset | 1 (function), 4 (boot) |
| Boot initialises attribute to equivalent seconds | 4 (`apply_travel_time` at end of `app_main`) |
| Recompute after calibration | 4 (`handle_mark`) |
| Uncalibrated: store, echo request unchanged | 4 (`apply_travel_time` span guard) |
| Mid-move write applies next move | 4 (`PROF_MOVE` read at move start) |
| Clamp-and-write-back, not reject | 4 (`apply_travel_time`), 5 (no optimistic state) |
| `span_steps == 0` guard | 1 |
| Host tests: nominal, both clamps, zero span, floor boundary | 1 |
| On-device verification | 6 |

No gaps.

**Placeholder scan:** No TBD/TODO. Every code step has literal code. The only
deliberately unresolved item is the herdsman attribute name in Task 5, which
has an explicit verification command and a stated fallback rather than being
left to guesswork.

**Type consistency:** `s_travel_secs` is `uint16_t` in both `covering.c` and
`main.c`; `blind_store_data_t.travel_secs` is `uint16_t`; `app_event_t.secs`
is `uint16_t`; `ramp_us_from_travel_time` takes `uint16_t` and returns
`uint32_t`, matching `motion_profile_t.cruise_us`;
`ramp_travel_time_from_us` takes `uint32_t` and returns `uint16_t`, matching
`covering_report_travel_time`. Consistent throughout.

**Scope:** One feature, one subsystem. No decomposition needed.
