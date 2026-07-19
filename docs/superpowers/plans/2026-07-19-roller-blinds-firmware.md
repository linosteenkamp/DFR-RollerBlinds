# DFR-RollerBlinds Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32-C6 Zigbee-router roller blind controller (Window Covering 0x0102) with stepper motion, keypad calibration, and OTA — the first consumer of `esp-zb-common` v0.1.1.

**Architecture:** `app_main()` runs once and never sleeps. All decisions flow through one FreeRTOS queue consumed by a dispatcher task in `main.c`: keypad gestures, Zigbee covering commands, and motion-done events are all queue items. Pure logic (position/calibration state machine, ramp math, keypad gesture classification) lives in host-testable modules; hardware modules (`motion` GPTimer ISR, `status_led`, `blind_store` NVS, `covering` cluster glue) are thin. Zigbee bring-up, OTA, and debounce come from the `esp-zb-common` component.

**Tech Stack:** C11, ESP-IDF via PlatformIO (pioarduino), esp-zb-common v0.1.1 (esp-zigbee-lib 1.6.x), Unity host tests, zigbee2mqtt external converter.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-18-roller-blinds-design.md`; vocabulary: `CONTEXT.md` (Open=lift 0 %, Closed=lift 100 %, Tap/Hold, Calibration Mode, Mark, Span, Position Unknown, Re-home, Calibrated, Motor Reversed)
- Platform pin: `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.31-2/platform-espressif32.zip`; board `dfrobot_firebeetle2_esp32c6`; `framework = espidf`; `build_flags = -DUSE_ZIGBEE`
- Library dependency: `esp-zb-common` (hyphenated key) pinned to **git tag `v0.1.1`** — never v0.1.0 (it predates the OTA-rollback doc fixes)
- Consumer obligations from the library's final review: call `ota_client_mark_valid()` after successful boot; **all `action_handler` work defers to the app queue** (it runs in stack context with the Zigbee lock held); OTA CI must pass `--header-string` explicitly
- Zigbee identity: manufacturer `"\x0B" "DFRobot-DIY"`, model `"\x10" "DFR-RollerBlinds"` (ZCL length-prefixed; 11 and 16 chars). OTA identity: manufacturer code `0xFEFE`, image type **`0x0003`** (soil=0x0001, door=0x0002)
- ZCL Window Covering facts (verified against esp-zigbee-lib 1.6.8 headers): device id `ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID` (0x0202); attrs `CURRENT_POSITION_LIFT_PERCENTAGE` 0x0008 (u8, 0=open, 100=closed, 0xFF=unknown), `CONFIG_STATUS` 0x0007 (bit0 = Operational → our Calibrated flag), `MODE` 0x0017 (bit0 = motor direction reversed, writable); movement commands arrive via core action `ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID` (`esp_zb_zcl_window_covering_movement_message_t`: `.command` per `esp_zb_zcl_window_covering_cmd_t`, `.payload.percentage_lift_value`); Mode writes arrive via `ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID` (`esp_zb_zcl_set_attr_value_message_t`)
- Motion facts: 1/8 microstep hard-wired → 1600 µsteps/motor-rev, 24 000 µsteps/output-rev (1:15). Steps counted signed from Open=0, increasing toward Closed. DRV8825 `EN̅` is active-low enable (GPIO high = driver disabled). Step ISR + its data in IRAM
- Default GPIO map (single source of truth in `src/main.c`; bench-verifiable, avoid strapping 8/9/15 as inputs and USB-JTAG 12/13): `STEP=2, DIR=3, EN=4, BTN_UP=5, BTN_DOWN=6, BTN_FN=7, LED_EXT=14, LED_ONBOARD=15` (onboard LED output on a strapping pin is fine post-boot)
- Naming: functions `module_verb_noun()`, constants `UPPER_SNAKE_CASE`, types `snake_case_t`, log tags short uppercase
- Host tests: `pio test -e native` (Unity, tests `#include` the SUT `.c` directly, `build_src_filter = -<*>` pattern from siblings)
- Repo: this repo (`/Users/lino/Developer/499/DFR-RollerBlinds`), branch `main`, GitHub owner `linosteenkamp` (private). All task paths relative to repo root
- Reference repos (read-only): `$LIB = /Users/lino/Developer/499/esp-zb-common`, `$DS = /Users/lino/Developer/499/DFR-DoorSensor`

## File Map

| File | Responsibility | Host tests |
|---|---|---|
| `src/position.c` + `include/position.h` | Pure: steps↔lift %, clamping, calibration/re-home state machine, wipe/unknown transitions | `test/test_position/` |
| `src/ramp.c` + `include/ramp.h` | Pure: trapezoid/triangle step-interval planning | `test/test_ramp/` |
| `src/keypad_logic.c` + `include/keypad_logic.h` | Pure: press/release+time → Tap / Hold / Fn-long / Up+Down-chord | `test/test_keypad/` |
| `src/blind_store.c` + `include/blind_store.h` | NVS persistence (namespace `blind`) | — |
| `src/motion.c` + `include/motion.h` | GPTimer ISR step generation, DIR/EN, step counter, done-events to queue | — |
| `src/status_led.c` + `include/status_led.h` | LED pattern player (ext + onboard mirror) | — |
| `src/covering.c` + `include/covering.h` | Window Covering cluster build/report + action-handler → queue | — |
| `src/keypad.c` + `include/keypad.h` | GPIO + ISR + library debounce → feeds `keypad_logic`, events to queue | — |
| `src/main.c` | Wiring, GPIO map, constants, dispatcher task (gesture matrix + calibration flow) | — |
| `include/app_event.h` | The one queue item type shared by keypad/covering/motion/main | — |
| `include/ota_ids.h`, `include/fw_version.h` | OTA identity (image type 0x0003) | — |
| `z2m/dfr_roller_blinds.js` | z2m external converter (cover + motor_reversed + calibrated) | — |
| `.github/workflows/release-ota.yml` | Tag-triggered OTA build/publish | — |

---

### Task 1: Project scaffold + Zigbee skeleton (consumes esp-zb-common v0.1.1)

**Files:**
- Create: `.gitignore`, `platformio.ini`, `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults`, `sdkconfig.defaults.zigbee`, `src/CMakeLists.txt`, `src/idf_component.yml`, `include/ota_ids.h`, `include/fw_version.h`, `src/main.c`

**Interfaces:**
- Consumes: `esp-zb-common` v0.1.1 (`zb_core.h`: `zb_core_cfg_t`, `zb_core_init`, `zb_core_wait_ready`; `ota_client.h`: `ota_client_ids_t`, `ota_client_mark_valid`).
- Produces: a building, joinable Zigbee-router skeleton every later task extends; `include/ota_ids.h` macros `OTA_MANUFACTURER_CODE`/`OTA_IMAGE_TYPE`/`OTA_MODEL_ID`/`OTA_PACK_VERSION` and `include/fw_version.h` `FW_VERSION_U32`/`FW_VERSION_STR` used by Tasks 8–11.

- [ ] **Step 1: Write `.gitignore`**

```gitignore
.pio/
managed_components/
dependencies.lock
sdkconfig.dfrobot_firebeetle2_esp32c6_zigbee
__pycache__/
.pytest_cache/
.superpowers/
```

- [ ] **Step 2: Copy build templates from the library**

```bash
cd /Users/lino/Developer/499/DFR-RollerBlinds
cp "$LIB/templates/partitions.csv" .
cp "$LIB/templates/sdkconfig.defaults" .
cp "$LIB/templates/sdkconfig.defaults.zigbee" .
```

- [ ] **Step 3: Write `platformio.ini`**

```ini
; PlatformIO Project Configuration — DFR-RollerBlinds
; ESP32-C6 Zigbee-router roller blind controller (Window Covering 0x0102).

[platformio]
default_envs = dfrobot_firebeetle2_esp32c6_zigbee

[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.31-2/platform-espressif32.zip
framework = espidf
monitor_speed = 115200
board_build.partitions = partitions.csv

[env:dfrobot_firebeetle2_esp32c6_zigbee]
board = dfrobot_firebeetle2_esp32c6
build_flags = -DUSE_ZIGBEE
board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee"

; Bench env (identical; kept for sibling symmetry / future tweaks)
[env:dfrobot_firebeetle2_esp32c6_zigbee_test]
extends = env:dfrobot_firebeetle2_esp32c6_zigbee

[env:native]
platform = native
framework =
test_framework = unity
build_flags = -std=c11 -Wall -Wextra -I include
build_src_filter = -<*>
test_filter =
    test_position
    test_ramp
    test_keypad
```

- [ ] **Step 4: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(DFR-RollerBlinds)
```

- [ ] **Step 5: Write `src/CMakeLists.txt`** (later tasks append to `SRCS`; the full final list is included here so no later task edits are ambiguous — files that don't exist yet are NOT listed; each later task adds its own line)

```cmake
set(SRCS
    "main.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        nvs_flash
        esp_event
        esp_timer
        esp_driver_gpio
        esp_driver_gptimer
)
```

- [ ] **Step 6: Write `src/idf_component.yml`** (pin v0.1.1; the private-repo clone uses your local git credentials — if the component fetch fails with an auth error, run `gh auth setup-git` once and retry)

```yaml
dependencies:
  esp-zb-common:
    git: https://github.com/linosteenkamp/esp-zb-common.git
    version: v0.1.1
```

- [ ] **Step 7: Write `include/ota_ids.h`** (pattern from `$DS/include/ota_ids.h`, image type 0x0003)

```c
#ifndef OTA_IDS_H
#define OTA_IDS_H

#include <stdint.h>

/* Fixed product identity for Zigbee OTA. These three values MUST match across
 * the firmware, the .ota image header, and the z2m OTA index. */
#define OTA_MANUFACTURER_CODE  0xFEFEu   /* shared DIY 16-bit code */
#define OTA_IMAGE_TYPE         0x0003u   /* roller blinds (soil 0x0001, door 0x0002) */
#define OTA_MODEL_ID           "DFR-RollerBlinds"

/* Pack semver into the 32-bit Zigbee fileVersion (z2m renders the high hex
 * digits first — same scheme as the siblings):
 *   nibble7=major  nibble6=minor  byte2=patch  bytes1..0=0x0000 */
#define OTA_PACK_VERSION(major, minor, patch, build)            \
    (((uint32_t)((major) & 0xFu) << 28) |                       \
     ((uint32_t)((minor) & 0xFu) << 24) |                       \
     ((uint32_t)((patch) & 0xFFu) << 16))

#endif /* OTA_IDS_H */
```

- [ ] **Step 8: Write `include/fw_version.h`** (verbatim pattern from `$DS/include/fw_version.h`)

```c
#ifndef FW_VERSION_H
#define FW_VERSION_H

#include "ota_ids.h"

/* FW_VER_MAJOR/MINOR/PATCH/BUILD are injected by the build (-D flags) from the
 * git tag in CI. Defaults keep local dev builds compiling. */
#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR 0
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR 0
#endif
#ifndef FW_VER_PATCH
#define FW_VER_PATCH 0
#endif
#ifndef FW_VER_BUILD
#define FW_VER_BUILD 0
#endif

#define FW_VERSION_U32  OTA_PACK_VERSION(FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH, FW_VER_BUILD)

#define FW_VER_STR_(a,b,c) "v" #a "." #b "." #c
#define FW_VER_STR__(a,b,c) FW_VER_STR_(a,b,c)
#define FW_VERSION_STR  FW_VER_STR__(FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH)

#endif /* FW_VERSION_H */
```

- [ ] **Step 9: Write skeleton `src/main.c`** (Task 9 replaces this entirely; the skeleton proves join + OTA wiring)

```c
/**
 * DFR-RollerBlinds — skeleton main. Joins as a Zigbee router with an empty
 * Window Covering-less endpoint; Task 9 replaces this with the full wiring.
 */
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "zb_core.h"
#include "fw_version.h"

static const char *TAG = "BLINDS";

#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x10" "DFR-RollerBlinds"

