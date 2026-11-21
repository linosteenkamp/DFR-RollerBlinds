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

# Gearbox test rig (FW_APP=rig; no Zigbee, no keypad; refer to RIG_GUIDE.md)
pio run -e seeed_xiao_esp32c6_rig -t upload -t monitor

# Host tests (position / ramp / keypad_logic / key_filter / trace_ring / ctl / rig_ctl — pure C, Unity)
pio test -e native

# Clean / erase flash (before re-provisioning in development)
pio run --target clean
pio run --target erase
```

## Architecture

### Module Responsibilities

| Module | File | Purpose | Host tests |
|---|---|---|---|
| `position` | `src/position.c` | Pure: steps↔lift %, clamping, the calibration and re-home state machine, the wipe and unknown transitions | `test/test_position/` |
| `ramp` | `src/ramp.c` | Pure: planning of trapezoid and triangle step intervals | `test/test_ramp/` |
| `ctl` | `src/ctl.c` | Pure: all dispatcher decisions (gesture matrix, calibration procedure, preemption, persistence policy), behind `ctl_ports_t` | `test/test_ctl/` |
| `keypad_logic` | `src/keypad_logic.c` | Pure: press/release + time → Tap / Hold / Fn-long / three-key reset. It classifies each key independently. There are no cross-key latches. | `test/test_keypad/`, `test/test_press_classification/` |
| `blind_store` | `src/blind_store.c` | NVS persistence (namespace `blind`) | — |
| `motion` | `src/motion.c` | GPTimer ISR step generation, DIR/EN, the step counter. It posts its own `motion_done_t` item to a queue with one slot. The blind dispatcher converts that item to `APP_EVT_MOTION_DONE`. The driver is disabled after each move, unless hold at idle is set (the test rig will set it after ticket #33). | — |
| `rf_switch` | `src/rf_switch.c` | Enables the XIAO RF switch and selects the antenna, before the radio starts | — |
| `status_led` | `src/status_led.c` | LED pattern player (one external LED) | — |
| `covering` | `src/covering.c` | Window Covering cluster build and reports, and the action handler that sends to the queue | — |
| `keypad` | `src/keypad.c` | 5 ms poller (no ISR) + `key_filter` integrator. It sends to `keypad_logic`, and sends events to the queue. The key pins have no GPIO glitch filter. It was removed on 2026-09-26 because it latched a released key as "pressed" while the motor ran (refer to the comment in `src/keypad.c`). | — |
| `key_filter` | `src/key_filter.c` | Pure: integrator debounce. The output changes only after a full traverse from one rail to the other. | `test/test_key_filter/` |
| `trace_ring` | `src/trace_ring.c` | Pure: a ring of decision records with a fixed size | `test/test_trace_ring/` |
| `trace` | `src/trace.c` | RTC_NOINIT storage and the boot dump. The data stays after a reset, but not after a power loss. | — |
| `main` | `src/main.c` | Wiring: the GPIO map, the constants, the ports table that connects ctl to the real modules, the queues, the dispatcher loop | — |
| `app_event` | `include/app_event.h` | The single queue item type that keypad, covering and main share (`motion` has its own `motion_done_t`) | — |
| `ota_ids` / `fw_version` | `include/ota_ids.h`, `include/fw_version.h` | OTA identity (image type 0x0003) | — |
| converter | `z2m/dfr_roller_blinds.js` | z2m external converter (cover + motor_reversed + calibrated + travel_time) | — |
| OTA CI | `.github/workflows/release-ota.yml` | OTA build and publish when a tag is pushed | — |

The test rig firmware (`FW_APP=rig`) has these modules. Refer to
[RIG_GUIDE.md](RIG_GUIDE.md).

| Module | File | Purpose | Host tests |
|---|---|---|---|
| `rig_ctl` | `src/rig_ctl.c` | Pure: all the rig decisions, behind `rig_ports_t`. Only the dispatcher calls `rig_handle()`. | `test/test_rig_ctl/` |
| `rig_cmd` | `src/rig_cmd.c` | Pure: the command parser (text → `rig_cmd_t`). It checks only the syntax. It also assembles the console lines (Backspace and DEL remove a byte). | `test/test_rig_ctl/` |
| `rig_record` | `src/rig_record.c` | Pure: the topic and the JSON text of a published record | `test/test_rig_ctl/` |
| `rig_console` | `src/rig_console.c` | The serial console: it reads the command lines, and it is the publish port (it does not block) | — |
| `rig_main` | `src/rig_main.c` | Wiring: the GPIO map, the constants, the ports table, the queues, the dispatcher loop | — |
| `rig_event` | `include/rig_event.h` | The queue item of the rig | — |

### Shared modules and FW_APP

`motion`, `ramp`, `key_filter` and `rf_switch` are shared modules. The
blind firmware and the test rig use them. The CMake variable `FW_APP`
selects the source list of the firmware. The default is `blind`. The
env `seeed_xiao_esp32c6_rig` sets `FW_APP=rig` in
`board_build.cmake_extra_args`. A shared module
must not include `app_event.h` or another header of only one firmware.
An env can set `RAMP_MIN_CRUISE_US` with a build flag. The blind keeps
60 µs. The rig env sets 40 µs, and `rig_main.c` stops a build without it.

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

### Motion constants

The GPIO map and the motion constants are in `src/main.c`.

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

## Documentation language: ASD-STE100

Write all the documentation in this repository in ASD-STE100 Simplified
Technical English. This applies to READMEs, guides, specs, plans, ADRs,
code comments, commit messages, PR descriptions and all other written
documentation.

Key rules:

- Use only approved words with their approved meanings. Use one word for
  one meaning, and one meaning for one word. Technical names and
  technical verbs (part names, tool names, code identifiers, commands)
  are permitted.
- Do not use synonyms for the same thing. Use the same term every time.
- Procedural sentences: maximum 20 words. Descriptive sentences: maximum
  25 words.
- Write one instruction per sentence. Put the instructions in the
  sequence that the reader does them.
- Use the imperative (command) form for instructions: "Install the
  bearing.", not "The bearing should be installed."
- Use the active voice. Use the passive voice only in descriptive text,
  and only when necessary.
- Use only these verb forms: infinitive, imperative, simple present,
  simple past, simple future, and past participle as an adjective. Do
  not use "-ing" forms as verbs.
- Do not use noun clusters of more than three words. Break up long
  clusters or use hyphens.
- Use articles ("the", "a", "an") and demonstratives where possible.
- Keep paragraphs to one topic, with a maximum of six sentences.
- Put a warning or caution before the instruction it applies to. Start
  it with a clear command, then give the risk.
- Do not use idioms, slang, or contractions.

Code, command output and quoted text keep their original wording. Do not
rewrite dated records (specs, plans, research notes) only to change
their language.
