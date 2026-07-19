# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C6 IoT firmware (C / ESP-IDF via PlatformIO) for a **mains-powered
stepper-driven roller blind controller that joins Zigbee as a Router** and
presents a standard **Window Covering (0x0102)** device to Zigbee2MQTT / Home
Assistant. Motion comes from a DRV8825-driven bipolar stepper through a
3D-printed 1:15 reduction; position is open-loop step counting against
one-time-calibrated soft limits stored in NVS. A 3-key membrane keypad
(Up / Down / Fn) gives full local control and hosts the calibration UX.

**Target board:** DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`)

**Library:** this is the first consumer of `esp-zb-common` (pinned to
**v0.1.1** in `src/idf_component.yml`), the shared component extracted from
`../DFR-DoorSensor`'s proven Zigbee/OTA code. It supplies `zb_core` (stack
init, BDB steering/join, endpoint/cluster registration via an app-supplied
builder callback) and `ota_client` (Zigbee OTA download + rollback
self-check). This project's own modules sit on top of the library.

**Design spec:** [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md)

**Vocabulary:** see [CONTEXT.md](CONTEXT.md) for the ubiquitous language
(Open/Closed/Lift, Tap/Hold, Calibration Mode, Mark, Span, Position Unknown,
Re-home, Calibrated, Motor Reversed) — used throughout this file and the code.

## Build, Flash & Test Commands

```bash
# Router build (deployment)
pio run -e dfrobot_firebeetle2_esp32c6_zigbee -t upload -t monitor

# Bench build (identical; named env kept for sibling symmetry)
pio run -e dfrobot_firebeetle2_esp32c6_zigbee_test -t upload -t monitor

# Host tests (position / ramp / keypad_logic — pure C, Unity)
pio test -e native