static void build_clusters(esp_zb_cluster_list_t *clusters)
{
    (void)clusters;   /* Window Covering cluster arrives with the covering module */
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    zb_core_cfg_t cfg = {
        .role              = ZB_CORE_ROLE_ROUTER,
        .endpoint          = 1,
        .app_device_id     = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
        .manufacturer_name = MANUF_NAME,
        .model_identifier  = MODEL_ID,
        .ota = {
            .manufacturer_code = OTA_MANUFACTURER_CODE,
            .image_type        = OTA_IMAGE_TYPE,
            .file_version      = FW_VERSION_U32,
            .version_str       = FW_VERSION_STR,
        },
        .build_clusters    = build_clusters,
        .post_register     = NULL,
        .on_joined         = NULL,
        .action_handler    = NULL,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));
    ESP_LOGI(TAG, "router starting (%s); waiting for join…", FW_VERSION_STR);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    /* A freshly OTA'd image boots pending-verify; confirm it once the app is
     * up (join not required — the firmware itself is healthy) or the
     * bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
```

- [ ] **Step 10: Build**

Run: `cd /Users/lino/Developer/499/DFR-RollerBlinds && pio run`
Expected: SUCCESS (first run fetches the platform, esp-zb-common v0.1.1, and esp-zigbee-lib 1.6.x — several minutes).

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "Scaffold RollerBlinds firmware: Zigbee skeleton on esp-zb-common v0.1.1"
```

---

### Task 2: `position` module — calibration state machine + lift math (pure, TDD)

**Files:**
- Create: `include/position.h`, `src/position.c`, `test/test_position/test_position.c`
- Modify: `src/CMakeLists.txt` (add `"position.c"` to `SRCS`)

**Interfaces:**
- Consumes: nothing (pure C).
- Produces (used by Tasks 8–9): everything in `position.h` below, exactly as declared. Key semantics: steps are signed, `0 = Open`, increasing toward Closed; `POSITION_LIFT_UNKNOWN = 0xFF`.

- [ ] **Step 1: Write `include/position.h`** (complete file)

```c
#ifndef POSITION_H
#define POSITION_H

#include <stdbool.h>
#include <stdint.h>

#define POSITION_LIFT_UNKNOWN 0xFF

typedef enum {
    POS_CAL_NONE = 0,       /* not in calibration mode */
    POS_CAL_WAIT_MARK1,     /* full calibration: awaiting Open mark */
    POS_CAL_WAIT_MARK2,     /* full calibration: awaiting Closed mark */
    POS_CAL_WAIT_REHOME,    /* re-home: span kept, awaiting single Open mark */
} pos_cal_state_t;

typedef struct {
    bool            span_valid;    /* closed_steps is trustworthy */
    bool            pos_known;     /* cur_steps is trustworthy */
    int32_t         closed_steps;  /* Span: steps from Open to Closed (>0 when valid) */
    int32_t         cur_steps;     /* current position, 0 = Open */
    pos_cal_state_t cal;
    int32_t         cal_mark1;     /* raw counter captured at mark 1 */
} position_t;

/* Restore from persisted state (blind_store) at boot. */
void position_init(position_t *p, bool span_valid, int32_t closed_steps,
                   bool pos_known, int32_t cur_steps);

/* Calibrated = span + position trusted and not currently calibrating.
 * Only a Calibrated device accepts remote motion (spec §5 lockout). */
bool position_calibrated(const position_t *p);

/* 0 (Open) .. 100 (Closed), or POSITION_LIFT_UNKNOWN. */
uint8_t position_lift_pct(const position_t *p);

/* Target step for a lift percentage, clamped to [0, closed_steps].
 * Only meaningful when calibrated. */
int32_t position_target_for_pct(const position_t *p, uint8_t pct);

/* Clamp an arbitrary target into the soft limits. */
int32_t position_clamp(const position_t *p, int32_t target);

/* Update current position after a move completes (or stops). */
void position_set_current(position_t *p, int32_t steps);

/* --- calibration lifecycle (spec §6) --- */

/* Enter calibration mode from standstill. Picks the variant automatically:
 * span kept but position unknown -> WAIT_REHOME (one mark); otherwise full
 * calibration -> WAIT_MARK1 (two marks). */
void position_cal_enter(position_t *p);

/* Record a mark at the given raw jog-counter value.
 * WAIT_MARK1: zero reference recorded -> WAIT_MARK2, returns true.
 * WAIT_MARK2: requires raw >= mark1 + min_span (Closed lies BELOW Open =
 *   higher step count). Valid: span+position set atomically, cal exits,
 *   returns true. Invalid: state unchanged (still WAIT_MARK2), returns false.
 * WAIT_REHOME: re-zeros against the kept span, cal exits, returns true.
 * POS_CAL_NONE: returns false (marks outside calibration never change state). */
bool position_cal_mark(position_t *p, int32_t raw, int32_t min_span);

/* Abort calibration (Fn long-press inside the mode, or timeout): previous
 * span/position state is untouched. */
void position_cal_abort(position_t *p);

/* Motor Reversed toggled: span AND position wiped -> uncalibrated. */
void position_wipe(position_t *p);

/* Boot detected move_in_progress flag: position no longer trusted
 * (span kept -> re-home is enough). */
void position_mark_unknown(position_t *p);

#endif /* POSITION_H */
```

- [ ] **Step 2: Write the failing tests** — `test/test_position/test_position.c` (complete file)

```c
#include <unity.h>
#include "../../src/position.c"

void setUp(void) {}
void tearDown(void) {}

#define MIN_SPAN 6000

static position_t fresh(void)   /* factory-new device */
{
    position_t p;
    position_init(&p, false, 0, false, 0);
    return p;
}

static position_t calibrated(void)   /* span 24000, sitting at Open */
{
    position_t p;
    position_init(&p, true, 24000, true, 0);
    return p;
}

static void test_fresh_device_is_uncalibrated_unknown_lift(void)
{
    position_t p = fresh();
    TEST_ASSERT_FALSE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(POSITION_LIFT_UNKNOWN, position_lift_pct(&p));
}

static void test_calibrated_lift_math_and_rounding(void)
{
    position_t p = calibrated();
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(0, position_lift_pct(&p));
    position_set_current(&p, 24000);
    TEST_ASSERT_EQUAL_UINT8(100, position_lift_pct(&p));
    position_set_current(&p, 12000);
    TEST_ASSERT_EQUAL_UINT8(50, position_lift_pct(&p));
    position_set_current(&p, 12120);   /* 50.5% -> rounds to nearest */
    TEST_ASSERT_EQUAL_UINT8(51, position_lift_pct(&p));
}

static void test_target_for_pct_and_clamp(void)
{
    position_t p = calibrated();
    TEST_ASSERT_EQUAL_INT32(0, position_target_for_pct(&p, 0));
    TEST_ASSERT_EQUAL_INT32(24000, position_target_for_pct(&p, 100));
    TEST_ASSERT_EQUAL_INT32(12000, position_target_for_pct(&p, 50));
    TEST_ASSERT_EQUAL_INT32(24000, position_target_for_pct(&p, 150)); /* clamped */
    TEST_ASSERT_EQUAL_INT32(0, position_clamp(&p, -500));
    TEST_ASSERT_EQUAL_INT32(24000, position_clamp(&p, 99999));
}

static void test_full_calibration_happy_path(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 1000, MIN_SPAN));   /* Open mark at raw 1000 */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 25000, MIN_SPAN));  /* Closed mark */
    TEST_ASSERT_EQUAL(POS_CAL_NONE, p.cal);
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
    TEST_ASSERT_EQUAL_UINT8(100, position_lift_pct(&p));       /* at Closed after cal */
}

static void test_mark2_above_or_too_close_rejected_stays_in_mode(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 10000, MIN_SPAN));
    TEST_ASSERT_FALSE(position_cal_mark(&p, 9000, MIN_SPAN));   /* above mark1 */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);               /* still waiting */
    TEST_ASSERT_FALSE(position_cal_mark(&p, 10000 + MIN_SPAN - 1, MIN_SPAN)); /* too short */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 10000 + MIN_SPAN, MIN_SPAN));      /* boundary OK */
    TEST_ASSERT_TRUE(position_calibrated(&p));
}

static void test_recal_abort_keeps_previous_calibration(void)
{
    position_t p = calibrated();
    position_cal_enter(&p);                       /* calibrated -> full recal */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 500, MIN_SPAN));
    position_cal_abort(&p);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, p.cal);
    TEST_ASSERT_TRUE(position_calibrated(&p));    /* old span survives */
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
}

static void test_rehome_entry_when_position_unknown(void)
{
    position_t p = calibrated();
    position_mark_unknown(&p);                    /* power died mid-move */
    TEST_ASSERT_FALSE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(POSITION_LIFT_UNKNOWN, position_lift_pct(&p));
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_REHOME, p.cal); /* one mark, span kept */
    TEST_ASSERT_TRUE(position_cal_mark(&p, -3210, MIN_SPAN)); /* raw value irrelevant */
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
    TEST_ASSERT_EQUAL_UINT8(0, position_lift_pct(&p));        /* re-homed at Open */
}

static void test_wipe_forces_full_recal_not_rehome(void)
{
    position_t p = calibrated();
    position_wipe(&p);                            /* Motor Reversed toggled */
    TEST_ASSERT_FALSE(position_calibrated(&p));
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal); /* span wiped -> two marks */
}

static void test_zero_min_span_cannot_commit_empty_span(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 5000, 0));
    TEST_ASSERT_FALSE(position_cal_mark(&p, 5000, 0));  /* zero span rejected */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_FALSE(position_calibrated(&p));
}

static void test_mark_outside_calibration_is_inert(void)
{
    position_t p = calibrated();
    TEST_ASSERT_FALSE(position_cal_mark(&p, 123, MIN_SPAN));
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(0, p.cur_steps);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_device_is_uncalibrated_unknown_lift);
    RUN_TEST(test_calibrated_lift_math_and_rounding);
    RUN_TEST(test_target_for_pct_and_clamp);
    RUN_TEST(test_full_calibration_happy_path);
    RUN_TEST(test_mark2_above_or_too_close_rejected_stays_in_mode);
    RUN_TEST(test_recal_abort_keeps_previous_calibration);
    RUN_TEST(test_rehome_entry_when_position_unknown);
    RUN_TEST(test_wipe_forces_full_recal_not_rehome);
    RUN_TEST(test_mark_outside_calibration_is_inert);
    RUN_TEST(test_zero_min_span_cannot_commit_empty_span);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_position`
Expected: FAIL — cannot open `../../src/position.c`.

- [ ] **Step 4: Write `src/position.c`** (complete file)

```c
/**
 * @file position.c
 * @brief Pure position/calibration logic. No ESP dependencies — host-testable.
 *        Steps are signed; 0 = Open, increasing toward Closed (after the
 *        motor_reversed setting is applied by the motion layer).
 */
#include "position.h"

void position_init(position_t *p, bool span_valid, int32_t closed_steps,
                   bool pos_known, int32_t cur_steps)
{
    p->span_valid   = span_valid && closed_steps > 0;
    p->closed_steps = p->span_valid ? closed_steps : 0;
    p->pos_known    = pos_known && p->span_valid;
    p->cur_steps    = p->pos_known ? cur_steps : 0;
    p->cal          = POS_CAL_NONE;
    p->cal_mark1    = 0;
}

bool position_calibrated(const position_t *p)
{
    return p->span_valid && p->pos_known && p->cal == POS_CAL_NONE;
}

