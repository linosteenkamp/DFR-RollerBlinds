# Keypad Debounce Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop a single electrical glitch on a keypad line from starting or stopping a blind move.

**Architecture:** A new pure integrator-filter module (`key_filter`) replaces the shared library's change detector at the one call site in `keypad.c`. The output flips only after the raw level has travelled the full distance between the rails, so noise is rejected on energy rather than on sample alignment. The poll rate rises from 20 ms to 5 ms and the C6's flex glitch filter is enabled underneath as opportunistic insurance.

**Tech Stack:** C11, ESP-IDF 5.5.1 via PlatformIO, Unity host tests on the `native` env, ESP32-C6 (`seeed_xiao_esp32c6`).

**Spec:** [docs/superpowers/specs/2026-08-11-keypad-debounce-design.md](../specs/2026-08-11-keypad-debounce-design.md)

## Global Constraints

- Target board `seeed_xiao_esp32c6`; build env `seeed_xiao_esp32c6_zigbee`.
- Host tests are pure C11 with Unity, run via `pio test -e native`. The native env sets `build_src_filter = -<*>`, so test files `#include` the `.c` under test directly — follow that pattern.
- **Do not modify `esp-zb-common`.** Its `debounce` module is correct for its contract and for DoorSensor, its one other consumer. The library stays pinned at v0.2.0.
- **Do not change `keypad_logic.c` or the gesture matrix.** A tap remains a full travel. All 15 existing `test_keypad` cases must pass unchanged.
- Integrator constants: poll **5 ms**, `FILTER_SAMPLES` **12** (60 ms per edge).
- Flex glitch filter: `GLITCH_FILTER_CLK_SRC_XTAL`, `window_width_ns = 1500`, `window_thres_ns = 1500`. **Not** `..._CLK_SRC_DEFAULT` — see Task 3.
- Commit messages are prose explaining *why*, not bullet lists of what changed.
- All work happens on branch **`keypad-debounce`**, cut from `main` at the start of Task 1 and merged `--no-ff` in Task 4. Do not push or tag; that is the maintainer's call.

## File Structure

| File | Responsibility |
|---|---|
| `include/key_filter.h` (create) | Integrator filter interface — struct, init, sample, level accessor |
| `src/key_filter.c` (create) | The integrator itself. Pure, no ESP-IDF dependency, host-testable |
| `test/test_key_filter/test_key_filter.c` (create) | Unity suite for the filter, including the regression case for the reported bug |
| `platformio.ini` (modify) | Register `test_key_filter` in the `native` env's `test_filter` |
| `src/keypad.c` (modify) | Swap the debouncer for the filter, raise the poll rate, install glitch filters |
| `include/keypad.h` (modify) | Header comment currently promises "20 ms" and "library debounce module" |
| `CLAUDE.md` (modify) | Module table: update the `keypad` row, add a `key_filter` row |

---

### Task 1: The `key_filter` integrator module

**Files:**
- Create: `include/key_filter.h`
- Create: `src/key_filter.c`
- Test: `test/test_key_filter/test_key_filter.c`
- Modify: `platformio.ini` (the `[env:native]` `test_filter` list)

**Interfaces:**
- Consumes: nothing — this is a leaf module with no project dependencies.
- Produces: `key_filter_t` (fields `count`, `max`, `stable_level`), `void key_filter_init(key_filter_t *f, uint8_t max, int initial_level)`, `bool key_filter_sample(key_filter_t *f, int sampled_level)`, `int key_filter_level(const key_filter_t *f)`. Task 2 depends on exactly these names and signatures.

- [ ] **Step 0: Cut the branch**

```bash
git checkout main
git checkout -b keypad-debounce
```

- [ ] **Step 1: Create the header**

Create `include/key_filter.h`:

```c
#ifndef KEY_FILTER_H
#define KEY_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/* Integrator debounce for a mechanical key on an electrically noisy line.
 *
 * Unlike a plain change detector, the output flips only when the raw level
 * has travelled the FULL distance between the rails: the counter climbs
 * while the sample reads high and falls while it reads low, and the output
 * changes only at count == 0 and count == max. A line that rattles moves the
 * counter back and forth without ever crossing, so noise is rejected on
 * energy rather than on where the samples happen to land.
 *
 * Polarity-free: feed the raw GPIO level, read a debounced raw level back.
 * The active-low mapping stays with the caller. */
typedef struct {
    uint8_t count;         /* integrator, 0..max */
    uint8_t max;           /* samples needed to traverse rail to rail */
    int     stable_level;  /* debounced output */
} key_filter_t;

/* Seed at the rail matching initial_level, so a filter starting on a settled
 * line needs a full traverse before it can report anything. */
void key_filter_init(key_filter_t *f, uint8_t max, int initial_level);

/* Feed one raw sample. Returns true only when the debounced output flips. */
bool key_filter_sample(key_filter_t *f, int sampled_level);

int key_filter_level(const key_filter_t *f);

#endif /* KEY_FILTER_H */
```