# Clean / erase flash (before re-provisioning in development)
pio run --target clean
pio run --target erase
```

## Architecture

`app_main()` runs once and never sleeps:

1. `nvs_flash_init()` + default event loop.
2. `blind_store_init()` / `position_init()` — restore span + last position
   from NVS; an unclean shutdown mid-move (`move_in_progress` flag still set)
   drops straight to Position Unknown.
3. `motion_init()` — STEP/DIR/EN GPIOs (`EN̅` high = disabled at idle), GPTimer.
4. `status_led_init()`, `keypad_init()` — LED pattern player, keypad GPIOs +
   debounce → `APP_EVT_KEYPAD` events.
5. `zb_core_init()` (library) — Router bring-up; `covering_build_clusters` /
   `covering_post_register` register Basic / Identify / Window Covering / OTA.
6. `dispatcher_task` is created and the stack task runs forever; everything
   after boot is event-driven through one queue.

### Module Responsibilities

| Module | File | Purpose | Host tests |
|---|---|---|---|
| `position` | `src/position.c` | Pure: steps↔lift %, clamping, calibration/re-home state machine, wipe/unknown transitions | `test/test_position/` |
| `ramp` | `src/ramp.c` | Pure: trapezoid/triangle step-interval planning | `test/test_ramp/` |
| `keypad_logic` | `src/keypad_logic.c` | Pure: press/release+time → Tap / Hold / Fn-long / Up+Down-chord | `test/test_keypad/` |
| `blind_store` | `src/blind_store.c` | NVS persistence (namespace `blind`) | — |
| `motion` | `src/motion.c` | GPTimer ISR step generation, DIR/EN, step counter, done-events to queue | — |
| `status_led` | `src/status_led.c` | LED pattern player (external + onboard mirror) | — |
| `covering` | `src/covering.c` | Window Covering cluster build/report + action-handler → queue | — |
| `keypad` | `src/keypad.c` | 20 ms poller (no ISR) + library debounce → feeds `keypad_logic`, events to queue | — |
| `main` | `src/main.c` | Wiring, GPIO map, constants, dispatcher task (gesture matrix + calibration flow) | — |
| `app_event` | `include/app_event.h` | The one queue item type shared by keypad/covering/motion/main | — |
| `ota_ids` / `fw_version` | `include/ota_ids.h`, `include/fw_version.h` | OTA identity (image type 0x0003) | — |
| converter | `z2m/dfr_roller_blinds.js` | z2m external converter (cover + motor_reversed + calibrated) | — |
| OTA CI | `.github/workflows/release-ota.yml` | Tag-triggered OTA build/publish | — |

### Concurrency rule: ISR and action handler never decide

The GPTimer step ISR (`motion.c`, `IRAM_ATTR`) only decrements a step budget
and toggles STEP — it makes no decisions and touches no Zigbee/NVS API. The
Zigbee action handler (`covering_action_handler`) only validates and enqueues
— it never calls `motion_start`/`motion_stop` directly. **Every decision**
(ramp profile choice, limit clamping, persistence, reporting, lockout checks)
happens in `dispatcher_task` (`src/main.c`), which owns `s_pos` and all mutable
state and processes one `app_event_t` at a time off a single FreeRTOS queue.
This is why the step ISR and its data live in IRAM: flash writes (OTA
download, NVS commits) driven from the dispatcher must never stall stepping
mid-move.

### Key Configuration Constants (`src/main.c`, copied verbatim)

```c
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
```

At 1/8 microstep (M0-M2 hard-wired) through the 1:15 reduction that is
1600 microsteps/motor-rev, 24 000 microsteps/output-rev.

## Calibration, Re-home & Lockout

Full behavioral detail lives in [CONTEXT.md](CONTEXT.md) (vocabulary) and the
design spec §6 (calibration/recovery state machine) and §5 (Zigbee lockout).
Summary for code navigation:

- **Calibration Mode** is entered by holding Fn ~3 s from standstill
  (`enter_or_exit_cal()` in `main.c`, backed by `position_cal_enter()`).
  Calibrated/never-calibrated → full calibration (two marks, Open then
  Closed); Position Unknown → **Re-home** (span kept, one mark).
  `position_cal_mark()` validates mark 2 lies below mark 1 by at least
  `MIN_SPAN_STEPS`; an invalid mark is rejected (`LED_ERROR`, mode stays
  waiting).
- **Abort** (second Fn long-press or 5-minute timeout,
  `CAL_TIMEOUT_US`): span/position untouched **unless** the blind was jogged
  during the aborted session, in which case position drops to Position
  Unknown (`cal_abort_position_policy()`) rather than trusting stale state.
- **Position Unknown** happens whenever stored position can no longer be
  trusted: boot with `move_in_progress` still set (power died mid-move), or
  a jogged-then-aborted calibration session. Span is kept; only a Re-home
  restores trust.
- **Lockout**: `position_calibrated()` gates both remote motion
  (`covering_set_motion_allowed()` / `zb_goto_request()`'s backstop check)
  and taps (`handle_keypad()` — taps are inert on an uncalibrated device;
  hold-to-jog always works, uncapped).
- **Motor Reversed** toggle (`toggle_reversed()`) wipes span and position via
  `position_wipe()` — all prior step counts were measured under the old
  direction sense.

## NVS

Namespace **`blind`** (`blind_store.c`), keys:

| Key | Type | Meaning |
|---|---|---|
| `span_ok` | u8 bool | `span_valid` — `span` is trustworthy |
| `span` | i32 | `closed_steps` — calibrated Open→Closed travel |
| `pos_ok` | u8 bool | `pos_known` — `pos` is trustworthy |
| `pos` | i32 | `cur_steps` — last known absolute position |
| `rev` | u8 bool | `motor_reversed` |
| `moving` | u8 bool | `move_in_progress` — set at move start, cleared on clean completion |

## OTA

Identity (`include/ota_ids.h`): `OTA_MANUFACTURER_CODE = 0xFEFE`,
`OTA_IMAGE_TYPE = 0x0003` (soil tracker = 0x0001, door sensor = 0x0002),
`OTA_MODEL_ID = "DFR-RollerBlinds"`. `.github/workflows/release-ota.yml`
builds and publishes on a `v*` tag; it checks out the private
`linosteenkamp/esp-zb-common` repo twice (component fetch during `pio run`,
and the `tools/` checkout for image packaging) using the `ZB_COMMON_PAT`
repo secret — a fine-grained PAT with read access to that repo. Without it
CI fails at the component-manager clone step.

See [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) for the full pairing, calibration
walkthrough, bench checklist, and release flow, and
[HARDWARE.md](HARDWARE.md) for wiring and the Vref procedure.