uint8_t position_lift_pct(const position_t *p)
{
    if (!position_calibrated(p)) {
        return POSITION_LIFT_UNKNOWN;
    }
    /* round to nearest percent */
    int64_t pct = ((int64_t)p->cur_steps * 100 + p->closed_steps / 2) / p->closed_steps;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

int32_t position_clamp(const position_t *p, int32_t target)
{
    if (!p->span_valid) {
        return target;
    }
    if (target < 0) return 0;
    if (target > p->closed_steps) return p->closed_steps;
    return target;
}

int32_t position_target_for_pct(const position_t *p, uint8_t pct)
{
    if (pct > 100) pct = 100;
    return position_clamp(p, (int32_t)(((int64_t)p->closed_steps * pct) / 100));
}

void position_set_current(position_t *p, int32_t steps)
{
    p->cur_steps = steps;
}

void position_cal_enter(position_t *p)
{
    p->cal = (p->span_valid && !p->pos_known) ? POS_CAL_WAIT_REHOME
                                              : POS_CAL_WAIT_MARK1;
}

bool position_cal_mark(position_t *p, int32_t raw, int32_t min_span)
{
    switch (p->cal) {
    case POS_CAL_WAIT_MARK1:
        p->cal_mark1 = raw;
        p->cal = POS_CAL_WAIT_MARK2;
        return true;
    case POS_CAL_WAIT_MARK2: {
        int32_t span = raw - p->cal_mark1;
        /* Closed must lie below Open by at least min_span (down = raw
         * increase); span must also be positive even if a caller ever passes
         * min_span <= 0 — a zero span would make lift math divide by zero. */
        if (span < min_span || span < 1) {
            return false;   /* rejected; stay in WAIT_MARK2 */
        }
        /* Atomic commit: span + position together, then exit the mode. */
        p->closed_steps = span;
        p->span_valid   = true;
        p->cur_steps    = p->closed_steps;   /* physically at Closed */
        p->pos_known    = true;
        p->cal          = POS_CAL_NONE;
        return true;
    }
    case POS_CAL_WAIT_REHOME:
        p->cur_steps = 0;                    /* physically at Open */
        p->pos_known = true;
        p->cal       = POS_CAL_NONE;
        return true;
    case POS_CAL_NONE:
    default:
        return false;                        /* idle marks are inert */
    }
}

void position_cal_abort(position_t *p)
{
    p->cal = POS_CAL_NONE;
}

void position_wipe(position_t *p)
{
    p->span_valid   = false;
    p->pos_known    = false;
    p->closed_steps = 0;
    p->cur_steps    = 0;
    p->cal          = POS_CAL_NONE;
}

void position_mark_unknown(position_t *p)
{
    p->pos_known = false;
}
```

- [ ] **Step 5: Add to `src/CMakeLists.txt` `SRCS`** — insert `"position.c"` after `"main.c"`.

- [ ] **Step 6: Run to verify it passes**

Run: `pio test -e native -f test_position`
Expected: PASS — 10/10.

- [ ] **Step 7: Device build still green**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add include/position.h src/position.c test/test_position src/CMakeLists.txt
git commit -m "Add position module: lift math + calibration/re-home state machine (TDD)"
```

---

### Task 3: `ramp` module — trapezoid step-interval planning (pure, TDD)

**Files:**
- Create: `include/ramp.h`, `src/ramp.c`, `test/test_ramp/test_ramp.c`
- Modify: `src/CMakeLists.txt` (add `"ramp.c"`)

**Interfaces:**
- Consumes: nothing (pure C).
- Produces (used by Task 6 `motion`): `ramp_plan_t`, `void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us, uint32_t start_us, int32_t accel_steps)`, `uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx)`.

- [ ] **Step 1: Write `include/ramp.h`** (complete file)

```c
#ifndef RAMP_H
#define RAMP_H

#include <stdint.h>

/* Trapezoidal (or, for short moves, triangular) speed profile expressed as a
 * per-step timer interval. Speeds interpolate linearly in the FREQUENCY
 * domain between 1e6/start_us and 1e6/cruise_us over accel_steps, mirror-image
 * on deceleration. Pure math: the motion ISR asks for the interval of the
 * step it is about to schedule. */
typedef struct {
    int32_t  total;        /* total steps in the move (> 0) */
    int32_t  accel_steps;  /* steps in the accel phase (== decel phase) */
    uint32_t start_us;     /* interval of the first/last step (slowest) */
    uint32_t cruise_us;    /* interval at cruise (fastest) */
} ramp_plan_t;

/* accel_steps is clamped to total/2 (triangle profile for short moves). */
void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us,
                    uint32_t start_us, int32_t accel_steps);

/* Interval in µs for step step_idx (0-based, < total). */
uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx);

#endif /* RAMP_H */
```

- [ ] **Step 2: Write the failing tests** — `test/test_ramp/test_ramp.c` (complete file)

```c
#include <unity.h>
#include "../../src/ramp.c"

void setUp(void) {}
void tearDown(void) {}

/* Bench-plausible numbers: start 2 kHz (500 µs), cruise ~3.3 kHz (300 µs). */
#define START_US  500
#define CRUISE_US 300

static void test_long_move_reaches_and_holds_cruise(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 0));
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us(&r, 800));   /* end of accel */
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us(&r, 5000));  /* mid cruise */
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 9999));   /* last step */
}

static void test_profile_is_symmetric(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    for (int32_t i = 0; i < 800; i += 37) {
        TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, i),
                                 ramp_interval_us(&r, r.total - 1 - i));
    }
}

static void test_accel_intervals_monotonically_decrease(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    uint32_t prev = ramp_interval_us(&r, 0);
    for (int32_t i = 1; i <= 800; i++) {
        uint32_t cur = ramp_interval_us(&r, i);
        TEST_ASSERT_TRUE(cur <= prev);
        prev = cur;
    }
}

static void test_short_move_becomes_triangle_never_reaches_cruise(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 100, CRUISE_US, START_US, 800);   /* accel clamped to 50 */
    TEST_ASSERT_EQUAL_INT32(50, r.accel_steps);
    for (int32_t i = 0; i < 100; i++) {
        TEST_ASSERT_TRUE(ramp_interval_us(&r, i) > CRUISE_US);
    }
    /* peak (fastest) at the apex, edges slowest */
    TEST_ASSERT_TRUE(ramp_interval_us(&r, 50) < ramp_interval_us(&r, 0));
}

static void test_single_step_move(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 1, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_long_move_reaches_and_holds_cruise);
    RUN_TEST(test_profile_is_symmetric);
    RUN_TEST(test_accel_intervals_monotonically_decrease);
    RUN_TEST(test_short_move_becomes_triangle_never_reaches_cruise);
    RUN_TEST(test_single_step_move);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_ramp`
Expected: FAIL — cannot open `../../src/ramp.c`.

- [ ] **Step 4: Write `src/ramp.c`** (complete file)

```c
/**
 * @file ramp.c
 * @brief Pure trapezoid/triangle profile math. Called from the motion ISR —
 *        keep it allocation-free and branch-light. (The motion module places
 *        this in IRAM via its own compilation unit copy of the hot path; this
 *        file stays pure C for host tests.)
 */
#include "ramp.h"

void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us,
                    uint32_t start_us, int32_t accel_steps)
{
    if (total_steps < 1) total_steps = 1;
    if (accel_steps < 1) accel_steps = 1;
    if (accel_steps > total_steps / 2) accel_steps = total_steps / 2;
    if (accel_steps < 1) accel_steps = 1;   /* total==1 edge */
    r->total       = total_steps;
    r->accel_steps = accel_steps;
    r->start_us    = start_us;
    r->cruise_us   = cruise_us;
}

/* Linear interpolation in the frequency domain between f0=1e6/start and
 * fc=1e6/cruise: f(i) = f0 + (fc-f0)*i/accel. Returns 1e6/f(i). */
static uint32_t interval_at(const ramp_plan_t *r, int32_t i_into_ramp)
{
    if (i_into_ramp >= r->accel_steps) {
        return r->cruise_us;
    }
    uint32_t f0 = 1000000u / r->start_us;
    uint32_t fc = 1000000u / r->cruise_us;
    uint32_t f  = f0 + (uint32_t)(((uint64_t)(fc - f0) * (uint32_t)i_into_ramp)
                                  / (uint32_t)r->accel_steps);
    return 1000000u / f;
}

uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx)
{
    if (step_idx < 0) step_idx = 0;
    if (step_idx >= r->total) step_idx = r->total - 1;
    int32_t from_end = r->total - 1 - step_idx;
    int32_t i = (step_idx < from_end) ? step_idx : from_end;  /* mirror */
    return interval_at(r, i);
}
```

- [ ] **Step 5: Add `"ramp.c"` to `src/CMakeLists.txt` `SRCS`.**

- [ ] **Step 6: Run to verify it passes**

Run: `pio test -e native -f test_ramp`
Expected: PASS — 5/5.

- [ ] **Step 7: Commit**

```bash
git add include/ramp.h src/ramp.c test/test_ramp src/CMakeLists.txt
git commit -m "Add ramp module: trapezoid/triangle interval planning (TDD)"
```

---

### Task 4: `keypad_logic` module — gesture classification (pure, TDD)

**Files:**
- Create: `include/keypad_logic.h`, `src/keypad_logic.c`, `test/test_keypad/test_keypad.c`
- Modify: `src/CMakeLists.txt` (add `"keypad_logic.c"`)

**Interfaces:**
- Consumes: nothing (pure C; the GPIO/debounce feeding happens in Task 9's `keypad.c`).
- Produces (used by Tasks 8–9): everything in `keypad_logic.h` below. Gesture thresholds arrive at init so main owns the constants (`KP_HOLD_MS 400`, `KP_LONG_MS 3000`).

- [ ] **Step 1: Write `include/keypad_logic.h`** (complete file)

```c
#ifndef KEYPAD_LOGIC_H
#define KEYPAD_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_UP = 0, KEY_DOWN, KEY_FN, KEY_COUNT } key_id_t;

typedef enum {
    KP_EVT_NONE = 0,
    KP_EVT_TAP,           /* press+release shorter than hold_ms */
    KP_EVT_HOLD_START,    /* key held past hold_ms (jog begins) */
    KP_EVT_HOLD_END,      /* held key released (jog ends) */
    KP_EVT_FN_LONG,       /* Fn held past long_ms (calibration mode toggle) */
    KP_EVT_CHORD_REVERSE, /* Up+Down both held past long_ms (motor_reversed) */
} kp_event_type_t;

typedef struct {
    kp_event_type_t type;
    key_id_t        key;   /* meaningful for TAP / HOLD_START / HOLD_END */
} kp_event_t;

typedef struct {
    uint32_t hold_ms;
    uint32_t long_ms;
    bool     down[KEY_COUNT];       /* current pressed state */
    uint32_t t_press[KEY_COUNT];    /* press timestamp */
    bool     holding[KEY_COUNT];    /* HOLD_START already emitted */
    bool     long_fired;            /* FN_LONG emitted for this Fn press */
    bool     chord_fired;           /* CHORD emitted for this Up+Down press */
    bool     in_chord;              /* Up+Down suppression latch */
} kp_state_t;

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms);

/* Feed a debounced edge. Returns at most one event (KP_EVT_NONE otherwise). */
kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms);

/* Call periodically (~50 ms). Emits time-based events: HOLD_START, FN_LONG,
 * CHORD_REVERSE. Returns at most one event per call. */
kp_event_t kp_on_tick(kp_state_t *s, uint32_t now_ms);

#endif /* KEYPAD_LOGIC_H */
```

- [ ] **Step 2: Write the failing tests** — `test/test_keypad/test_keypad.c` (complete file)

```c
#include <unity.h>
#include "../../src/keypad_logic.c"

void setUp(void) {}
void tearDown(void) {}

#define HOLD 400
#define LONG 3000

static kp_state_t s;

static void init(void) { kp_init(&s, HOLD, LONG); }

static void test_short_press_is_tap(void)
{
    init();
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, true, 1000).type);
    kp_event_t e = kp_on_change(&s, KEY_UP, false, 1200);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, e.type);
    TEST_ASSERT_EQUAL(KEY_UP, e.key);
}

static void test_hold_emits_start_then_end_not_tap(void)
{
    init();
    kp_on_change(&s, KEY_DOWN, true, 1000);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 1300).type);   /* not yet */
    kp_event_t e = kp_on_tick(&s, 1450);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, e.type);
    TEST_ASSERT_EQUAL(KEY_DOWN, e.key);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 1500).type);   /* only once */
    e = kp_on_change(&s, KEY_DOWN, false, 2000);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, e.type);
    TEST_ASSERT_EQUAL(KEY_DOWN, e.key);
}

static void test_fn_long_press_fires_once_no_hold_events(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_event_t e = kp_on_tick(&s, HOLD + 50);       /* Fn does NOT jog */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, e.type);
    e = kp_on_tick(&s, LONG + 10);
    TEST_ASSERT_EQUAL(KP_EVT_FN_LONG, e.type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 500).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, LONG + 900).type);
}

static void test_fn_short_press_is_tap(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 100);
    kp_event_t e = kp_on_change(&s, KEY_FN, false, 250);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, e.type);
    TEST_ASSERT_EQUAL(KEY_FN, e.key);
}

static void test_chord_fires_once_and_suppresses_up_down_events(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 100);
    /* chord timing counts from the SECOND key press (t=100) */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 50).type);   /* 3050 < 3100 */
    kp_event_t e = kp_on_tick(&s, 100 + LONG + 10);
    TEST_ASSERT_EQUAL(KP_EVT_CHORD_REVERSE, e.type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 100 + LONG + 200).type);  /* once */
    /* releases after a chord are swallowed — no TAP/HOLD_END */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 100 + LONG + 300).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 100 + LONG + 400).type);
    /* next single press works normally again */
    kp_on_change(&s, KEY_UP, true, 9000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_UP, false, 9100).type);
}

static void test_two_keys_without_long_hold_are_independent(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 50);
    /* both released quickly: chord never fired, but both were suppressed as
     * a chord ATTEMPT -> no stray TAPs from a fat-finger */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 150).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 200).type);
}

static void test_chord_during_jog_ends_hold(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, HOLD + 10).type);
    kp_event_t e = kp_on_change(&s, KEY_DOWN, true, HOLD + 100);  /* chord latch */
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, e.type);   /* jog is closed, not orphaned */
    TEST_ASSERT_EQUAL(KEY_UP, e.key);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, HOLD + 200).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, HOLD + 300).type);
}

static void test_fn_press_does_not_rearm_chord(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 100);
    TEST_ASSERT_EQUAL(KP_EVT_CHORD_REVERSE, kp_on_tick(&s, 100 + LONG).type);
    kp_on_change(&s, KEY_FN, true, 100 + LONG + 100);
    kp_event_t e = kp_on_tick(&s, 100 + LONG + 100 + LONG + 100);
    TEST_ASSERT_TRUE(e.type != KP_EVT_CHORD_REVERSE);   /* no duplicate toggle */
}

static void test_fn_long_fires_during_chord(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);   /* chord latch at 200 */
    TEST_ASSERT_EQUAL(KP_EVT_FN_LONG, kp_on_tick(&s, LONG + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_CHORD_REVERSE, kp_on_tick(&s, 200 + LONG + 10).type);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_tap);
    RUN_TEST(test_hold_emits_start_then_end_not_tap);
    RUN_TEST(test_fn_long_press_fires_once_no_hold_events);
    RUN_TEST(test_fn_short_press_is_tap);
    RUN_TEST(test_chord_fires_once_and_suppresses_up_down_events);
    RUN_TEST(test_two_keys_without_long_hold_are_independent);
    RUN_TEST(test_chord_during_jog_ends_hold);
    RUN_TEST(test_fn_press_does_not_rearm_chord);
    RUN_TEST(test_fn_long_fires_during_chord);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_keypad`
Expected: FAIL — cannot open `../../src/keypad_logic.c`.

- [ ] **Step 4: Write `src/keypad_logic.c`** (complete file)

```c
/**
 * @file keypad_logic.c
 * @brief Pure gesture classifier. Semantics (CONTEXT.md):
 *        - Tap: press+release < hold_ms.
 *        - Hold (jog): Up/Down held >= hold_ms -> HOLD_START, release -> HOLD_END.
 *        - Fn long-press >= long_ms -> FN_LONG (Fn never jogs).
 *        - Up+Down both held >= long_ms (from the second press) -> CHORD_REVERSE.
 *          A chord attempt (both down together) suppresses the individual keys'
 *          TAP/HOLD events entirely, fired or not — fat-finger safety.
 */
#include "keypad_logic.h"

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms)
{
    *s = (kp_state_t){ .hold_ms = hold_ms, .long_ms = long_ms };
}

static kp_event_t evt(kp_event_type_t t, key_id_t k)
{
    return (kp_event_t){ .type = t, .key = k };
}

kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms)
{
    if (key >= KEY_COUNT) return evt(KP_EVT_NONE, key);

    if (pressed) {
        s->down[key]    = true;
        s->t_press[key] = now_ms;
        s->holding[key] = false;
        if (key == KEY_FN) s->long_fired = false;
        if ((key == KEY_UP || key == KEY_DOWN) &&
            s->down[KEY_UP] && s->down[KEY_DOWN] && !s->in_chord) {
            s->in_chord    = true;   /* latch: suppress both keys until released */
            s->chord_fired = false;
            /* chord timing restarts from this (second) press */
            s->t_press[KEY_UP] = s->t_press[KEY_DOWN] = now_ms;
            /* if the other key was mid-jog, close that hold before suppressing
             * it — otherwise the consumer never gets the jog-stop signal */
            key_id_t other = (key == KEY_UP) ? KEY_DOWN : KEY_UP;
            if (s->holding[other]) {
                s->holding[other] = false;
                return evt(KP_EVT_HOLD_END, other);
            }
        }
        return evt(KP_EVT_NONE, key);
    }

    /* release */
    bool was_down = s->down[key];
    s->down[key] = false;
    if (!was_down) return evt(KP_EVT_NONE, key);

    if (s->in_chord && (key == KEY_UP || key == KEY_DOWN)) {
        if (!s->down[KEY_UP] && !s->down[KEY_DOWN]) {
            s->in_chord = false;     /* both released: chord attempt over */
        }
        return evt(KP_EVT_NONE, key);   /* chord suppresses individual events */
    }
    if (key == KEY_FN && s->long_fired) {
        return evt(KP_EVT_NONE, key);   /* release after FN_LONG is silent */
    }
    if (s->holding[key]) {
        s->holding[key] = false;
        return evt(KP_EVT_HOLD_END, key);
    }
    if (now_ms - s->t_press[key] < s->hold_ms) {
        return evt(KP_EVT_TAP, key);
    }
    /* held past hold_ms but HOLD_START never emitted (tick starvation):
     * treat as a completed hold with no motion — emit nothing. */
    return evt(KP_EVT_NONE, key);
}

kp_event_t kp_on_tick(kp_state_t *s, uint32_t now_ms)
{
    /* Fn is independent of the Up/Down chord — checked first so a chord
     * attempt can never starve FN_LONG */
    if (s->down[KEY_FN] && !s->long_fired &&
        now_ms - s->t_press[KEY_FN] >= s->long_ms) {
        s->long_fired = true;
        return evt(KP_EVT_FN_LONG, KEY_FN);
    }

    /* chord: suppresses Up/Down hold processing */
    if (s->in_chord && !s->chord_fired &&
        s->down[KEY_UP] && s->down[KEY_DOWN] &&
        now_ms - s->t_press[KEY_UP] >= s->long_ms) {
        s->chord_fired = true;
        return evt(KP_EVT_CHORD_REVERSE, KEY_UP);
    }
    if (s->in_chord) return evt(KP_EVT_NONE, KEY_UP);

    /* Up/Down hold -> jog (Fn never jogs) */
    for (key_id_t k = KEY_UP; k <= KEY_DOWN; k++) {
        if (s->down[k] && !s->holding[k] &&
            now_ms - s->t_press[k] >= s->hold_ms) {
            s->holding[k] = true;
            return evt(KP_EVT_HOLD_START, k);
        }
    }
    return evt(KP_EVT_NONE, KEY_UP);
}
```

- [ ] **Step 5: Add `"keypad_logic.c"` to `src/CMakeLists.txt` `SRCS`.**

- [ ] **Step 6: Run to verify it passes**

Run: `pio test -e native -f test_keypad`
Expected: PASS — 6/6. Then `pio test -e native` — all three suites green.

- [ ] **Step 7: Commit**

```bash
git add include/keypad_logic.h src/keypad_logic.c test/test_keypad src/CMakeLists.txt
git commit -m "Add keypad_logic: tap/hold/Fn-long/chord gesture classifier (TDD)"
```

---

### Task 5: `app_event.h` + `blind_store` NVS persistence

**Files:**
- Create: `include/app_event.h`, `include/blind_store.h`, `src/blind_store.c`
- Modify: `src/CMakeLists.txt` (add `"blind_store.c"`)

**Interfaces:**
- Consumes: ESP-IDF NVS.
- Produces: `app_event_t` (the single queue item type Tasks 6–9 all use) and the `blind_store_*` API below. NVS namespace `"blind"`, keys: `span_ok` (u8), `span` (i32), `pos_ok` (u8), `pos` (i32), `rev` (u8), `moving` (u8).

- [ ] **Step 1: Write `include/app_event.h`** (complete file)

```c
#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include "keypad_logic.h"

/* One queue, one item type. Producers: keypad ISR-side task (KP_*), the
 * Zigbee action handler via covering (ZB_*), the motion ISR (MOTION_DONE),
 * and esp_timer callbacks (CAL_TIMEOUT). Consumer: the dispatcher in main. */
typedef enum {
    APP_EVT_KEYPAD = 0,      /* .kp: gesture from keypad_logic */
    APP_EVT_ZB_OPEN,         /* UpOpen command */
    APP_EVT_ZB_CLOSE,        /* DownClose command */
    APP_EVT_ZB_STOP,         /* Stop command */
    APP_EVT_ZB_GOTO,         /* .pct: GoToLiftPercentage */
    APP_EVT_ZB_SET_REVERSED, /* .on: Mode attr bit0 written from z2m */
    APP_EVT_MOTION_DONE,     /* .steps final position, .completed reached target */
    APP_EVT_CAL_TIMEOUT,     /* 5-min calibration timeout */
    APP_EVT_REPORT_TICK,     /* 1 s live-position reporting tick during moves */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        kp_event_t kp;
        uint8_t    pct;
        bool       on;
        struct { int32_t steps; bool completed; };
    };
} app_event_t;

#endif /* APP_EVENT_H */
```

- [ ] **Step 2: Write `include/blind_store.h`** (complete file)

```c
#ifndef BLIND_STORE_H
#define BLIND_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool    span_valid;
    int32_t closed_steps;
    bool    pos_known;
    int32_t cur_steps;
    bool    motor_reversed;
    bool    move_in_progress;   /* set at move start, cleared on clean end */
} blind_store_data_t;

/* Open the namespace and load everything; missing keys become safe defaults
 * (uncalibrated, not reversed, no move in progress). */
esp_err_t blind_store_init(blind_store_data_t *out);

esp_err_t blind_store_save_span(bool span_valid, int32_t closed_steps);
esp_err_t blind_store_save_position(bool pos_known, int32_t cur_steps);
esp_err_t blind_store_save_motor_reversed(bool reversed);
esp_err_t blind_store_set_move_flag(bool in_progress);

#endif /* BLIND_STORE_H */
```

- [ ] **Step 3: Write `src/blind_store.c`** (complete file)

```c
/**
 * @file blind_store.c
 * @brief NVS persistence, namespace "blind". Thin wrapper — logic lives in
 *        position.c. Writes happen at move boundaries only (NVS wear).
 */
#include "blind_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "STORE";
static nvs_handle_t s_nvs;

static bool get_u8_bool(const char *key, bool dflt)
{
    uint8_t v = dflt ? 1 : 0;
    nvs_get_u8(s_nvs, key, &v);   /* NOT_FOUND leaves default */
    return v != 0;
}

static int32_t get_i32(const char *key, int32_t dflt)
{
    int32_t v = dflt;
    nvs_get_i32(s_nvs, key, &v);
    return v;
}

esp_err_t blind_store_init(blind_store_data_t *out)
{
    esp_err_t err = nvs_open("blind", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        *out = (blind_store_data_t){0};
        return err;
    }
    out->span_valid       = get_u8_bool("span_ok", false);
    out->closed_steps     = get_i32("span", 0);
    out->pos_known        = get_u8_bool("pos_ok", false);
    out->cur_steps        = get_i32("pos", 0);
    out->motor_reversed   = get_u8_bool("rev", false);
    out->move_in_progress = get_u8_bool("moving", false);
    return ESP_OK;
}

static esp_err_t commit2(esp_err_t a, esp_err_t b)
{
    esp_err_t c = nvs_commit(s_nvs);
    if (a != ESP_OK) return a;
    if (b != ESP_OK) return b;
    return c;
}

esp_err_t blind_store_save_span(bool span_valid, int32_t closed_steps)
{
    return commit2(nvs_set_u8(s_nvs, "span_ok", span_valid ? 1 : 0),
                   nvs_set_i32(s_nvs, "span", closed_steps));
}

esp_err_t blind_store_save_position(bool pos_known, int32_t cur_steps)
{
    return commit2(nvs_set_u8(s_nvs, "pos_ok", pos_known ? 1 : 0),
                   nvs_set_i32(s_nvs, "pos", cur_steps));
}

esp_err_t blind_store_save_motor_reversed(bool reversed)
{
    return commit2(nvs_set_u8(s_nvs, "rev", reversed ? 1 : 0), ESP_OK);
}

esp_err_t blind_store_set_move_flag(bool in_progress)
{
    return commit2(nvs_set_u8(s_nvs, "moving", in_progress ? 1 : 0), ESP_OK);
}
```

- [ ] **Step 4: Add `"blind_store.c"` to `src/CMakeLists.txt` `SRCS`; build**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add include/app_event.h include/blind_store.h src/blind_store.c src/CMakeLists.txt
git commit -m "Add app_event type and blind_store NVS persistence"
```

---

### Task 6: `motion` module — GPTimer step generation

**Files:**
- Create: `include/motion.h`, `src/motion.c`
- Modify: `src/CMakeLists.txt` (add `"motion.c"`)

**Interfaces:**
- Consumes: `ramp.h` (Task 3), `app_event.h` (Task 5), ESP-IDF `driver/gpio.h` + `driver/gptimer.h`, FreeRTOS queue.
- Produces (used by Task 9): the `motion_*` API below. Concurrency contract: the ISR only walks the precomputed plan, toggles STEP, counts, and posts `APP_EVT_MOTION_DONE` — all decisions happen in the dispatcher. ISR callback is `IRAM_ATTR`.

- [ ] **Step 1: Write `include/motion.h`** (complete file)

```c
#ifndef MOTION_H
#define MOTION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    int gpio_step;
    int gpio_dir;
    int gpio_en;      /* DRV8825 EN̅: high = driver DISABLED */
} motion_pins_t;

typedef struct {
    uint32_t cruise_us;   /* step interval at cruise (e.g. 300 = ~3.3 kHz) */
    uint32_t start_us;    /* first/last-step interval (e.g. 500 = 2 kHz) */
    int32_t  accel_steps; /* ramp length in steps (e.g. 800) */
} motion_profile_t;

/* Configure GPIOs (EN̅ high), create the GPTimer. done-events are posted to q
 * as APP_EVT_MOTION_DONE {steps=final absolute position, completed}. */
esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t q);