- [ ] **Step 2: Write the failing test suite**

Create `test/test_key_filter/test_key_filter.c`:

```c
#include <unity.h>
#include "../../src/key_filter.c"

void setUp(void) {}
void tearDown(void) {}

#define MAX 12

static key_filter_t f;

/* Feed n samples at one level; return how many times the output flipped. */
static int feed(int level, int n)
{
    int flips = 0;
    for (int i = 0; i < n; i++) {
        if (key_filter_sample(&f, level)) flips++;
    }
    return flips;
}

/* The reported bug: one aberrant sample used to be a complete press. */
static void test_single_sample_glitch_is_rejected(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_FALSE(key_filter_sample(&f, 0));   /* the glitch */
    TEST_ASSERT_FALSE(key_filter_sample(&f, 1));   /* line recovers */
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_sustained_low_flips_once_on_the_max_th_sample(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(0, feed(0, MAX - 1));
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 0));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
}

static void test_no_repeat_events_while_level_holds(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(0, feed(0, 50));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
}

static void test_rattling_line_never_flips(void)
{
    key_filter_init(&f, MAX, 1);
    int flips = 0;
    for (int i = 0; i < 500; i++) {
        if (key_filter_sample(&f, i & 1)) flips++;
    }
    TEST_ASSERT_EQUAL(0, flips);
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_partial_excursion_retreats(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(0, feed(0, MAX - 1));   /* one short of the rail */
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));   /* and back again */
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_release_is_symmetric(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 1));
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

/* Without clamping, a long press would drive the counter far past the rail
 * and the release would then take hundreds of samples to register. */
static void test_counter_clamps_at_the_rails(void)
{
    key_filter_init(&f, MAX, 1);
    feed(0, 100);
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 1));
}

static void test_init_seeds_at_the_rail_with_no_spurious_event(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, 20));

    key_filter_init(&f, MAX, 0);
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(0, 20));
}

/* Equal latency at both edges means keypad_logic still measures the true
 * press duration, so HOLD_MS and LONG_MS keep their current feel. */
static void test_latency_is_symmetric_so_duration_is_preserved(void)
{
    key_filter_init(&f, MAX, 1);
    int press_at = -1, release_at = -1;
    for (int i = 0; i < 200; i++) {
        int raw = (i < 100) ? 0 : 1;          /* contact for 100 samples */
        if (key_filter_sample(&f, raw)) {
            if (press_at < 0) press_at = i; else release_at = i;
        }
    }
    TEST_ASSERT_EQUAL(11, press_at);
    TEST_ASSERT_EQUAL(111, release_at);
    TEST_ASSERT_EQUAL(100, release_at - press_at);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_sample_glitch_is_rejected);
    RUN_TEST(test_sustained_low_flips_once_on_the_max_th_sample);
    RUN_TEST(test_no_repeat_events_while_level_holds);
    RUN_TEST(test_rattling_line_never_flips);
    RUN_TEST(test_partial_excursion_retreats);
    RUN_TEST(test_release_is_symmetric);
    RUN_TEST(test_counter_clamps_at_the_rails);
    RUN_TEST(test_init_seeds_at_the_rail_with_no_spurious_event);
    RUN_TEST(test_latency_is_symmetric_so_duration_is_preserved);
    return UNITY_END();
}
```

- [ ] **Step 3: Register the suite in `platformio.ini`**

In `[env:native]`, extend `test_filter` so the new suite actually runs:

```ini
test_filter =
    test_position
    test_ramp
    test_keypad
    test_key_filter
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `pio test -e native -f test_key_filter`

Expected: FAIL at compile time — `src/key_filter.c` does not exist yet, so the `#include` cannot resolve.

- [ ] **Step 5: Write the implementation**

Create `src/key_filter.c`:

