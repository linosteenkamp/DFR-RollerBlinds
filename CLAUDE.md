# CLAUDE.md

This file gives guidance to Claude Code (claude.ai/code) when it works
with the code in this repository.

## Project Overview

This is ESP32-C6 IoT firmware (C and ESP-IDF through PlatformIO) for a
roller blind controller. The controller uses mains power and a stepper
motor. It **joins Zigbee as a Router** and shows a standard **Window
Covering (0x0102)** device to Zigbee2MQTT and Home Assistant. A
TMC2209-driven bipolar stepper moves the blind through a 3D-printed 1:15
reduction. The position is open-loop step counting between soft limits.
A one-time calibration sets these limits, and NVS stores them. A
membrane keypad with three keys (Up, Down and Fn) gives full local
control. It also gives the calibration procedure.

**Target board:** Seeed XIAO ESP32C6 (`seeed_xiao_esp32c6`). It drives a
BIGTREETECH TMC2209 V1.3 in StealthChop2 mode. (Rev 1 used a DFRobot
FireBeetle 2 and a DRV8825. The new hardware kept 1/8 microstepping.
Thus all the step-count calculations and motion constants did not
change.)

**Library:** this project is the first user of `esp-zb-common`. It is
pinned to **v0.2.1** in `src/idf_component.yml`. This shared component
came from the proven Zigbee and OTA code of `../DFR-DoorSensor`. It
supplies two parts:

- `zb_core`: stack initialization, BDB steering and join, and endpoint
  and cluster registration through a builder callback that the
  application supplies.
- `ota_client`: the Zigbee OTA download and the rollback self-check.

The modules of this project are on top of the library.

**Design spec:** [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md)

**Vocabulary:** refer to [CONTEXT.md](CONTEXT.md) for the ubiquitous
language (Open/Closed/Lift, Tap/Hold, Calibration Mode, Mark, Span,
Position Unknown, Re-home, Calibrated, Motor Reversed). This file and
the code use these terms.

## Build, Flash & Test Commands

```bash
# Router build (deployment)
pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor

# Bench build (identical; named env kept for sibling symmetry)
pio run -e seeed_xiao_esp32c6_zigbee_test -t upload -t monitor

# Host tests (position / ramp / keypad_logic / key_filter / trace_ring / ctl — pure C, Unity)
pio test -e native

# Clean / erase flash (before re-provisioning in development)
pio run --target clean
pio run --target erase
```

## Architecture

`app_main()` runs one time and does not sleep:

1. `nvs_flash_init()` and the default event loop.
2. `blind_store_init()` / `position_init()`: restore the span and the
   last position from NVS. After an unclean shutdown during a move (the
   `move_in_progress` flag is still set), the device goes directly to
   Position Unknown.
3. `motion_init()`: the STEP, DIR and EN GPIOs (`EN̅` high = disabled at
   idle), and the GPTimer.
4. `status_led_init()`, `keypad_init()`: the LED pattern player, the
   keypad GPIOs, and the integrator debounce, which sends
   `APP_EVT_KEYPAD` events.
5. `zb_core_init()` (library): starts the Router.
   `covering_build_clusters` / `covering_post_register` register Basic,
   Identify, Window Covering and OTA.
6. `ctl_init()` restores the state. (An unclean shutdown during a move
   sets Position Unknown here.) `app_main` sets the LED from
   `ctl_led_pattern()` and creates the dispatcher. Then it waits for the
   join and sends `APP_EVT_BOOT_SYNC`.

### Module Responsibilities