/* Applied before each move; flips the DIR level meaning. Persisted elsewhere. */
void motion_set_reversed(bool reversed);

/* Absolute move from from_steps to to_steps (signed, Open=0, + toward Closed).
 * Enables the driver, runs the trapezoid, disables the driver on arrival,
 * posts MOTION_DONE{completed=true}. Fails with ESP_ERR_INVALID_STATE if a
 * move is already running; equal from/to posts MOTION_DONE immediately.
 * hard_cap: any |to-from| larger is rejected (runaway watchdog, spec §6). */
esp_err_t motion_start(int32_t from_steps, int32_t to_steps,
                       const motion_profile_t *prof, int32_t hard_cap);

/* Request a decelerating stop. MOTION_DONE{completed=false, steps=where it
 * stopped} arrives when the motor is stationary. No-op when idle. */
void motion_stop(void);

bool motion_is_moving(void);

/* Live absolute position during a move (atomic read; between moves it equals
 * the last MOTION_DONE steps). Used for the 1 s progress reports. */
int32_t motion_current_steps(void);

#endif /* MOTION_H */
```

- [ ] **Step 2: Write `src/motion.c`** (complete file)

```c
/**
 * @file motion.c
 * @brief DRV8825 step generation on a GPTimer. One-shot alarms: each ISR
 *        invocation emits one step pulse, schedules the next by the ramp
 *        interval, and stops at plan end (or early at the decel-stop target).
 *        ISR + data in IRAM so OTA/NVS flash writes never stall stepping.
 *
 *        Stop semantics: motion_stop() converts the remaining plan into
 *        "decelerate from here": the ISR switches its index to the mirrored
 *        decel zone of the CURRENT speed and halts after those steps.
 */