```c
#include "key_filter.h"

void key_filter_init(key_filter_t *f, uint8_t max, int initial_level)
{
    f->max          = max;
    f->stable_level = initial_level ? 1 : 0;
    f->count        = f->stable_level ? max : 0;
}

bool key_filter_sample(key_filter_t *f, int sampled_level)
{
    if (sampled_level) {
        if (f->count < f->max) f->count++;
    } else {
        if (f->count > 0) f->count--;
    }

    /* Only the rails move the output; everything between leaves it latched. */
    if (f->count == f->max && f->stable_level == 0) {
        f->stable_level = 1;
        return true;
    }
    if (f->count == 0 && f->stable_level == 1) {
        f->stable_level = 0;
        return true;
    }
    return false;
}

int key_filter_level(const key_filter_t *f)
{
    return f->stable_level;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `pio test -e native -f test_key_filter`
Expected: PASS, 9 tests, 0 failures.

- [ ] **Step 7: Run the full host suite for no regressions**

Run: `pio test -e native`
Expected: PASS — all four suites (`test_position`, `test_ramp`, `test_keypad`, `test_key_filter`).

- [ ] **Step 8: Commit**

```bash
git add include/key_filter.h src/key_filter.c test/test_key_filter/test_key_filter.c platformio.ini
git commit -F - <<'EOF'
key_filter: integrator debounce for a noisy keypad line

A change detector answers "is this sample different from the last one",
which on a polled input means a single aberrant sample is a complete edge.
An integrator instead requires the level to travel rail to rail before the
output moves, so rejection depends on how much of the window the line
actually spends low rather than on which instants the samples landed on.
That is the property this keypad needs: the harness runs beside the stepper
drive, and noise there rattles rather than pulses cleanly.

Pure and host-tested, with the single-sample glitch that ran the blind all
night captured as the first test.
EOF
```

---

### Task 2: Wire the filter into the keypad poller

**Files:**
- Modify: `src/keypad.c` (lines 1-19 header/includes/constants, 23-24 statics, 45-51 poll body, 70-72 init seeding)
- Modify: `include/keypad.h` (header comment)
- Modify: `CLAUDE.md` (module table, line 77)

**Interfaces:**
- Consumes: `key_filter_t`, `key_filter_init`, `key_filter_sample`, `key_filter_level` from Task 1.
- Produces: no new interface. `keypad_init`'s signature is unchanged.

- [ ] **Step 1: Swap the include**

In `src/keypad.c`, replace line 9:

```c
#include "debounce.h"
```

with:

```c
#include "key_filter.h"
```

- [ ] **Step 2: Update the file header comment**

Replace lines 2-4 of `src/keypad.c`:

```c
 * @file keypad.c
 * @brief 20 ms polling of the membrane keys (active-low, internal pull-ups).
 *        debounce (library) -> keypad_logic classifier -> app queue.
```

with:

```c
 * @file keypad.c
 * @brief 5 ms polling of the membrane keys (active-low, internal pull-ups).
 *        key_filter integrator -> keypad_logic classifier -> app queue.
 *        The integrator is what makes this safe on an installed unit: the
 *        harness runs beside the stepper drive, and the previous change
 *        detector turned a single coupled glitch into a full travel.
```

- [ ] **Step 3: Change the poll period and add the filter depth**

Replace line 16 of `src/keypad.c`:

```c
#define POLL_MS   20
```

with:

```c
#define POLL_MS        5
#define FILTER_SAMPLES 12   /* 12 x 5 ms = 60 ms of settled level per edge */
```

Leave `HOLD_MS`, `LONG_MS` and `RESET_MS` untouched — the filter adds equal
latency at both edges, so measured durations are unchanged.

- [ ] **Step 4: Swap the state array**

Replace line 24 of `src/keypad.c`:

```c
static debounce_t    s_db[KEY_COUNT];
```

with:

```c
static key_filter_t  s_filt[KEY_COUNT];
```

- [ ] **Step 5: Update the poll body**

Replace lines 45-51 of `src/keypad.c`:

```c
    for (int k = 0; k < KEY_COUNT; k++) {
        int level = gpio_get_level(s_gpio[k]);
        if (debounce_settle(&s_db[k], level)) {
            /* active-low: level 0 = pressed */
            post_kp(kp_on_change(&s_kp, (key_id_t)k, level == 0, now));
        }
    }
```

with:

```c
    for (int k = 0; k < KEY_COUNT; k++) {
        if (key_filter_sample(&s_filt[k], gpio_get_level(s_gpio[k]))) {
            /* active-low: 0 = pressed. Read the debounced output rather than
             * the raw sample — they agree today, but the filter owns the
             * truth and a future change to the rail logic should not have to
             * know that the caller was relying on them matching. */
            post_kp(kp_on_change(&s_kp, (key_id_t)k,
                                 key_filter_level(&s_filt[k]) == 0, now));
        }
    }
