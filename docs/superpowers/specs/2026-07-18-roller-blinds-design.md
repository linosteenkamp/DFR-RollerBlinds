# DFR-RollerBlinds — Zigbee Roller Blind Controller + `esp-zb-common` Library

**Date:** 2026-07-18
**Status:** Approved design (brainstormed and validated section-by-section)
**Siblings:** `../DFR-MoistureTracker` (1st), `../DFR-DoorSensor` (2nd) — this is the
3rd Zigbee2MQTT (z2m) project in the family and the agreed trigger for extracting
the shared `esp-zb-common` library (deferred in the DoorSensor spec).

## 1. Summary

ESP32-C6 firmware (C / ESP-IDF via PlatformIO) for a stepper-driven roller blind
controller that joins the Zigbee mesh as a **Router** (permanently mains-powered)
and presents a standard **Window Covering (0x0102)** device to Zigbee2MQTT /
Home Assistant. Motion is a DRV8825-driven bipolar stepper through a 3D-printed
**1:15 reduction** (`Blinds 2.step`). Position is open-loop step counting against
one-time calibrated soft limits stored in NVS. A 3-key membrane keypad
(Up / Down / Fn) gives full local control and hosts the calibration UX.

Two deliverables, in order:

1. **`esp-zb-common`** — new sibling library repo extracting DoorSensor's proven
   Zigbee/OTA modules and tooling. Consumed via PlatformIO `lib_deps`.
2. **DFR-RollerBlinds firmware** — this repo, first consumer of the library.

Retrofitting DoorSensor / MoistureTracker onto the library is **explicitly out of
scope** (separate later effort — no risk to deployed devices).

## 2. Hardware

### Bill of materials