#include "motion.h"
#include "ramp.h"
#include "app_event.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"     /* esp_rom_delay_us (IRAM-safe) */
#include "esp_log.h"

static const char *TAG = "MOTION";

#define STEP_PULSE_US 3   /* DRV8825 minimum STEP high time is 1.9 µs */

static motion_pins_t   s_pins;
static QueueHandle_t   s_queue;
static gptimer_handle_t s_timer;
static bool            s_reversed;

/* ---- ISR-shared state (IRAM/DRAM) ---- */
static DRAM_ATTR ramp_plan_t      s_plan;
static DRAM_ATTR volatile int32_t s_step_idx;     /* steps emitted this move */
static DRAM_ATTR volatile int32_t s_pos;          /* absolute position */
static DRAM_ATTR volatile int8_t  s_dir;          /* +1 toward Closed, -1 toward Open */
static DRAM_ATTR volatile bool    s_moving;
static DRAM_ATTR volatile bool    s_stop_req;     /* decel-stop latch */
static DRAM_ATTR volatile int32_t s_stop_at_idx;  /* index to halt at after stop req */

/* interval_at() duplicated from ramp.c's math in IRAM-safe form: the ISR must
 * not call flash-resident code. ramp_interval_us is small; we inline the same
 * computation here against s_plan. */
static IRAM_ATTR uint32_t isr_interval(int32_t idx)
{
    int32_t from_end = s_plan.total - 1 - idx;
    if (s_stop_req) {
        /* decel: mirror of how far INTO the ramp we currently are */
        int32_t remaining = s_stop_at_idx - idx;
        from_end = remaining < 0 ? 0 : remaining;
    }
    int32_t i = (idx < from_end) ? idx : from_end;
    if (i >= s_plan.accel_steps) return s_plan.cruise_us;
    uint32_t f0 = 1000000u / s_plan.start_us;
    uint32_t fc = 1000000u / s_plan.cruise_us;
    uint32_t f  = f0 + (uint32_t)(((uint64_t)(fc - f0) * (uint32_t)i)
                                  / (uint32_t)s_plan.accel_steps);
    return 1000000u / f;
}

static IRAM_ATTR bool timer_cb(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *edata, void *ctx)
{
    (void)edata; (void)ctx;
    BaseType_t hpw = pdFALSE;

    /* emit one step pulse */
    gpio_set_level(s_pins.gpio_step, 1);
    esp_rom_delay_us(STEP_PULSE_US);
    gpio_set_level(s_pins.gpio_step, 0);
    s_pos += s_dir;
    s_step_idx++;

    bool done = s_stop_req ? (s_step_idx >= s_stop_at_idx)
                           : (s_step_idx >= s_plan.total);
    if (done) {
        gptimer_stop(timer);
        gpio_set_level(s_pins.gpio_en, 1);   /* disable driver at idle */
        s_moving = false;
        app_event_t ev = { .type = APP_EVT_MOTION_DONE,
                           .steps = s_pos, .completed = !s_stop_req };
        xQueueSendFromISR(s_queue, &ev, &hpw);
    } else {
        gptimer_alarm_config_t al = {
            .alarm_count = isr_interval(s_step_idx),
            .flags.auto_reload_on_alarm = false,
        };
        gptimer_set_raw_count(timer, 0);
        gptimer_set_alarm_action(timer, &al);
    }
    return hpw == pdTRUE;
}

esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t q)
{
    s_pins  = *pins;
    s_queue = q;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pins->gpio_step) | (1ULL << pins->gpio_dir) |
                        (1ULL << pins->gpio_en),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(pins->gpio_step, 0);
    gpio_set_level(pins->gpio_en, 1);    /* disabled at boot */

    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,        /* 1 µs ticks */
        .flags.intr_shared = false,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = timer_cb };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    ESP_LOGI(TAG, "ready (step=%d dir=%d en=%d)",
             pins->gpio_step, pins->gpio_dir, pins->gpio_en);
    return ESP_OK;
}

void motion_set_reversed(bool reversed) { s_reversed = reversed; }

esp_err_t motion_start(int32_t from_steps, int32_t to_steps,
                       const motion_profile_t *prof, int32_t hard_cap)
{
    if (s_moving) return ESP_ERR_INVALID_STATE;

    int32_t delta = to_steps - from_steps;
    if (delta == 0) {
        app_event_t ev = { .type = APP_EVT_MOTION_DONE,
                           .steps = from_steps, .completed = true };
        xQueueSend(s_queue, &ev, 0);
        return ESP_OK;
    }
    int32_t dist = delta > 0 ? delta : -delta;
    if (dist > hard_cap) {
        ESP_LOGE(TAG, "move %ld exceeds hard cap %ld — refused",
                 (long)dist, (long)hard_cap);
        return ESP_ERR_INVALID_ARG;
    }

    ramp_plan_init(&s_plan, dist, prof->cruise_us, prof->start_us, prof->accel_steps);
    s_dir      = delta > 0 ? 1 : -1;
    s_pos      = from_steps;
    s_step_idx = 0;
    s_stop_req = false;

    /* logical +1 (toward Closed) maps to a DIR level; motor_reversed flips it */
    int level = (s_dir > 0) ? 1 : 0;
    if (s_reversed) level = !level;
    gpio_set_level(s_pins.gpio_dir, level);
    gpio_set_level(s_pins.gpio_en, 0);       /* enable driver */
    esp_rom_delay_us(5);                     /* DRV8825 wake/setup time */

    s_moving = true;
    gptimer_alarm_config_t al = {
        .alarm_count = s_plan.start_us,
        .flags.auto_reload_on_alarm = false,
    };
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &al));
    ESP_ERROR_CHECK(gptimer_start(s_timer));
    return ESP_OK;
}

void motion_stop(void)
{
    if (!s_moving || s_stop_req) return;
    /* Halt after decelerating from the current speed: as many steps out as we
     * are currently into the ramp (capped by what remains of the plan). */
    int32_t idx      = s_step_idx;
    int32_t from_end = s_plan.total - idx;
    int32_t into     = idx < s_plan.accel_steps ? idx : s_plan.accel_steps;
    int32_t decel    = into < from_end ? into : from_end;
    if (decel < 1) decel = 1;
    s_stop_at_idx = idx + decel;
    s_stop_req    = true;
}

bool motion_is_moving(void) { return s_moving; }

int32_t motion_current_steps(void) { return s_pos; }
```

- [ ] **Step 3: Make GPIO/GPTimer control IRAM-safe for the flash-write case** — append to `sdkconfig.defaults` (base file, not the zigbee overlay):

```
# Step ISR must keep running during flash writes (OTA/NVS): keep the driver
# control paths it calls in IRAM.
CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y
CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM=y
CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=y
CONFIG_GPTIMER_ISR_IRAM_SAFE=y
```

- [ ] **Step 4: Add `"motion.c"` to `src/CMakeLists.txt` `SRCS`; build**

Run: `pio run`
Expected: SUCCESS. (Behavioral verification is the Task 13 bench checklist — no motor on the build machine. If any `CONFIG_GPTIMER_*` symbol is rejected as unknown by this IDF version, drop only that line and note it in the report — `ISR_IRAM_SAFE` and `ISR_HANDLER_IN_IRAM` overlap across IDF 5.x minors.)

- [ ] **Step 5: Commit**

```bash
git add include/motion.h src/motion.c src/CMakeLists.txt
git commit -m "Add motion module: GPTimer IRAM step ISR, trapezoid + decel-stop"
```

---

### Task 7: `status_led` module — pattern player

**Files:**
- Create: `include/status_led.h`, `src/status_led.c`
- Modify: `src/CMakeLists.txt` (add `"status_led.c"`)

**Interfaces:**
- Consumes: ESP-IDF `driver/gpio.h`, `esp_timer.h`.
- Produces (used by Task 9): `status_led_init(int gpio_ext, int gpio_onboard)`, `status_led_set(led_pattern_t)`, `status_led_flash(led_pattern_t)`. Patterns per spec §2 LED table.

- [ ] **Step 1: Write `include/status_led.h`** (complete file)

```c
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"

/* Spec §2 LED patterns. Base patterns persist; transient patterns
 * (ACK/ERROR) play once and revert to the base. */
typedef enum {
    LED_OFF = 0,        /* normal: calibrated, idle */
    LED_CAL_MARK1,      /* 1 Hz blink: awaiting mark 1 (Open) */
    LED_CAL_MARK2,      /* fast ~5 Hz blink: awaiting mark 2 (Closed) */
    LED_UNCAL,          /* double-flash every 3 s: uncalibrated / pos unknown */
    LED_IDENTIFY,       /* steady rapid blink: Zigbee Identify */
    LED_ACK,            /* transient: three quick flashes */
    LED_ERROR,          /* transient: five rapid flashes */
} led_pattern_t;

esp_err_t status_led_init(int gpio_ext, int gpio_onboard);
void status_led_set(led_pattern_t base);       /* persistent */
void status_led_flash(led_pattern_t transient);/* LED_ACK / LED_ERROR overlay */

#endif /* STATUS_LED_H */
```

- [ ] **Step 2: Write `src/status_led.c`** (complete file)

```c
/**
 * @file status_led.c
 * @brief 50 ms-tick pattern player driving the external status LED with the
 *        onboard LED mirroring it. Patterns are on/off bitmasks over a
 *        repeating frame of 60 ticks (3 s).
 */
#include "status_led.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <stdint.h>

#define TICK_MS   50
#define FRAME     60          /* 60 ticks * 50 ms = 3 s frame */

static int s_ext = -1, s_onb = -1;
static led_pattern_t s_base = LED_OFF;
static led_pattern_t s_trans = LED_OFF;   /* LED_OFF = no transient */
static int s_base_tick;    /* base and transient keep independent tick counters */
static int s_trans_tick;   /* so a base change can never corrupt a flash train */
static esp_timer_handle_t s_timer;

