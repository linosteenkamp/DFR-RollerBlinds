# DFR-RollerBlinds — Zigbee Roller Blind Controller + `esp-zb-common` Library

**Date:** 2026-07-18 (revised same day after grilling review)
**Status:** Approved design (brainstormed, validated section-by-section, then
stress-tested in a grilling session — control model, calibration lifecycle, and
direction handling revised as a result)
**Vocabulary:** see `CONTEXT.md` (Open/Closed/Lift, Tap/Hold, Calibration Mode,
Mark, Span, Position Unknown, Re-home, Calibrated, Motor Reversed)
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
| DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`) | Controller (fallback: Seeed XIAO ESP32-C6 if it doesn't fit the enclosure — design keeps GPIO budget to 7 pins so fallback is a pin remap, not a redesign) |
| 2HS60-1504JA05-020-03 bipolar stepper | Drive motor — 1.8°/step (200 full steps/rev), 1.5 A/phase class |
| DRV8825 breakout | Stepper driver, 1/8 microstep (hard-wired) |
| 3D-printed geartrain (`Blinds 2.step`) | 1:15 reduction to the roller tube |
| Mean Well 24 V DC PSU — **recommend LRS-50-24** (LRS-35-24 acceptable) | System supply |
| Step-down regulator 5 V / 3.2 A (VIN 5.3–50 V) | 24 V → 5 V for the FireBeetle |
| Membrane keypad: 2 arrow keys + function key (in stock) | Local controls + calibration UX |
| External status LED (enclosure face) | State annunciator (§2 Keypad & LED); onboard LED mirrors it |

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
- **Thermal:** at 1.2 A/phase the DRV8825 needs its **heatsink fitted** and the
  enclosure design must provide airflow/venting around the driver.
- **Motor direction is per-unit runtime config**, not wiring/compile-time:
  front-roll vs back-roll and motor orientation differ per window. NVS flag
  `motor_reversed` (default off), applied in `motion_init()` before any position
  math — a correctly configured unit always has "Up" moving toward Open. Set via
  keypad chord **or** z2m (§5); **changing it wipes calibration** (§6).

### Idle behavior (decided)

Driver **disabled at idle** (`EN̅` high after each move): silent, cool, near-zero
idle draw. Relies on the 1:15 gearing not back-driving — verify on the bench. If
the blind creeps, fallback is a build flag enabling idle hold (interface already
supports it since `EN̅` is firmware-controlled).

### Keypad & LED

Membrane keypad common → GND; **Up / Down / Fn** → three GPIOs with internal
pull-ups, firmware debounce (library `debounce` module). One external status LED
on the enclosure face (a GPIO through a resistor); the onboard LED mirrors it.

**Gesture model — Calibrated, idle:**
- **Tap Up / Down** — full open / full close (clamped, position tracked).
- **Tap any key while moving** — stop in place (also stops z2m-initiated moves).
- **Hold Up / Down** — jog while held, clamped at soft limits.

**Gesture model — uncalibrated / Position Unknown / calibration mode:**
- **Hold Up / Down** — move only while held (deadman), unclamped. This is the
  *only* way to move an uncalibrated blind — operable on first contact by
  someone who has never seen the system, in any device state.
- **Taps do nothing** (except Fn-tap = set Mark, inside calibration mode).

**Mode gestures (any state, from standstill only):**
- **Fn long-press (~3 s)** — enter/exit calibration mode (§6). Ignored while
  moving; stop first.
- **Up+Down chord held ~3 s** — toggle Motor Reversed (§5, §6).

**LED patterns:**
| Pattern | Meaning |
|---|---|
| Off | Normal: calibrated, idle |
| 1 Hz blink | Calibration mode, awaiting mark 1 (Open) |
| Fast blink (~5 Hz) | Calibration mode, awaiting mark 2 (Closed) |
| Double-flash every 3 s | Uncalibrated / Position Unknown (z2m motion locked) |
| Three quick flashes | Ack: mark accepted / direction toggled |
| Five rapid flashes | Error: mark rejected (§6 validation) |
| Steady rapid blink | Zigbee Identify (0x0003) active |

GPIO budget: 7 (STEP, DIR, EN, BTN_UP, BTN_DOWN, BTN_FN, LED).

## 3. `esp-zb-common` Library (Deliverable 1)

New repo `~/Developer/499/esp-zb-common`, pushed to GitHub, packaged as an
**ESP-IDF component** consumed via the IDF Component Manager — a git dependency
in each project's `src/idf_component.yml` pinned to a **version tag**, with
`override_path` for local development. (Revised from the originally decided
PlatformIO `lib_deps`: `lib_deps` cannot express ESP-IDF component `REQUIRES`
on esp-zigbee-lib in pure-espidf projects; the component manager is the same
proven mechanism both siblings already use to consume esp-zigbee-lib itself.
All decided properties — own repo, GitHub, tag pinning, local override — are
preserved.) Extracted from DoorSensor's proven code:

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
| `keypad` | Debounce + tap / hold / long-press / Up+Down-chord classification → control events | classifier yes |
| `status_led` | LED pattern player (external + onboard mirror) driven by device state | pattern logic yes |
| `main` | Wiring, config constants | — |

### Data flow

Command (z2m cluster cmd **or** keypad event) → `position` computes target step,
clamps to limits → `motion` ramps to target, counting steps → on arrival or Stop:
motor stops, driver disabled, `blind_store` persists position, `covering` reports
final lift-%. During moves, `covering` reports intermediate position ~every 1 s so
the HA slider tracks live.

**Concurrency rule:** the GPTimer ISR only decrements a step budget and toggles
STEP. All decisions (ramp profile, limits, persistence, reporting) run in task
context via a FreeRTOS queue. **No Zigbee or NVS calls from ISR.** The step ISR
and its data live in **IRAM** (`IRAM_ATTR`) so flash writes (OTA download, NVS)
never stall stepping mid-move.

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
  - **Mode attribute (0x0017), bit 0 "motor direction reversed"** — writable;
    backs the NVS `motor_reversed` flag. Two-way sync: a z2m write is applied on
    the device; a keypad-chord toggle is reported back via the attribute. The
    NVS value is the single source of truth; both UIs are editors of it.
  - **Motion lockout:** while uncalibrated, Position Unknown, or in calibration
    mode, **all motion commands** (UpOpen/DownClose/Stop/GoToLift) are rejected
    with a ZCL default response (failure). Lift reports unknown (0xFF) and the
    converter's `calibrated: false` flag shows why. Remote motion is a privilege
    calibration unlocks — an uncalibrated blind moves only via local
    hold-to-jog (deadman). Rationale: Zigbee has no key-release event, so a
    remote unclamped move has no natural stop.
- **Join:** BDB steering + network-state restore, inherited from `zb_core`.
- **Converter:** `z2m/dfr_roller_blinds.js` (pattern of `dfr_door_sensor.js`) —
  matches manufacturer+model, exposes a native **cover** (position,
  open/close/stop), a `calibrated` diagnostic flag, and a `motor_reversed`
  toggle (Mode bit 0). Friendly-naming in z2m UI.
- **OTA:** shared-library `tools/` build the images; served by the existing
  `dfr-ota-server`; bootloader rollback + self-check via library `ota_client`.
  Downloads coexist with motion (step ISR in IRAM). **Apply/reboot only when
  idle:** if a download finishes mid-move, mark upgrade-pending, complete the
  move, persist position, then reboot — OTA can never cause Position Unknown.

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

Calibration mode is entered by holding **Fn** ~3 s **from standstill** (the
gesture is ignored while moving; stop first). Soft limits are off inside the
mode; motion is hold-to-jog only; z2m motion commands are rejected (§5). The
mode has two automatic entry variants — no extra gestures to learn:

**Full calibration** (entered while calibrated, or never calibrated):
1. Jog to **fully open**, tap Fn → mark 1 (zero reference). LED goes 1 Hz →
   fast (~5 Hz).
2. Jog to **fully closed**, tap Fn → mark 2. **Validation:** mark 2 must lie
   *below* mark 1 by a minimum sane travel (constant, ~¼ output rev). Invalid →
   five-flash error, mark rejected, mode stays waiting for mark 2. Valid →
   span + position saved to NVS **atomically** (previous calibration untouched
   until then), mode exits, device reports calibrated + 100 %.

**Re-home** (entered while Position Unknown — span already known):
1. Jog to **fully open**, tap Fn → re-zeroed against the kept span, mode exits,
   device calibrated again. One mark instead of two.

Abort either variant: second Fn long-press, or 5-minute timeout — no save.
Bare Fn-taps outside calibration mode **never** set marks (idle taps are inert;
taps while moving mean stop) — no accidental re-zeros.

### Power-loss / drift recovery

- Position written to NVS **after each completed move** (not during — NVS wear).
- **A clean power cycle keeps calibration**: span and position persist in NVS
  and the geartrain prevents hand-moving the blind while unpowered, so the
  device boots knowing where it is.
- `move_in_progress` NVS flag set at move start, cleared on clean completion.
  Boot with the flag set (power died during the ~11 s of a move) → **Position
  Unknown**: span kept, z2m motion locked, hold-to-jog only, LED double-flash —
  until a Re-home (above).
- The same Re-home fixes drift if the blind ever back-drives while idle.

### Motor Reversed changes wipe calibration

Toggling `motor_reversed` (keypad chord or z2m Mode write) **invalidates span
and position** → device drops to uncalibrated. All stored step counts were
measured under the old direction sense; keeping them would drive the blind past
its real limits with full confidence. The toggle is an install-time tool;
re-flipping costs a 2-minute recalibration.

### Safety clamps

- All targets clamp to [0, `closed_steps`].
- Motion refuses to start when uncalibrated (except jog).
- Runaway watchdog: any move exceeding `closed_steps` + margin is aborted
  (software backstop for logic bugs — DRV8825 breakouts expose no fault pin).

## 7. Error Handling

| Condition | Behavior |
|---|---|
| Zigbee down / not joined | Keypad retains full control; motion never depends on the stack. Join retries per `zb_core` steering backoff. |
| Invalid command (>100 %), or any motion command while uncalibrated / Position Unknown / in calibration mode | ZCL default response with failure status; no motion (§5 lockout). |
| Command during a move | Clean preemption: decelerate to stop, then execute new target. Keypad and Zigbee equal priority, last-writer-wins. Local taps stop network-initiated moves. |
| Calibration mark 2 above mark 1, or span below minimum | Mark rejected, five-flash error, mode stays in awaiting-mark-2; prior calibration untouched. |
| Motor Reversed toggled while calibrated | Calibration wiped → uncalibrated state, LED double-flash, z2m `calibrated: false`. |
| OTA download completes mid-move | Upgrade-pending; reboot deferred until move completes and position is persisted. |
| NVS write failure | Logged; move completes; position flagged dirty → next boot demands re-home rather than trusting stale data. |
| Driver electrical fault | Not detectable in v1 (no fault pin); runaway watchdog is the backstop. |

## 8. Testing

- **Host (`pio test -e native`, Unity):** `position` (steps↔%, clamping,
  calibration state machine incl. mark-2 validation, re-home vs full-recal entry
  selection, motor-reversed-wipes-calibration, recovery-flag logic), `keypad`
  classifier (tap/hold/long-press/chord), `status_led` pattern selection, ramp
  math as a pure function (distance → accel/cruise/decel step counts). Library
  `debounce` already covered in its repo.
- **Library repo:** Unity tests for pure modules, builder-interface compile
  checks, pytest for OTA tools.
- **On-device (manual checklist in `DEVELOPER_GUIDE.md`):** bench motor run
  before mounting (Vref set, direction, microstep audio check); calibration;
  motor-reversed toggle from both keypad chord and z2m (and the sync back);
  calibration incl. a deliberately wrong mark 2; full open / close / 50 % from
  z2m; z2m lockout while uncalibrated; keypad tap/hold/stop-while-moving;
  power-cut mid-travel → re-home flow; OTA round-trip incl. download during a
  move; router relay check with a downstream device.

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