| Part | Role |
|---|---|
| DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`) | Controller (fallback: Seeed XIAO ESP32-C6 if it doesn't fit the enclosure — design keeps GPIO budget to 6 pins so fallback is a pin remap, not a redesign) |
| 2HS60-1504JA05-020-03 bipolar stepper | Drive motor — 1.8°/step (200 full steps/rev), 1.5 A/phase class |
| DRV8825 breakout | Stepper driver, 1/8 microstep (hard-wired) |
| 3D-printed geartrain (`Blinds 2.step`) | 1:15 reduction to the roller tube |
| Mean Well 24 V DC PSU — **recommend LRS-50-24** (LRS-35-24 acceptable) | System supply |
| Step-down regulator 5 V / 3.2 A (VIN 5.3–50 V) | 24 V → 5 V for the FireBeetle |
| Membrane keypad: 2 arrow keys + function key (in stock) | Local controls + calibration UX |

### Power chain

- 24 V → **DRV8825 VMOT**, with the mandatory **≥100 µF electrolytic across
  VMOT/GND close to the driver** (spike protection — non-negotiable).
- 24 V → step-down regulator → 5 V → FireBeetle 5V/VCC pin. ESP32-C6 peak draw is
  well under 1 A; large margin.

### DRV8825 wiring & setup

- `STEP`, `DIR`, `EN̅` → three GPIOs. Exact pins chosen at implementation time,
  avoiding ESP32-C6 strapping pins (GPIO8/9/15), USB-JTAG pins, and FireBeetle
  reserved pins (same rule as siblings).
- `M0/M1/M2` **hard-wired for 1/8 microstep** (not firmware-controlled; saves 3
  GPIOs; bench-retunable by rewiring). → **1600 microsteps/motor-rev**,
  **24 000 microsteps/output-rev** through the 1:15 reduction.
- `SLEEP̅` tied high (to `RESET̅`); idle power-down is done via `EN̅`.
- Current limit via Vref pot: **~1.2 A/phase** (80 % of rating). Procedure
  documented in `HARDWARE.md`.

### Idle behavior (decided)

Driver **disabled at idle** (`EN̅` high after each move): silent, cool, near-zero
idle draw. Relies on the 1:15 gearing not back-driving — verify on the bench. If
the blind creeps, fallback is a build flag enabling idle hold (interface already
supports it since `EN̅` is firmware-controlled).

### Keypad

Membrane keypad common → GND; **Up / Down / Fn** → three GPIOs with internal
pull-ups, firmware debounce (library `debounce` module).

- **Up / Down** — tap: full open / close (when calibrated; uncalibrated devices
  jog on tap too, §5). Hold: jog while held.
- **Fn** — tap: **stop** any motion. Long-press (~3 s): enter/exit
  **calibration mode** (§6).

GPIO budget: 6 (STEP, DIR, EN, BTN_UP, BTN_DOWN, BTN_FN).

## 3. `esp-zb-common` Library (Deliverable 1)

New repo `~/Developer/499/esp-zb-common`, pushed to GitHub, consumed via
`platformio.ini` `lib_deps` pinned to a **version tag** (local-path override
during active development). Extracted from DoorSensor's proven code:

| Component | Source | Notes |
|---|---|---|
| `zb_core` | generalized `zb_router.c` | Stack init, BDB steering/join, network-state restore, signal handler; endpoint/cluster registration via an **app-supplied builder callback**. Role is a parameter — `ROUTER` implemented now; the enum includes `ED` so the interface won't churn, but end-device support ships with the MoistureTracker retrofit, not now. |
| `ota_client` | `ota_client.c` | Zigbee OTA download + rollback self-check. Already ported once (MoistureTracker → DoorSensor); interface is stable. Per-app manufacturer/image IDs stay in each app's `ota_ids.h`. |
| `debounce` | `debounce.c` | Pure, host-testable; reused verbatim for the keypad. |
| `tools/` | `make_ota_image.py`, `update_ota_index.py` + pytest suite | Ends per-repo copy-paste of OTA packaging. |
| Reference templates | `partitions.csv` (dual-OTA), `sdkconfig.defaults.zigbee` | PlatformIO can't ship these as library artifacts; they are copied into consumers but canonical here, with a README note. |

**Not extracted now:** `ias_zone` (no consumer in this project — moves in with the
DoorSensor retrofit), WiFi/MQTT/config-portal modules (unused by Zigbee builds).

Library has its own Unity tests (pure modules), pytest (tools), and a README
documenting the `zb_core` builder interface.

## 4. Firmware Architecture (Deliverable 2)

DoorSensor skeleton: `app_main()` runs once, never sleeps; Zigbee **Router**
(radio always on, relays for the mesh); everything after boot is event-driven.

### Boot sequence

1. `init_system()` — NVS flash, event loop.
2. `motion_init()` — STEP/DIR/EN GPIOs (`EN̅` high = disabled), GPTimer, load
   calibration + last position from NVS.
3. `keypad_init()` — 3 GPIOs + ISR + debounce → control events.
4. `zb_core_init()` (library) — router bring-up; endpoint builder registers
   Basic / Identify / Window Covering / OTA clusters.
5. Return; stack task runs forever.

### Modules (this repo, on top of the library)

| Module | Purpose | Host-testable |
|---|---|---|
| `motion` | GPTimer-ISR step-pulse generation, trapezoidal ramp, step counting, `EN̅` idle control | ramp math yes (pure fn) |
| `position` | Pure logic: steps ↔ lift-%, soft-limit clamping, calibration state machine, position-unknown tracking | **yes** |
| `blind_store` | NVS persistence: `closed_steps` span, last position, `move_in_progress` flag | via `nvs_shim` pattern |
| `covering` | Window Covering cluster glue: commands → `motion`; attribute reports → z2m | no |
| `keypad` | Debounce + tap / hold / long-press classification → control events | classifier yes |
| `main` | Wiring, config constants | — |

### Data flow

Command (z2m cluster cmd **or** keypad event) → `position` computes target step,
clamps to limits → `motion` ramps to target, counting steps → on arrival or Stop:
motor stops, driver disabled, `blind_store` persists position, `covering` reports
final lift-%. During moves, `covering` reports intermediate position ~every 1 s so
the HA slider tracks live.

**Concurrency rule:** the GPTimer ISR only decrements a step budget and toggles
STEP. All decisions (ramp profile, limits, persistence, reporting) run in task
context via a FreeRTOS queue. **No Zigbee or NVS calls from ISR.**

## 5. Zigbee Behavior & z2m Integration

- **Endpoint:** single, Home Automation profile, device id
  `ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID` (0x0202).
- **Clusters:** Basic (0x0000 — manufacturer/model identity strings, sibling
  convention, default model `blinds01`), Identify (0x0003), **Window Covering
  (0x0102) server**, OTA Upgrade client (0x0019).
- **Window Covering:**
  - `WindowCoveringType` = Rollershade (0x00).
  - Commands: **UpOpen (0x00), DownClose (0x01), Stop (0x02),
    GoToLiftPercentage (0x05)**.
  - Reported: **CurrentPositionLiftPercentage (0x0008)** — ZCL convention
    0 = open, 100 = closed; the z2m converter maps to HA cover semantics.
  - **Uncalibrated:** lift reported as unknown (0xFF); `GoToLiftPercentage`
    rejected (ZCL default response, failure status). Up/Down/Stop still work
    (jog-style, unclamped — operator beware, same as calibration mode).
- **Join:** BDB steering + network-state restore, inherited from `zb_core`.
- **Converter:** `z2m/dfr_roller_blinds.js` (pattern of `dfr_door_sensor.js`) —
  matches manufacturer+model, exposes a native **cover** (position,
  open/close/stop) plus a `calibrated` diagnostic flag. Friendly-naming in z2m UI.
- **OTA:** shared-library `tools/` build the images; served by the existing
  `dfr-ota-server`; bootloader rollback + self-check via library `ota_client`.

## 6. Motion Control, Calibration & Recovery

### Motion profile

Trapezoidal ramp (accelerate → cruise → decelerate, stopping exactly on the
target step). Cruise/accel are `#define` constants tuned on the bench. Starting
point: **~3 kHz cruise** at 1/8 microstep ≈ 11 s for ~17 output revs of travel.
Direction reversals always pass through a full stop.