/* on/off per tick for each pattern; t is the tick inside the frame */
static bool pattern_level(led_pattern_t p, int t)
{
    switch (p) {
    case LED_CAL_MARK1: return (t / 10) % 2 == 0;          /* 1 Hz */
    case LED_CAL_MARK2: return (t / 2)  % 2 == 0;          /* fast, 5 Hz (spec: ~5 Hz) */
    case LED_UNCAL:     return t == 0 || t == 1 || t == 4 || t == 5; /* dbl flash / 3 s */
    case LED_IDENTIFY:  return t % 2 == 0;                 /* rapid 10 Hz */
    case LED_ACK:       return t < 12 && (t / 2) % 2 == 0; /* 3 flashes */
    case LED_ERROR:     return t < 20 && (t / 2) % 2 == 0; /* 5 flashes */
    case LED_OFF:
    default:            return false;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    bool lvl;
    if (s_trans != LED_OFF) {
        lvl = pattern_level(s_trans, s_trans_tick);
        s_trans_tick++;
        /* transient ends after its flash train (ACK 12, ERROR 20 ticks) */
        if ((s_trans == LED_ACK && s_trans_tick >= 12) ||
            (s_trans == LED_ERROR && s_trans_tick >= 20)) {
            s_trans = LED_OFF;
        }
    } else {
        lvl = pattern_level(s_base, s_base_tick);
        s_base_tick = (s_base_tick + 1) % FRAME;
    }
    gpio_set_level(s_ext, lvl);
    gpio_set_level(s_onb, lvl);
}

esp_err_t status_led_init(int gpio_ext, int gpio_onboard)
{
    s_ext = gpio_ext;
    s_onb = gpio_onboard;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << gpio_ext) | (1ULL << gpio_onboard),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    const esp_timer_create_args_t targs = {
        .callback = tick_cb, .name = "led_tick",
    };
    err = esp_timer_create(&targs, &s_timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(s_timer, TICK_MS * 1000);
}

void status_led_set(led_pattern_t base)
{
    if (base != s_base) { s_base = base; s_base_tick = 0; }
}

void status_led_flash(led_pattern_t transient)
{
    if (transient != LED_ACK && transient != LED_ERROR) {
        return;   /* only flash trains are transients; base patterns never overlay */
    }
    s_trans = transient;
    s_trans_tick = 0;
}
```

- [ ] **Step 3: Add `"status_led.c"` to `src/CMakeLists.txt` `SRCS`; build**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/status_led.h src/status_led.c src/CMakeLists.txt
git commit -m "Add status_led pattern player (ext + onboard mirror)"
```

---

### Task 8: `covering` module — Window Covering cluster glue

**Files:**
- Create: `include/covering.h`, `src/covering.c`
- Modify: `src/CMakeLists.txt` (add `"covering.c"`)

**Interfaces:**
- Consumes: `app_event.h` (Task 5), esp-zigbee-lib (via esp-zb-common), `position.h` (`POSITION_LIFT_UNKNOWN`).
- Produces (used by Task 9): `covering_build_clusters`, `covering_post_register`, `covering_action_handler` (plugged into `zb_core_cfg_t`), `covering_set_queue`, `covering_report_lift(uint8_t pct)`, `covering_set_operational(bool)`, `covering_report_mode(bool reversed)`. Lockout rule: the ACTION HANDLER never decides anything — it forwards every command to the queue; the dispatcher (Task 9) rejects motion when uncalibrated. ZCL-level rejection nuance: the movement callback's return value is the ZCL status — return `ESP_FAIL` for motion commands received while the dispatcher has motion locked (checked via an atomic flag set by main), producing the default-response failure the spec requires.

- [ ] **Step 1: Write `include/covering.h`** (complete file)

```c
#ifndef COVERING_H
#define COVERING_H

#ifdef USE_ZIGBEE
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Wire-up: main passes these three into zb_core_cfg_t. */
void covering_build_clusters(esp_zb_cluster_list_t *clusters);
void covering_post_register(void);
esp_err_t covering_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                  const void *message);

/* Dispatcher queue for APP_EVT_ZB_* events (set before zb_core_init). */
void covering_set_queue(QueueHandle_t q);

/* Motion lockout flag, owned by the dispatcher: while false, movement
 * commands get a ZCL failure response and are NOT queued (spec §5). */
void covering_set_motion_allowed(bool allowed);

/* Attribute updates — call from TASK context only (they take the Zigbee
 * lock). pct: 0..100 or POSITION_LIFT_UNKNOWN (0xFF). */
void covering_report_lift(uint8_t pct);
void covering_set_operational(bool calibrated);   /* ConfigStatus bit0 */
void covering_report_mode(bool reversed);         /* Mode attr bit0 */

#endif /* USE_ZIGBEE */
#endif /* COVERING_H */
```

- [ ] **Step 2: Write `src/covering.c`** (complete file)

```c
/**
 * @file covering.c
 * @brief Window Covering (0x0102) server glue. The action handler runs in
 *        Zigbee stack context WITH THE LOCK HELD (zb_core contract): it only
 *        classifies the message, checks the lockout flag, and posts to the
 *        dispatcher queue. All real work happens in main's dispatcher task.
 */
#ifdef USE_ZIGBEE
#include "covering.h"
#include "app_event.h"
#include "position.h"          /* POSITION_LIFT_UNKNOWN */

#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_window_covering.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "esp_log.h"

static const char *TAG = "COVER";

#define COVER_ENDPOINT 1U

static QueueHandle_t  s_queue;
static volatile bool  s_motion_allowed = false;

/* Attribute storage — the ZCL table keeps pointers; must live forever. */
static uint8_t s_lift_pct   = POSITION_LIFT_UNKNOWN;
static uint8_t s_mode       = 0;   /* bit0 = motor reversed */
static uint8_t s_cfg_status = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_ONLINE; /* not yet operational */

void covering_set_queue(QueueHandle_t q) { s_queue = q; }
void covering_set_motion_allowed(bool allowed) { s_motion_allowed = allowed; }

void covering_build_clusters(esp_zb_cluster_list_t *clusters)
{
    esp_zb_window_covering_cluster_cfg_t cfg = {
        .covering_type   = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_ROLLERSHADE,
        .covering_status = s_cfg_status,
        .covering_mode   = s_mode,
    };
    esp_zb_attribute_list_t *attrs = esp_zb_window_covering_cluster_create(&cfg);
    /* Lift percentage is not among the create()-mandatory attrs — add it
     * (u8, read+report). 0xFF = unknown until calibrated. */
    esp_zb_cluster_add_attr(attrs, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &s_lift_pct);
    esp_zb_cluster_list_add_window_covering_cluster(clusters, attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void covering_post_register(void)
{
    /* Device-side reporting on lift % so the stack pushes on-change reports
     * (same mechanism the siblings use). */
    esp_zb_zcl_reporting_info_t rep = {
        .direction      = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
        .ep             = COVER_ENDPOINT,
        .cluster_id     = ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        .cluster_role   = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id        = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
        .manuf_code     = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .u.send_info    = { .min_interval = 0, .max_interval = 0,
                            .def_min_interval = 0, .def_max_interval = 0,
                            .delta.u8 = 1 },
    };
    esp_zb_zcl_update_reporting_info(&rep);
}

static void post(app_event_t ev)
{
    if (s_queue) xQueueSend(s_queue, &ev, 0);
}

esp_err_t covering_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                  const void *message)
{
    switch (cb_id) {
    case ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID: {
        const esp_zb_zcl_window_covering_movement_message_t *msg = message;
        switch (msg->command) {
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN:
            if (!s_motion_allowed) return ESP_FAIL;      /* ZCL failure resp */
            post((app_event_t){ .type = APP_EVT_ZB_OPEN });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE:
            if (!s_motion_allowed) return ESP_FAIL;
            post((app_event_t){ .type = APP_EVT_ZB_CLOSE });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP:
            /* Stop is always safe — queue it even when locked out. */
            post((app_event_t){ .type = APP_EVT_ZB_STOP });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
            if (!s_motion_allowed) return ESP_FAIL;
            if (msg->payload.percentage_lift_value > 100) return ESP_FAIL;
            post((app_event_t){ .type = APP_EVT_ZB_GOTO,
                                .pct = msg->payload.percentage_lift_value });
            return ESP_OK;
        default:
            ESP_LOGW(TAG, "unsupported covering cmd 0x%x", msg->command);
            return ESP_FAIL;
        }
    }
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
        const esp_zb_zcl_set_attr_value_message_t *msg = message;
        if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING &&
            msg->attribute.id == ESP_ZB_ZCL_ATTR_WINDOW_COVERING_MODE_ID &&
            msg->attribute.data.value) {
            uint8_t mode = *(uint8_t *)msg->attribute.data.value;
            post((app_event_t){ .type = APP_EVT_ZB_SET_REVERSED,
                                .on = (mode & ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_REVERSED_MOTOR_DIRECTION) != 0 });
        }
        return ESP_OK;
    }
    default:
        return ESP_OK;
    }
}

/* ---- task-context attribute updates (take the Zigbee lock) ---- */

static void set_attr(uint16_t attr_id, void *val)
{
    if (!esp_zb_lock_acquire(portMAX_DELAY)) return;
    esp_zb_zcl_set_attribute_val(COVER_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        attr_id, val, false);
    esp_zb_lock_release();
}

void covering_report_lift(uint8_t pct)
{
    s_lift_pct = pct;
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
             &s_lift_pct);
}

void covering_set_operational(bool calibrated)
{
    s_cfg_status = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_ONLINE |
                   (calibrated ? ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_OPERATIONAL : 0);
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_STATUS_ID, &s_cfg_status);
}

void covering_report_mode(bool reversed)
{
    s_mode = reversed ? ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_REVERSED_MOTOR_DIRECTION : 0;
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_MODE_ID, &s_mode);
}
#endif /* USE_ZIGBEE */
```

- [ ] **Step 3: Add `"covering.c"` to `src/CMakeLists.txt` `SRCS`; build**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/covering.h src/covering.c src/CMakeLists.txt
git commit -m "Add covering module: Window Covering cluster glue -> app queue"
```

---

### Task 9: `keypad` GPIO feeder + full `main.c` dispatcher

**Files:**
- Create: `include/keypad.h`, `src/keypad.c`
- Modify: `src/main.c` (**complete replacement** of the Task 1 skeleton), `src/CMakeLists.txt` (add `"keypad.c"`)

**Interfaces:**
- Consumes: everything produced by Tasks 2–8 plus library `debounce.h`, `zb_core.h`, `ota_client.h`.
- Produces: the running application. The dispatcher implements the CONTEXT.md gesture matrix and spec §6 calibration flow verbatim.

- [ ] **Step 1: Write `include/keypad.h`** (complete file)

```c
#ifndef KEYPAD_H
#define KEYPAD_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Polls the three keys every 20 ms (buttons don't need ISRs), debounces via
 * the library debounce module, classifies via keypad_logic, and posts
 * APP_EVT_KEYPAD events to q. */
esp_err_t keypad_init(int gpio_up, int gpio_down, int gpio_fn, QueueHandle_t q);

#endif /* KEYPAD_H */
```

- [ ] **Step 2: Write `src/keypad.c`** (complete file)

```c
/**
 * @file keypad.c
 * @brief 20 ms polling of the membrane keys (active-low, internal pull-ups).
 *        debounce (library) -> keypad_logic classifier -> app queue.
 */
#include "keypad.h"
#include "keypad_logic.h"
#include "app_event.h"
#include "debounce.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#define POLL_MS   20
#define HOLD_MS   400
#define LONG_MS   3000

static int           s_gpio[KEY_COUNT];
static debounce_t    s_db[KEY_COUNT];
static kp_state_t    s_kp;
static QueueHandle_t s_queue;

static void post_kp(kp_event_t e)
{
    if (e.type == KP_EVT_NONE) return;
    app_event_t ev = { .type = APP_EVT_KEYPAD, .kp = e };
    xQueueSend(s_queue, &ev, 0);
}

static void poll_cb(void *arg)
{
    (void)arg;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    for (int k = 0; k < KEY_COUNT; k++) {
        int level = gpio_get_level(s_gpio[k]);
        if (debounce_settle(&s_db[k], level)) {
            /* active-low: level 0 = pressed */
            post_kp(kp_on_change(&s_kp, (key_id_t)k, level == 0, now));
        }
    }
    post_kp(kp_on_tick(&s_kp, now));
}

esp_err_t keypad_init(int gpio_up, int gpio_down, int gpio_fn, QueueHandle_t q)
{
    s_gpio[KEY_UP] = gpio_up;
    s_gpio[KEY_DOWN] = gpio_down;
    s_gpio[KEY_FN] = gpio_fn;
    s_queue = q;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << gpio_up) | (1ULL << gpio_down) | (1ULL << gpio_fn),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    for (int k = 0; k < KEY_COUNT; k++) {
        debounce_init(&s_db[k], gpio_get_level(s_gpio[k]));
    }
    kp_init(&s_kp, HOLD_MS, LONG_MS);

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t targs = { .callback = poll_cb, .name = "keypad" };
    err = esp_timer_create(&targs, &timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(timer, POLL_MS * 1000);
}
```

- [ ] **Step 3: Replace `src/main.c` entirely** (complete file)

```c
/**
 * DFR-RollerBlinds — wiring + dispatcher. One queue; every decision happens
 * here in task context. Gesture matrix and calibration flow per CONTEXT.md /
 * spec §6; z2m motion lockout per spec §5.
 */
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "zb_core.h"
#include "fw_version.h"
#include "app_event.h"
#include "position.h"
#include "motion.h"
#include "blind_store.h"
#include "keypad.h"
#include "status_led.h"
#include "covering.h"

static const char *TAG = "BLINDS";

/* ---- identity ---- */
#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x10" "DFR-RollerBlinds"
#define APP_ENDPOINT 1

/* ---- GPIO map (bench-verify; see HARDWARE.md) ---- */
#define PIN_STEP     2
#define PIN_DIR      3
#define PIN_EN       4
#define PIN_BTN_UP   5
#define PIN_BTN_DOWN 6
#define PIN_BTN_FN   7
#define PIN_LED_EXT  14
#define PIN_LED_ONB  15

/* ---- motion tuning (bench constants, spec §6) ---- */
#define CRUISE_US       300      /* ~3.3 kHz at 1/8 µstep */
#define START_US        500      /* ~2 kHz first/last step */
#define ACCEL_STEPS     800
#define JOG_CRUISE_US   600      /* jog slower for control */
#define MIN_SPAN_STEPS  6000     /* ~1/4 output rev: min valid calibration */
#define HARD_CAP_MARGIN 2400     /* watchdog: allowed overshoot of span */
#define JOG_UNBOUNDED   2000000  /* "infinite" jog target while uncalibrated */
#define CAL_TIMEOUT_US  (5LL * 60 * 1000000)
#define REPORT_PERIOD_US (1LL * 1000000)

static QueueHandle_t s_queue;
static position_t    s_pos;
static bool          s_reversed;
static bool          s_cal_mode;      /* position.cal != NONE mirror for clarity */
static bool          s_cal_moved;     /* blind was jogged inside calibration mode */
static bool          s_cal_abort_pending; /* timeout hit mid-jog: abort on DONE */
static bool          s_pending_valid; /* ZB target parked while a move decelerates */
static uint8_t       s_pending_pct;
static int32_t       s_raw;           /* raw step counter (valid in cal mode too) */
static esp_timer_handle_t s_cal_timer;
static esp_timer_handle_t s_report_timer;

static const motion_profile_t PROF_MOVE = { CRUISE_US, START_US, ACCEL_STEPS };
static const motion_profile_t PROF_JOG  = { JOG_CRUISE_US, START_US, ACCEL_STEPS };

/* ---------- helpers ---------- */

static void refresh_outputs(void)
{
    bool cal = position_calibrated(&s_pos);
    covering_set_motion_allowed(cal);
    covering_set_operational(cal);
    covering_report_lift(position_lift_pct(&s_pos));
    if (s_pos.cal == POS_CAL_WAIT_MARK1 || s_pos.cal == POS_CAL_WAIT_REHOME) {
        status_led_set(LED_CAL_MARK1);
    } else if (s_pos.cal == POS_CAL_WAIT_MARK2) {
        status_led_set(LED_CAL_MARK2);
    } else {
        status_led_set(cal ? LED_OFF : LED_UNCAL);
    }
}

static int32_t hard_cap(void)
{
    /* Watchdog cap applies to calibrated moves only. Every state where the
     * operator jogs with a deadman hold (uncalibrated, Position Unknown,
     * calibration mode) must be uncapped — matching jog()'s target choice —
     * or Re-home/recal jogs would be refused while span_valid is still set. */
    return position_calibrated(&s_pos) ? s_pos.closed_steps + HARD_CAP_MARGIN
                                       : JOG_UNBOUNDED + 1;
}

static void start_move(int32_t target, const motion_profile_t *prof)
{
    blind_store_set_move_flag(true);
    esp_err_t err = motion_start(s_raw, target, prof, hard_cap());
    if (err != ESP_OK) {
        blind_store_set_move_flag(false);
        ESP_LOGW(TAG, "move refused: %s", esp_err_to_name(err));
    } else {
        esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US);
    }
}