| Module | File | Purpose | Host tests |
|---|---|---|---|
| `position` | `src/position.c` | Pure: steps↔lift %, clamping, the calibration and re-home state machine, the wipe and unknown transitions | `test/test_position/` |
| `ramp` | `src/ramp.c` | Pure: planning of trapezoid and triangle step intervals | `test/test_ramp/` |
| `ctl` | `src/ctl.c` | Pure: all dispatcher decisions (gesture matrix, calibration procedure, preemption, persistence policy), behind `ctl_ports_t` | `test/test_ctl/` |
| `keypad_logic` | `src/keypad_logic.c` | Pure: press/release + time → Tap / Hold / Fn-long / three-key reset. It classifies each key independently. There are no cross-key latches. | `test/test_keypad/`, `test/test_press_classification/` |
| `blind_store` | `src/blind_store.c` | NVS persistence (namespace `blind`) | — |
| `motion` | `src/motion.c` | GPTimer ISR step generation, DIR/EN, the step counter, done events to the queue | — |
| `status_led` | `src/status_led.c` | LED pattern player (one external LED) | — |
| `covering` | `src/covering.c` | Window Covering cluster build and reports, and the action handler that sends to the queue | — |
| `keypad` | `src/keypad.c` | 5 ms poller (no ISR) + `key_filter` integrator. It sends to `keypad_logic`, and sends events to the queue. The key pins have no GPIO glitch filter. It was removed on 2026-09-26 because it latched a released key as "pressed" while the motor ran (refer to the comment in `src/keypad.c`). | — |
| `key_filter` | `src/key_filter.c` | Pure: integrator debounce. The output changes only after a full traverse from one rail to the other. | `test/test_key_filter/` |
| `trace_ring` | `src/trace_ring.c` | Pure: a ring of decision records with a fixed size | `test/test_trace_ring/` |
| `trace` | `src/trace.c` | RTC_NOINIT storage and the boot dump. The data stays after a reset, but not after a power loss. | — |
| `main` | `src/main.c` | Wiring: the GPIO map, the constants, the ports table that connects ctl to the real modules, the queues, the dispatcher loop | — |
| `app_event` | `include/app_event.h` | The single queue item type that keypad, covering, motion and main share | — |
| `ota_ids` / `fw_version` | `include/ota_ids.h`, `include/fw_version.h` | OTA identity (image type 0x0003) | — |
| converter | `z2m/dfr_roller_blinds.js` | z2m external converter (cover + motor_reversed + calibrated + travel_time) | — |
| OTA CI | `.github/workflows/release-ota.yml` | OTA build and publish when a tag is pushed | — |

### Concurrency rule: ISR and action handler never decide

The GPTimer step ISR (`motion.c`, `IRAM_ATTR`) only decreases a step
budget and toggles STEP. It makes no decisions. It does not use a Zigbee
API or an NVS API. The Zigbee action handler (`covering_action_handler`)
only validates and puts items in the queue. It never calls
`motion_start` or `motion_stop` directly.

**All decisions** occur in `ctl_handle()` (`src/ctl.c`): ramp profile
selection, limit clamping, persistence, reports and lockout checks. Only
`dispatcher_task` (`src/main.c`) calls `ctl_handle()`, with one
`app_event_t` at a time.

The dispatcher waits on a FreeRTOS queue set. The set has the main queue
and a queue with one slot for `MOTION_DONE`. This queue cannot overflow,
because ctl never starts a move while a move is not complete.

ctl owns "move in progress" (`move_active`). The ISR has its own flag,
but that flag clears at the last step, before the final position gets
to the dispatcher. Thus do not use the ISR flag to make a decision.

The step ISR and its data are in IRAM for this reason: flash writes
from the dispatcher (OTA download, NVS commits) must never stop the
steps during a move.

### Key Configuration Constants (`src/main.c`, copied verbatim)

```c
/* ---- GPIO map: XIAO ESP32C6, D-number → GPIO (see HARDWARE.md) ----
 * The D-numbers are what the silkscreen and the wiring harness use; the
 * GPIO numbers below are what the driver API wants. Grouped to match the
 * implementation board's physical layout: driver signals on D7-D9 at one
 * end of the header, keypad on D3-D5 at the other, LED on D2. D6 is left
 * empty between them (v2.4.0 pin map; Fn was on D6 before).
 * D7 carries one of the C6's default UART0 pins — free here because the
 * console runs on USB-Serial-JTAG (CONFIG_ESP_CONSOLE_UART_NUM = -1). */
#define PIN_STEP     19     /* D8 */
#define PIN_DIR      17     /* D7 */
#define PIN_EN       20     /* D9 */
#define PIN_BTN_UP   22     /* D4 */
#define PIN_BTN_DOWN 23     /* D5 */
#define PIN_BTN_FN   21     /* D3 — off D6, away from the driver pins */
#define PIN_LED_EXT  2      /* D2 — sole indicator; no onboard mirror on XIAO */

/* On-board RF switch, not on the header. Nothing drives these unless the
 * firmware does, and left alone the radio barely hears its neighbours
 * (bench3 A/B, 2026-09-27: LQI at the coordinator 77 → 128). Every unit
 * has a U.FL antenna fitted, so the switch is enabled and pointed at it. */
#define PIN_RF_SW_EN 3      /* low = switch enabled */
#define PIN_RF_ANT   14     /* low = ceramic, high = U.FL */

/* ---- motion tuning (bench constants, spec §6) ---- */
#define START_US        500      /* ~2 kHz first/last step */
#define ACCEL_STEPS     800
#define JOG_CRUISE_US   300      /* jog slower than travel but fast enough to
                                  * cross a full 2.5 m span within the cal timeout */
#define JOG_START_US    900      /* jog starts slower than its cruise */
#define MIN_SPAN_STEPS  6000     /* ~1/4 output rev: min valid calibration */
#define HARD_CAP_MARGIN 2400     /* watchdog: allowed overshoot of span */
#define JOG_UNBOUNDED   2000000  /* "infinite" jog target while uncalibrated */
#define CAL_TIMEOUT_US  (10LL * 60 * 1000000)  /* must exceed a full-span jog */
#define REPORT_PERIOD_US (1LL * 1000000)
```