### Step accounting

Absolute position = signed microsteps from fully-open reference (0). Calibration
stores the span `closed_steps` in NVS. Lift-% maps linearly onto
[0, `closed_steps`].

### Calibration (local, keypad-only)

1. Hold **Fn** ~3 s → calibration mode (soft limits off).
2. Jog to **fully open**, tap Fn → zero reference set.
3. Jog to **fully closed**, tap Fn → span saved to NVS, mode exits, device
   reports calibrated + 100 % to z2m.

Abort: second Fn long-press, or 5-minute timeout — no save.

### Power-loss / drift recovery

- Position written to NVS **after each completed move** (not during — NVS wear).
- `move_in_progress` NVS flag set at move start, cleared on clean completion.
  On boot with the flag set: position unknown → device keeps the calibration
  span but demands a **re-home**: jog to fully open, tap Fn once (re-zero).
  Much lighter than full recalibration.
- Same re-home fixes drift if the blind ever back-drives while idle.

### Safety clamps

- All targets clamp to [0, `closed_steps`].
- Motion refuses to start when uncalibrated (except jog).
- Runaway watchdog: any move exceeding `closed_steps` + margin is aborted
  (software backstop for logic bugs — DRV8825 breakouts expose no fault pin).

## 7. Error Handling

| Condition | Behavior |
|---|---|
| Zigbee down / not joined | Keypad retains full control; motion never depends on the stack. Join retries per `zb_core` steering backoff. |
| Invalid command (>100 %, or GoTo while uncalibrated) | ZCL default response with failure status; no motion. |
| Command during a move | Clean preemption: decelerate to stop, then execute new target. Keypad and Zigbee equal priority, last-writer-wins. |
| NVS write failure | Logged; move completes; position flagged dirty → next boot demands re-home rather than trusting stale data. |
| Driver electrical fault | Not detectable in v1 (no fault pin); runaway watchdog is the backstop. |

## 8. Testing

- **Host (`pio test -e native`, Unity):** `position` (steps↔%, clamping,
  calibration state machine, recovery-flag logic), `keypad` classifier
  (tap/hold/long-press), ramp math as a pure function (distance →
  accel/cruise/decel step counts). Library `debounce` already covered in its repo.
- **Library repo:** Unity tests for pure modules, builder-interface compile
  checks, pytest for OTA tools.
- **On-device (manual checklist in `DEVELOPER_GUIDE.md`):** bench motor run
  before mounting (Vref set, direction, microstep audio check); calibration;
  full open / close / 50 % from z2m; keypad tap/hold/Fn-stop; power-cut
  mid-travel → re-home flow; OTA round-trip; router relay check with a
  downstream device.

## 9. Documentation Set (sibling conventions)

`CLAUDE.md`, `README.md`, `HARDWARE.md` (wiring diagram, Vref procedure, PSU
choice), `DEVELOPER_GUIDE.md` (pairing + verification checklists),
`PARTITIONS.md` (dual-OTA layout, copied from template), `z2m/dfr_roller_blinds.js`.

## 10. Build Order (input to the implementation plan)

1. `esp-zb-common` repo — extract modules + tools, tests green, tag v0.1.0.
2. Blinds firmware skeleton consuming the library (builds, joins as router).
3. `motion` / `position` / `keypad` on the bench (host tests + real motor).
4. Window Covering cluster + z2m converter, live position reporting.
5. OTA integration + documentation set.

## 11. Out of Scope

- Retrofitting DoorSensor / MoistureTracker onto `esp-zb-common`.
- `ias_zone` extraction, end-device (`ED`) role support in `zb_core`.
- Endstop/homing sensors, closed-loop position feedback, stall detection.
- Firmware-controlled microstepping, speed configurable via z2m.
- Multi-blind grouping/scenes (done in z2m/HA, not firmware).