static void goto_pct(uint8_t pct)
{
    if (!position_calibrated(&s_pos)) return;
    start_move(position_target_for_pct(&s_pos, pct), &PROF_MOVE);
}

static void jog(bool up)
{
    int32_t target;
    if (position_calibrated(&s_pos)) {
        target = up ? 0 : s_pos.closed_steps;             /* clamped jog */
    } else {
        target = up ? s_raw - JOG_UNBOUNDED : s_raw + JOG_UNBOUNDED;
    }
    start_move(target, &PROF_JOG);
}

static void cal_timeout_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_CAL_TIMEOUT };
    xQueueSend(s_queue, &ev, 0);
}

static void report_tick_cb(void *arg)
{
    /* esp_timer task: only post — s_pos is owned by the dispatcher task, so
     * the live-position computation happens there (no cross-task reads). */
    (void)arg;
    app_event_t ev = { .type = APP_EVT_REPORT_TICK };
    xQueueSend(s_queue, &ev, 0);
}

/* Abort policy: the span stays untouched (spec §6), but if the blind was
 * jogged while in the mode, the stored position no longer matches reality —
 * drop to Position Unknown so a Re-home is demanded instead of trusting
 * stale state. */
static void cal_abort_position_policy(void)
{
    if (s_cal_moved && s_pos.span_valid) {
        position_mark_unknown(&s_pos);
        blind_store_save_position(false, 0);
    }
}

static void enter_or_exit_cal(void)
{
    if (motion_is_moving()) return;              /* only from standstill */
    if (s_cal_mode) {                            /* second long-press: abort */
        position_cal_abort(&s_pos);
        s_cal_mode = false;
        esp_timer_stop(s_cal_timer);
        cal_abort_position_policy();
    } else {
        position_cal_enter(&s_pos);
        s_cal_mode = true;
        s_cal_moved = false;
        s_cal_abort_pending = false;
        s_raw = s_pos.pos_known ? s_pos.cur_steps : 0;   /* fresh raw frame */
        esp_timer_start_once(s_cal_timer, CAL_TIMEOUT_US);
    }
    refresh_outputs();
}

static void handle_mark(void)
{
    if (motion_is_moving()) { motion_stop(); return; }   /* Fn tap = stop first */
    if (!s_cal_mode) return;                             /* idle taps inert */
    /* NOTE: s_raw stays one continuous frame through the whole calibration —
     * position_cal_mark stores mark 1's raw and computes the span as the
     * difference at mark 2, so the caller must NOT re-anchor between marks. */
    if (position_cal_mark(&s_pos, s_raw, MIN_SPAN_STEPS)) {
        if (s_pos.cal == POS_CAL_NONE) {                 /* calibration finished */
            s_cal_mode = false;
            esp_timer_stop(s_cal_timer);
            blind_store_save_span(s_pos.span_valid, s_pos.closed_steps);
            blind_store_save_position(s_pos.pos_known, s_pos.cur_steps);
            s_raw = s_pos.cur_steps;                     /* re-anchor raw frame */
        }
        status_led_flash(LED_ACK);
    } else {
        status_led_flash(LED_ERROR);                     /* stay awaiting mark 2 */
    }
    refresh_outputs();
}

static void toggle_reversed(void)
{
    if (motion_is_moving()) return;
    s_reversed = !s_reversed;
    motion_set_reversed(s_reversed);
    blind_store_save_motor_reversed(s_reversed);
    /* direction sense changed -> all stored steps are meaningless (spec §6) */
    position_wipe(&s_pos);
    if (s_cal_mode) { s_cal_mode = false; esp_timer_stop(s_cal_timer); }
    blind_store_save_span(false, 0);
    blind_store_save_position(false, 0);
    s_raw = 0;
    covering_report_mode(s_reversed);
    status_led_flash(LED_ACK);
    refresh_outputs();
}

/* Spec §7: a command during a move preempts — decelerate to stop, then run
 * the new target (last writer wins). The target is parked until MOTION_DONE. */
static void zb_goto_request(uint8_t pct)
{
    if (!position_calibrated(&s_pos)) return;   /* lockout backstop */
    if (motion_is_moving()) {
        s_pending_pct   = pct;
        s_pending_valid = true;
        motion_stop();
    } else {
        goto_pct(pct);
    }
}

/* ---------- event dispatch ---------- */

static void handle_keypad(kp_event_t e)
{
    s_pending_valid = false;   /* any local input is the last writer (spec §7) */
    bool cal_dev = position_calibrated(&s_pos);
    switch (e.type) {
    case KP_EVT_TAP:
        if (motion_is_moving()) { motion_stop(); break; }   /* any tap stops */
        if (e.key == KEY_FN) { handle_mark(); break; }
        if (cal_dev) {                                       /* full travel */
            goto_pct(e.key == KEY_UP ? 0 : 100);
        }                                                    /* uncal: inert */
        break;
    case KP_EVT_HOLD_START:
        if (!motion_is_moving()) jog(e.key == KEY_UP);
        break;
    case KP_EVT_HOLD_END:
        motion_stop();
        break;
    case KP_EVT_FN_LONG:
        enter_or_exit_cal();
        break;
    case KP_EVT_CHORD_REVERSE:
        toggle_reversed();
        break;
    default:
        break;
    }
}

static void dispatcher_task(void *pv)
{
    (void)pv;
    app_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.type) {
        case APP_EVT_KEYPAD:
            handle_keypad(ev.kp);
            break;
        case APP_EVT_ZB_OPEN:   zb_goto_request(0);      break;
        case APP_EVT_ZB_CLOSE:  zb_goto_request(100);    break;
        case APP_EVT_ZB_GOTO:   zb_goto_request(ev.pct); break;
        case APP_EVT_ZB_STOP:
            s_pending_valid = false;
            motion_stop();
            break;
        case APP_EVT_ZB_SET_REVERSED:
            if (ev.on != s_reversed) toggle_reversed();
            break;
        case APP_EVT_MOTION_DONE:
            esp_timer_stop(s_report_timer);
            s_raw = ev.steps;
            if (s_cal_mode) {
                s_cal_moved = true;
                if (s_cal_abort_pending) {   /* timeout hit mid-jog */
                    s_cal_abort_pending = false;
                    position_cal_abort(&s_pos);
                    s_cal_mode = false;
                    esp_timer_stop(s_cal_timer);
                    cal_abort_position_policy();
                }
            } else {
                position_set_current(&s_pos, position_clamp(&s_pos, ev.steps));
                blind_store_save_position(s_pos.pos_known, s_pos.cur_steps);
            }
            blind_store_set_move_flag(false);
            if (s_pending_valid && !s_cal_mode && position_calibrated(&s_pos)) {
                uint8_t pct = s_pending_pct;   /* ZB preemption: last writer */
                s_pending_valid = false;
                goto_pct(pct);
            }
            refresh_outputs();
            break;
        case APP_EVT_CAL_TIMEOUT:
            if (!s_cal_mode) break;
            if (motion_is_moving()) {
                s_cal_abort_pending = true;    /* finish stopping; abort on DONE
                                                * so the DONE steps aren't written
                                                * into position as trusted state */
                motion_stop();
            } else {
                position_cal_abort(&s_pos);
                s_cal_mode = false;
                cal_abort_position_policy();
                refresh_outputs();
            }
            break;
        case APP_EVT_REPORT_TICK:
            if (motion_is_moving() && position_calibrated(&s_pos)) {
                position_t tmp = s_pos;        /* dispatcher owns s_pos: safe */
                position_set_current(&tmp, motion_current_steps());
                covering_report_lift(position_lift_pct(&tmp));
            }
            break;
        default:
            break;
        }
    }
}

/* ---------- boot ---------- */