The cruise speed (of the travel move, not the jog) is not a compile-time
constant. It is a runtime setting for each unit:

- z2m writes it as a full-travel duration in seconds.
- NVS stores it under the key `spd`.
- `ramp_us_from_travel_time()` in `src/ramp.c` converts it to
  `PROF_MOVE.cruise_us` from the calibrated span.

Before a value is set, the default is `RAMP_DEFAULT_CRUISE_US`
(`include/ramp.h`).

At 1/8 microstep (M0-M2 hard-wired) through the 1:15 reduction, there
are 1600 microsteps for each motor revolution and 24 000 microsteps for
each output revolution.

## Calibration, Re-home & Lockout

[CONTEXT.md](CONTEXT.md) (vocabulary) and the design spec give the full
behavior: §6 (calibration and recovery state machine) and §5 (Zigbee
lockout). This is a summary to help you find the code:

- **Calibration Mode**: to enter it, hold Fn for approximately 3 s at
  standstill (`enter_or_exit_cal()` in `main.c`, with
  `position_cal_enter()`). From Calibrated or never calibrated, the
  device does a full calibration (two marks: Open, then Closed). From
  Position Unknown, it does a **Re-home** (the span stays, one mark).
  `position_cal_mark()` makes sure that mark 2 is below mark 1 by
  `MIN_SPAN_STEPS` or more. The device rejects a mark that is not valid
  (`LED_ERROR`), and the mode continues to wait for a mark.
- **Abort** (a second long press on Fn, or the 10-minute timeout,
  `CAL_TIMEOUT_US`): the span and the position do not change. The
  exception is when you jogged the blind during the aborted session.
  Then the position goes to Position Unknown
  (`cal_abort_position_policy()`). The device does not trust the old
  state.
- **Position Unknown** occurs when the device cannot trust the stored
  position. Two events cause it: a boot with `move_in_progress` still set
  (the power failed during a move), or a calibration session that was
  jogged and then aborted. The span stays. Only a Re-home restores
  trust.
- **Lockout**: `position_calibrated()` controls two things. It controls
  remote motion (`covering_set_motion_allowed()` / the backstop check in
  `zb_goto_request()`). It also controls taps (`handle_keypad()`: taps
  do nothing on a device that is not calibrated). Hold-to-jog always
  operates, without a limit.
- **Motor Reversed** toggle (`toggle_reversed()`): erases the span and
  the position through `position_wipe()`. All the old step counts were
  measured with the old direction.

## NVS

Namespace **`blind`** (`blind_store.c`), keys:

| Key | Type | Meaning |
|---|---|---|
| `span_ok` | u8 bool | `span_valid`: you can trust `span` |
| `span` | i32 | `closed_steps`: the calibrated travel from Open to Closed |
| `pos_ok` | u8 bool | `pos_known`: you can trust `pos` |
| `pos` | i32 | `cur_steps`: the last known absolute position |
| `rev` | u8 bool | `motor_reversed` |
| `spd` | u16 | The full-travel time in seconds. `0` = never set; use `RAMP_DEFAULT_CRUISE_US`. |
| `moving` | u8 bool | `move_in_progress`: set when a move starts, cleared when the move completes correctly |

## OTA

Identity (`include/ota_ids.h`): `OTA_MANUFACTURER_CODE = 0xFEFE`,
`OTA_IMAGE_TYPE = 0x0003` (soil tracker = 0x0001, door sensor = 0x0002),
`OTA_MODEL_ID = "DFR-RollerBlinds"`.

`.github/workflows/release-ota.yml` builds and publishes an image when a
`v*` tag is pushed. It checks out `linosteenkamp/esp-zb-common` (now
public) for its `tools/`. It does not need a secret or a PAT. Before, it
used a fine-grained `ZB_COMMON_PAT` while that repo was private. The PAT
frequently gave a 403 error (expiry or scope). Thus the repo was made
public, and the authentication steps were removed from the workflow.

For the full pairing procedure, the calibration procedure, the bench
checklist and the release procedure, refer to
[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md). For the wiring and the Vref
procedure, refer to [HARDWARE.md](HARDWARE.md).

## Agent skills

### Issue tracker

The issues are in GitHub Issues on `linosteenkamp/DFR-RollerBlinds`
(through the `gh` CLI). Refer to `docs/agents/issue-tracker.md`.

### Triage labels

The default vocabulary is `needs-triage`, `needs-info`,
`ready-for-agent`, `ready-for-human` and `wontfix`. Refer to
`docs/agents/triage-labels.md`.

### Domain docs

Single context: `CONTEXT.md` and `docs/adr/` at the repo root. Refer to
`docs/agents/domain.md`.