```

- [ ] **Step 6: Update the init seeding**

Replace lines 70-72 of `src/keypad.c`:

```c
    for (int k = 0; k < KEY_COUNT; k++) {
        debounce_init(&s_db[k], gpio_get_level(s_gpio[k]));
    }
```

with:

```c
    for (int k = 0; k < KEY_COUNT; k++) {
        key_filter_init(&s_filt[k], FILTER_SAMPLES, gpio_get_level(s_gpio[k]));
    }
```

- [ ] **Step 7: Update `include/keypad.h`'s comment**

Replace the comment above `keypad_init`:

```c
/* Polls the three keys every 20 ms (buttons don't need ISRs), debounces via
 * the library debounce module, classifies via keypad_logic, and posts
 * APP_EVT_KEYPAD events to q. */
```

with:

```c
/* Polls the three keys every 5 ms (buttons don't need ISRs), debounces each
 * with a key_filter integrator requiring 60 ms of settled level per edge,
 * classifies via keypad_logic, and posts APP_EVT_KEYPAD events to q. */
```

- [ ] **Step 8: Update the CLAUDE.md module table**

Replace line 77:

```markdown
| `keypad` | `src/keypad.c` | 20 ms poller (no ISR) + library debounce → feeds `keypad_logic`, events to queue | — |
```

with these two rows:

```markdown
| `keypad` | `src/keypad.c` | 5 ms poller (no ISR) + `key_filter` integrator → feeds `keypad_logic`, events to queue | — |
| `key_filter` | `src/key_filter.c` | Pure: integrator debounce — the output flips only after a full rail-to-rail traverse | `test/test_key_filter/` |
```

Also update line 42's comment to list the new suite:

```
# Host tests (position / ramp / keypad_logic / key_filter — pure C, Unity)
```

- [ ] **Step 9: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS, no warnings referencing `debounce` or `s_db`.

- [ ] **Step 10: Verify no host regressions**

Run: `pio test -e native`
Expected: PASS — all four suites. `test_keypad`'s 15 cases passing unchanged is the evidence that the gesture matrix and its timing survived.

- [ ] **Step 11: Commit**

```bash
git add src/keypad.c include/keypad.h CLAUDE.md
git commit -F - <<'EOF'
keypad: debounce the poller with the integrator instead of a change detector

The poller fed raw samples straight into the library's change detector,
whose contract expects the caller to have already settled the line — the
way DoorSensor does, with an edge ISR and a one-shot timer. This poller had
nothing, so one aberrant sample was a press and the next was a release:
20 ms apart, which classifies as a tap, which on a calibrated blind is a
full travel. That is what ran the installed unit up and down all night.

Polling at 5 ms with a 12-sample integrator now demands 60 ms of settled
level per edge. The latency is symmetric, so keypad_logic still measures
the true press duration and HOLD_MS and LONG_MS keep their current feel.
EOF
```

---

### Task 3: Enable the C6 flex glitch filter

**Files:**
- Modify: `src/keypad.c` (add an include, a helper, and one call in `keypad_init`)

**Interfaces:**
- Consumes: `key_filter` wiring from Task 2.
- Produces: nothing. `install_glitch_filters()` is file-static.

This is a separable layer: it is opportunistic hardware assistance, and the
device must work correctly without it.

- [ ] **Step 1: Add the driver include**

In `src/keypad.c`, below `#include "driver/gpio.h"`, add:

```c
#include "driver/gpio_filter.h"
```

- [ ] **Step 2: Add the installer helper**

Add above `keypad_init`:

```c
/* Kill sub-microsecond coupled spikes in silicon before the integrator ever
 * samples them. Opportunistic, not load-bearing: the integrator is the fix,
 * and a device that cannot allocate a filter must still come up with working
 * keys rather than fail init and leave the blind with no local control.
 *
 * The clock source is not the default on purpose. The C6 caps the window at
 * 63 ticks, and GLITCH_FILTER_CLK_SRC_DEFAULT is PLL_F80M at 12.5 ns/tick —
 * that caps the window at 787 ns and rejects anything longer outright. XTAL
 * is 40 MHz / 25 ns per tick, so 1500 ns is 60 ticks: inside the limit, and
 * a wider window than the default clock can express at all. */
static void install_glitch_filters(void)
{
    for (int k = 0; k < KEY_COUNT; k++) {
        gpio_flex_glitch_filter_config_t fcfg = {
            .clk_src         = GLITCH_FILTER_CLK_SRC_XTAL,
            .gpio_num        = (gpio_num_t)s_gpio[k],
            .window_width_ns = 1500,
            .window_thres_ns = 1500,
        };
        gpio_glitch_filter_handle_t h;
        esp_err_t err = gpio_new_flex_glitch_filter(&fcfg, &h);
        if (err == ESP_OK) {
            err = gpio_glitch_filter_enable(h);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "glitch filter on gpio %d unavailable (%s) — integrator still active",
                     s_gpio[k], esp_err_to_name(err));
        }
    }
}
```