void app_main(void)
{
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
    position_init(&s_pos, st.span_valid, st.closed_steps, st.pos_known, st.cur_steps);
    if (st.move_in_progress) {
        /* power died mid-move: position no longer trusted (spec §6) */
        position_mark_unknown(&s_pos);
        blind_store_save_position(false, 0);
        blind_store_set_move_flag(false);
        ESP_LOGW(TAG, "unclean shutdown mid-move -> Position Unknown, re-home needed");
    }
    s_reversed = st.motor_reversed;
    s_raw = s_pos.pos_known ? s_pos.cur_steps : 0;

    motion_pins_t pins = { .gpio_step = PIN_STEP, .gpio_dir = PIN_DIR, .gpio_en = PIN_EN };
    ESP_ERROR_CHECK(motion_init(&pins, s_queue));
    motion_set_reversed(s_reversed);
    ESP_ERROR_CHECK(status_led_init(PIN_LED_EXT, PIN_LED_ONB));
    ESP_ERROR_CHECK(keypad_init(PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_FN, s_queue));

    const esp_timer_create_args_t cal_t = { .callback = cal_timeout_cb, .name = "cal_to" };
    ESP_ERROR_CHECK(esp_timer_create(&cal_t, &s_cal_timer));
    const esp_timer_create_args_t rep_t = { .callback = report_tick_cb, .name = "report" };
    ESP_ERROR_CHECK(esp_timer_create(&rep_t, &s_report_timer));

    covering_set_queue(s_queue);
    /* The lockout flag needs no Zigbee stack — set it truthfully NOW so a
     * calibrated device that joins slowly doesn't reject remote motion in
     * the meantime; attribute sync still happens after the join wait. */
    covering_set_motion_allowed(position_calibrated(&s_pos));

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
        .on_joined         = NULL,   /* initial attribute sync happens below */
        .action_handler    = covering_action_handler,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "starting %s (calibrated=%d reversed=%d)",
             FW_VERSION_STR, position_calibrated(&s_pos), s_reversed);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    covering_report_mode(s_reversed);
    refresh_outputs();

    /* Confirm a pending-verify OTA image once the app is up (join not
     * required) or the bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
```

- [ ] **Step 4: Add `"keypad.c"` to `src/CMakeLists.txt` `SRCS`; build + host tests**

Run: `pio run && pio test -e native`
Expected: build SUCCESS; all host suites still green (24 cases across the three).

- [ ] **Step 5: Commit**

```bash
git add include/keypad.h src/keypad.c src/main.c src/CMakeLists.txt
git commit -m "Wire dispatcher: gesture matrix, calibration flow, z2m lockout, persistence"
```

---

### Task 10: z2m external converter

**Files:**
- Create: `z2m/dfr_roller_blinds.js`

**Interfaces:**
- Consumes: the device's Zigbee surface (Tasks 8–9): model `DFR-RollerBlinds`, Window Covering server with lift %, Mode bit0, ConfigStatus bit0.
- Produces: the converter the z2m instance loads. No firmware dependencies.

- [ ] **Step 1: Write `z2m/dfr_roller_blinds.js`** (complete file; pattern follows `$DS/z2m/dfr_door_sensor.js`)

```js
// zigbee2mqtt external converter for the DFR-RollerBlinds DIY roller blind
// controller (ESP32-C6 Zigbee ROUTER, esp-zigbee 1.6.x). Exposes:
//   - cover (position + open/close/stop) via the standard Window Covering
//     cluster (0x0102). Lift: 0 = open, 100 = closed (ZCL); z2m presents
//     HA cover semantics.
//   - motor_reversed (rw): Window Covering Mode attr (0x0017) bit 0. Flipping
//     it WIPES calibration on the device (by design — direction sense changed).
//   - calibrated (read-only): ConfigStatus (0x0007) bit 0 "Operational".
//     While false the device rejects all motion commands from z2m; calibrate
//     locally with the keypad (hold Fn 3 s; see DEVELOPER_GUIDE.md).
//
// Install (z2m 2.x): copy into the zigbee2mqtt config dir and register under
// `external_converters:` in configuration.yaml, then restart z2m.
//
// OTA: manufacturerCode 0xFEFE + imageType 0x0003 (soil=0x0001, door=0x0002);
// index served from this repo's ota/index.json (see release-ota.yml).

const m = require('zigbee-herdsman-converters/lib/modernExtend');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;
const ea = exposes.access;

const fzCalibrated = {
    cluster: 'closuresWindowCovering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.configStatus !== undefined) {
            return {calibrated: (msg.data.configStatus & 0x01) === 0x01};
        }
    },
};

const fzMotorReversed = {
    cluster: 'closuresWindowCovering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.windowCoveringMode !== undefined) {
            return {motor_reversed: (msg.data.windowCoveringMode & 0x01) === 0x01};
        }
    },
};

const tzMotorReversed = {
    key: ['motor_reversed'],
    convertSet: async (entity, key, value, meta) => {
        const mode = value ? 0x01 : 0x00;
        await entity.write('closuresWindowCovering', {windowCoveringMode: mode});
        return {state: {motor_reversed: value}};
    },
    convertGet: async (entity, key, meta) => {
        await entity.read('closuresWindowCovering', ['windowCoveringMode']);
    },
};

module.exports = [
    {
        zigbeeModel: ['DFR-RollerBlinds'],
        model: 'DFR-RollerBlinds',
        vendor: 'DFRobot-DIY',
        description: 'ESP32-C6 Zigbee-router roller blind controller (DIY)',
        extend: [
            m.windowCovering({controls: ['lift']}),
        ],
        fromZigbee: [fzCalibrated, fzMotorReversed],
        toZigbee: [tzMotorReversed],
        exposes: [
            e.binary('calibrated', ea.STATE_GET, true, false)
                .withDescription('Travel limits calibrated; motion commands are rejected until true'),
            e.binary('motor_reversed', ea.ALL, true, false)
                .withDescription('Flip motor direction (install-time; wipes calibration)'),
        ],
        configure: async (device, coordinatorEndpoint) => {
            const ep = device.getEndpoint(1);
            await ep.read('closuresWindowCovering',
                ['configStatus', 'windowCoveringMode', 'currentPositionLiftPercentage']);
        },
        ota: true,
    },
];
```

- [ ] **Step 2: Sanity-check the JS parses**

Run: `node --check z2m/dfr_roller_blinds.js`
Expected: no output (exit 0). (Full validation happens on the live z2m instance in Task 13.)

- [ ] **Step 3: Commit**

```bash
git add z2m/dfr_roller_blinds.js
git commit -m "Add z2m converter: cover + calibrated + motor_reversed"
```

---

### Task 11: OTA release CI + index

**Files:**
- Create: `.github/workflows/release-ota.yml`, `ota/index.json`

**Interfaces:**
- Consumes: `$LIB` tools (fetched from the esp-zb-common repo at v0.1.1 in CI), `include/ota_ids.h` values (0xFEFE / 0x0003 / DFR-RollerBlinds).
- Produces: on tag `v*`: firmware built with version flags, wrapped as `.ota`, attached to a GitHub release, `ota/index.json` updated on `main`.

**Prerequisite (human, one-time):** the workflow clones the private `esp-zb-common` repo twice (component fetch during `pio run`, tools checkout). Create a fine-grained PAT with read access to `linosteenkamp/esp-zb-common` and add it to this repo's Actions secrets as `ZB_COMMON_PAT`.

- [ ] **Step 1: Write `ota/index.json`** (empty index; CI populates it)

```json
[]
```

- [ ] **Step 2: Write `.github/workflows/release-ota.yml`** (adapted from `$DS/.github/workflows/release-ota.yml`; deltas: image type 0x0003, model id, tools from esp-zb-common@v0.1.1, explicit `--header-string`, git auth for the private component)

```yaml
name: Release OTA
on:
  push:
    tags: ['v*']

permissions:
  contents: write

env:
  OTA_MANUFACTURER_CODE: "0xFEFE"
  OTA_IMAGE_TYPE: "0x0003"
  MODEL_ID: "DFR-RollerBlinds"

jobs:
  build-and-release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          ref: main
          fetch-depth: 0

      - name: Check out esp-zb-common tools
        uses: actions/checkout@v4
        with:
          repository: linosteenkamp/esp-zb-common
          ref: v0.1.1
          token: ${{ secrets.ZB_COMMON_PAT }}
          path: esp-zb-common

      - name: Authorize component-manager clone of esp-zb-common
        run: |
          git config --global url."https://x-access-token:${{ secrets.ZB_COMMON_PAT }}@github.com/linosteenkamp/".insteadOf "https://github.com/linosteenkamp/"

      - name: Derive version from tag
        id: ver
        run: |
          TAG="${GITHUB_REF_NAME#v}"
          IFS=. read -r MA MI PA <<< "$TAG"
          # Semver in the HIGH hex digits (must match OTA_PACK_VERSION in include/ota_ids.h)
          printf 'file_version=0x%X%X%02X0000\n' "$MA" "$MI" "$PA" >> "$GITHUB_OUTPUT"
          printf 'major=%s\nminor=%s\npatch=%s\n' "$MA" "$MI" "$PA" >> "$GITHUB_OUTPUT"

      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Install PlatformIO
        run: pip install platformio

      - name: Build firmware with version flags
        run: |
          PLATFORMIO_BUILD_FLAGS="-DUSE_ZIGBEE -DFW_VER_MAJOR=${{ steps.ver.outputs.major }} -DFW_VER_MINOR=${{ steps.ver.outputs.minor }} -DFW_VER_PATCH=${{ steps.ver.outputs.patch }}" \
            pio run -e dfrobot_firebeetle2_esp32c6_zigbee

      - name: Wrap as Zigbee OTA image
        run: |
          python3 esp-zb-common/tools/make_ota_image.py \
            --in .pio/build/dfrobot_firebeetle2_esp32c6_zigbee/firmware.bin \
            --out "DFR-RollerBlinds-${GITHUB_REF_NAME}.ota" \
            --manufacturer "$OTA_MANUFACTURER_CODE" \
            --image-type "$OTA_IMAGE_TYPE" \
            --file-version "${{ steps.ver.outputs.file_version }}" \
            --header-string "DFR-RollerBlinds OTA"

      - name: Create GitHub release with the .ota asset
        uses: softprops/action-gh-release@v2
        with:
          files: DFR-RollerBlinds-*.ota

      - name: Update ota/index.json on main
        run: |
          python3 esp-zb-common/tools/update_ota_index.py \
            --index ota/index.json \
            --model "$MODEL_ID" \
            --manufacturer "$OTA_MANUFACTURER_CODE" \
            --image-type "$OTA_IMAGE_TYPE" \
            --file-version "${{ steps.ver.outputs.file_version }}" \
            --url "https://github.com/${GITHUB_REPOSITORY}/releases/download/${GITHUB_REF_NAME}/DFR-RollerBlinds-${GITHUB_REF_NAME}.ota" \
            --image "DFR-RollerBlinds-${GITHUB_REF_NAME}.ota"
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git add ota/index.json
          git commit -m "OTA index: ${GITHUB_REF_NAME}"
          git push origin main
```

- [ ] **Step 3: Validate the workflow YAML**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release-ota.yml')); print('ok')"`
Expected: `ok`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release-ota.yml ota/index.json
git commit -m "Add OTA release workflow (image type 0x0003, tools from esp-zb-common)"
```

---

### Task 12: Documentation set

**Files:**
- Create: `CLAUDE.md`, `README.md`, `HARDWARE.md`, `DEVELOPER_GUIDE.md`, `PARTITIONS.md`

**Interfaces:**
- Consumes: everything (documents it).
- Produces: sibling-convention docs. Content requirements below are binding; exact prose is the implementer's, but every listed fact MUST appear and MUST match the code.

- [ ] **Step 1: Write `CLAUDE.md`** covering, in the siblings' format (see `$DS/CLAUDE.md` for tone/structure): project overview (Zigbee ROUTER window covering, esp-zb-common v0.1.1 consumer); build/flash/test commands (`pio run -e dfrobot_firebeetle2_esp32c6_zigbee -t upload -t monitor`, `pio test -e native`); the module table from this plan's File Map; the dispatcher/queue concurrency rule (ISR & action handler never decide; queue to dispatcher); key constants block (GPIO map + motion tuning values, copied from `src/main.c` verbatim); calibration/re-home/lockout behavior summary with pointer to `CONTEXT.md` and the spec; NVS namespace `blind` keys; OTA identity (0xFEFE/0x0003) and the `ZB_COMMON_PAT` CI note.

- [ ] **Step 2: Write `HARDWARE.md`** covering: BOM (spec §2 table incl. LRS-50-24); power chain incl. the ≥100 µF VMOT capacitor and DRV8825 heatsink requirement; DRV8825 wiring table (all 7 GPIOs, M0-M2 hard-wired 1/8, SLEEP̅ tied high); Vref procedure targeting ~1.2 A/phase (Vref = I/2 for standard DRV8825 boards → ~0.6 V, measure between pot and GND, motor disconnected); keypad wiring (common→GND, three lines to GPIO 5/6/7); LED wiring (GPIO14 → resistor → LED → GND); direction check procedure (tap Up; if blind moves down flip `motor_reversed` in z2m or hold Up+Down 3 s).

- [ ] **Step 3: Write `DEVELOPER_GUIDE.md`** covering: pairing flow (permit join, converter install, re-interview); the full calibration walkthrough (Fn 3 s → jog Open → Fn → jog Closed → Fn) incl. LED patterns per state; re-home flow after power-loss; the Task 13 bench checklist (copy it in as a checklist section); OTA release flow (tag `vX.Y.Z` → CI → z2m `zigbee_ota_override_index_location` pointing at this repo's `ota/index.json` raw URL).

- [ ] **Step 4: Write `README.md`** (short: what it is, photo placeholder, feature list, links to HARDWARE.md/DEVELOPER_GUIDE.md/spec) and `PARTITIONS.md` (copy `$DS/PARTITIONS.md` and fix the project name — layout is identical).

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md README.md HARDWARE.md DEVELOPER_GUIDE.md PARTITIONS.md
git commit -m "Add documentation set (CLAUDE, README, HARDWARE, DEVELOPER_GUIDE, PARTITIONS)"
```

---

### Task 13: Bench verification (human-in-the-loop — DO NOT dispatch to a subagent)

No files. This is the on-hardware checklist (also recorded in `DEVELOPER_GUIDE.md` by Task 12). It requires the physical rig: FireBeetle + DRV8825 (Vref set, heatsink fitted) + motor + 24 V PSU + keypad + LED, and a z2m instance with permit-join.

- [ ] Motor bench-run before mounting: direction, 1/8-µstep smoothness/audio, Vref current check
- [ ] Join as router; z2m shows the device via the converter (cover + `calibrated: false`)
- [ ] `motor_reversed` toggle from z2m and from keypad chord; both sides stay in sync; verify it wipes calibration
- [ ] z2m motion commands rejected while uncalibrated (buttons in z2m produce an error/no motion)
- [ ] Calibrate via keypad; deliberately try a wrong mark 2 (above mark 1) → five-flash error, mode stays
- [ ] Full open / close / 50 % from z2m; live position tracks ~1 s during moves
- [ ] Keypad matrix: tap up/down full travel; tap-while-moving stops; hold jogs clamped at limits
- [ ] Power-cut mid-travel → boots Position Unknown (double-flash, z2m locked) → re-home (Fn 3 s, jog Open, Fn)
- [ ] Clean power cycle at rest → still calibrated, taps work immediately
- [ ] Idle back-drive watch: leave the blind mid-travel overnight; if it creeps, revisit idle-hold (spec §2 fallback)
- [ ] OTA round-trip: tag a release, z2m offers + installs it, device reboots into new version (confirm no rollback after a further power cycle), position survives
- [ ] Router relay check with a downstream device

---

## Self-Review Notes

- **Spec coverage:** §2 hardware → Tasks 12 (docs) + 13 (bench; Vref/heatsink/keypad/LED wiring in HARDWARE.md); §2 gesture model + LED table → Tasks 4, 7, 9; §3 library consumption → Task 1 (v0.1.1 pinned); §4 modules/boot/data-flow/concurrency → Tasks 5–9 (queue-only decisions; step ISR IRAM); §5 Zigbee (0x0202, lift 0xFF uncalibrated, Mode bit0 two-way, lockout incl. rationale, converter, OTA apply-when-idle) → Tasks 8–11 — note: **OTA apply-when-idle** is satisfied structurally: the library's `ota_client` reboots on FINISH regardless of motion; mitigation implemented here is that a mid-move reboot is caught by the `move_in_progress` flag → re-home (spec's recovery path). Full defer-reboot-until-idle would need a library hook — recorded as a v0.2 library follow-up in the spec's terms, and flagged for the bench OTA test. §6 motion/calibration/recovery/safety → Tasks 2, 3, 6, 9; §7 error table → Tasks 8–9 (lockout + refusal paths), 6 (hard cap); §8 testing → Tasks 2–4 host suites + Task 13 checklist; §9 docs → Task 12; §10 build order → task order.
- **Deviation to surface honestly (spec §5):** "Apply/reboot only when idle" for OTA is NOT fully implemented (see above). The failure mode (reboot mid-move → Position Unknown → one-mark re-home) is the spec's own recovery path and OTA downloads during motion remain safe (IRAM ISR). If the bench test shows this bites in practice, the fix is an esp-zb-common v0.2 hook (`ota_client` defer-reboot callback).
- **Placeholder scan:** clean — every code step carries complete file contents; Task 12's doc prose is delegated but with binding fact lists (docs, not code).
- **Type consistency:** `app_event_t` fields match across covering.c/motion.c/main.c (`.kp`, `.pct`, `.on`, `.steps`, `.completed`); `position_*`/`ramp_*`/`kp_*`/`motion_*`/`covering_*`/`blind_store_*` signatures identical between headers (Tasks 2–8) and call sites (Task 9); `POSITION_LIFT_UNKNOWN` shared via position.h; queue item size fixed via `app_event.h` everywhere.