The handles are deliberately not retained: the keypad lives for the lifetime
of the device, matching the existing `static` poll timer.

- [ ] **Step 3: Call it from `keypad_init`**

In `keypad_init`, immediately after the `gpio_config` error check
(`if (err != ESP_OK) return err;`) and before the filter-seeding loop, add:

```c
    install_glitch_filters();
```

Ordering matters: the pins must be configured as inputs first, and the
filters must be in place before the integrators sample the line.

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 5: Verify on hardware that the window was accepted**

Flash and watch the boot log:

Run: `pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor`

Expected: **no** `glitch filter on gpio N unavailable` warnings. If any appear
with `ESP_ERR_INVALID_ARG`, the window exceeded the tick limit — reduce
`window_width_ns` and `window_thres_ns` together to 1000 and re-flash. Do not
raise them: 1575 ns is the hard ceiling on XTAL.

Note `pio device monitor` needs a real TTY and fails from a non-interactive
shell; read the port with pyserial from `~/.platformio/penv/bin/python` if
running headless.

- [ ] **Step 6: Commit**

```bash
git add src/keypad.c
git commit -F - <<'EOF'
keypad: enable the C6 flex glitch filter on the key lines

Cheap insurance under the integrator: it discards pulses narrower than
1.5 us in silicon, and coupled spikes from stepper chopping are fast. It
does not close the bug on its own — a 45k pull-up against a metre of cable
capacitance has an RC recovery measured in microseconds, past any window
the hardware can set — which is exactly why the external pull-ups and RC
still belong on the next build.

Filter allocation failure is a warning rather than an init failure, because
a keypad with a working integrator and no glitch filter is fine, whereas a
blind with no local control is not.
EOF
```

---

### Task 4: Bench verification and release preparation

**Files:** none — this task produces evidence, not code.

**Interfaces:**
- Consumes: the complete firmware from Tasks 1-3.
- Produces: a go/no-go for tagging v2.2.0.

This task exists because host tests cannot reach `keypad.c`'s glue or the
glitch filter, and that untested glue is precisely where the bug lived.

- [ ] **Step 1: Confirm the whole host suite is green**

Run: `pio test -e native`
Expected: PASS — `test_position`, `test_ramp`, `test_keypad`, `test_key_filter`.

- [ ] **Step 2: Flash the bench board and verify the gesture matrix**

Flash, then with a meter on `EN` (D9/GPIO20 — 3.3 V idle, 0 V for the
duration of a hold), confirm each gesture still works:

| Gesture | Expected |
|---|---|
| Hold Up / Down | jogs for the duration of the hold; `EN` drops to 0 V |
| Tap Up / Down (calibrated) | full travel to the limit |
| Tap anything mid-move | move stops |
| Fn held ~3 s from standstill | enters Calibration Mode (`LED_CAL_MARK1`) |
| Up+Down held ~3 s | `motor_reversed` toggles, calibration wipes |
| All three held 5 s | factory reset, refused mid-move |

Taps are inert until the device is calibrated, so calibrate the bench unit
first or test holds only.

- [ ] **Step 3: Soak for phantom events**

With the bench unit calibrated and the motor connected, run at least ten
full travels back to back and leave it powered and idle for an hour.
Expected: zero unexplained moves, and the position reported at rest matches
the commanded limit each time.

- [ ] **Step 4: Merge the branch**

```bash
git checkout main
git merge --no-ff keypad-debounce
git branch -d keypad-debounce
```

- [ ] **Step 5: Stop and hand back**

Do **not** tag or push. Report to the maintainer that the branch is merged
locally and v2.2.0 is ready to cut, and note the two sequencing points from
the spec:

1. **The installed blind must not be Re-homed before it takes this OTA.** The
   unplug left it in Position Unknown, where taps are inert, so phantom taps
   cannot run it end to end. Re-homing first re-arms the bug.
2. **Resolve the board-identity discrepancy first.** z2m holds
   `0xa0f262fffe878e0c` → USB serial `A0:F2:62:87:8E:0C`, recorded elsewhere
   as the bare/test board rather than the installed one.

The OTA index is bumped automatically by
`.github/workflows/release-ota.yml` on the `v*` tag; no manual edit of
`ota/index.json` is needed. `FW_VER_*` likewise comes from the tag via CI, so
no source version bump is required.
