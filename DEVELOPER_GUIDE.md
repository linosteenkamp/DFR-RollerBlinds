# Developer Documentation — DFR-RollerBlinds

## Overview

Technical reference for developers maintaining or extending the
DFR-RollerBlinds firmware. The device is a mains-powered ESP32-C6 stepper
roller-blind controller that joins Zigbee as a **Router** and presents a
standard **Window Covering (0x0102)** device to zigbee2mqtt, with a 3-key
membrane keypad for local control and calibration, and over-the-air firmware
updates.

This project consumes the shared `esp-zb-common` library (pinned to
**v0.1.1**), extracted from `../DFR-DoorSensor`'s proven Zigbee/OTA code:
`zb_core` (stack init, join, endpoint/cluster registration via an
app-supplied builder callback) and `ota_client` (OTA download + rollback
self-check). See [CLAUDE.md](CLAUDE.md) for the full module table and
architecture overview, and [CONTEXT.md](CONTEXT.md) for the vocabulary used
throughout this guide (Open/Closed/Lift, Tap/Hold, Calibration Mode, Mark,
Span, Position Unknown, Re-home, Calibrated, Motor Reversed).

## Useful commands

```bash
pio run -e seeed_xiao_esp32c6_zigbee                 # build
pio run -e seeed_xiao_esp32c6_zigbee -t upload       # flash
pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor
pio run -t erase                                              # erase flash (re-pair)
pio run -e seeed_xiao_esp32c6_zigbee -t size         # image size vs slot
pio run -t clean
pio test -e native                                            # host tests
```

## Pairing

1. Enable **permit join** in zigbee2mqtt.
2. Flash and power the device (`pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor`);
   it auto-runs BDB steering and joins as a Router.
3. Install the external converter: copy `z2m/dfr_roller_blinds.js` into the
   zigbee2mqtt config directory and register it under `external_converters:`
   in `configuration.yaml`, then restart z2m. Without it, z2m shows the
   device as "Not supported" — it matches on manufacturer + model, and this
   DIY model has no built-in definition.
4. Trigger a **Reconfigure / Re-interview** in the z2m UI so the converter's
   `configure()` runs and the device appears with a native **cover** entity,
   a `calibrated` diagnostic flag, and a `motor_reversed` toggle.
5. A freshly-flashed (never-calibrated) device reports `calibrated: false`
   and rejects all z2m motion commands — this is expected. Calibrate locally
   with the keypad before expecting remote control (below).

Rename the device via `friendly_name` in the z2m UI.

## Calibration walkthrough

Calibration is **local-only, keypad-driven**, entered from standstill by
holding **Fn for ~3 s**. The gesture is ignored while the blind is moving —
stop it first (any tap stops motion).

**Full calibration** (device is calibrated, or has never been calibrated):

1. **Hold Fn ~3 s** → enters calibration mode, awaiting mark 1 (Open).
   LED: **1 Hz blink**.
2. **Jog** (hold Up/Down) the blind to fully **open**.
3. **Tap Fn** → records mark 1 (the zero reference). LED switches to
   **~5 Hz blink** (awaiting mark 2), plus a three-flash ack.
4. **Jog** the blind to fully **closed**.
5. **Tap Fn** → records mark 2 and validates it. If mark 2 is at least the
   minimum travel below mark 1, calibration is saved to NVS atomically and
   the mode exits — device now reports `calibrated: true` and 100% lift.
   LED returns to **off** (normal), with a three-flash ack.
   If mark 2 is invalid (above mark 1, or too close), the mark is
   **rejected**: **five rapid flashes**, mode stays awaiting mark 2 — jog and
   tap Fn again.

**Re-home** (device is in **Position Unknown** — span already known, only
trust in current position was lost, e.g. after a power cut mid-move):

1. **Hold Fn ~3 s** → enters calibration mode; because the span is already
   known, this automatically selects the one-mark Re-home variant. LED:
   **1 Hz blink**.
2. **Jog** to fully **open**.
3. **Tap Fn** → re-zeroes position against the kept span, mode exits, device
   is calibrated again. LED returns to **off**, three-flash ack.

**Abort**: a second **Fn long-press** (~3 s) inside the mode, or a **5-minute
timeout** since entering the mode, aborts with no save — the timeout fires
regardless of whether a mark has already been recorded (e.g. mid-way through
a full calibration, awaiting mark 2). The span is left untouched — but if
the blind was **jogged** at all during the aborted session, the stored
position no longer matches reality, so the device drops to **Position
Unknown** instead of trusting stale state (LED: double-flash). An abort with
no jogging changes nothing.

### LED patterns per state

| Pattern | Meaning |
|---|---|
| Off | Normal: calibrated, idle |
| 1 Hz blink | Calibration mode, awaiting mark 1 (Open) |
| Fast blink (~5 Hz) | Calibration mode, awaiting mark 2 (Closed) |
| Double-flash every 3 s | Uncalibrated / Position Unknown (z2m motion locked) |
| Three quick flashes | Ack: mark accepted / direction toggled |
| Five rapid flashes | Error: mark rejected |
| Steady rapid blink | Zigbee Identify (0x0003) active |

## Re-home after power loss

Position is written to NVS only **after each completed move** (writing
during a move would wear NVS). A `move_in_progress` flag is set at move
start and cleared on clean completion. If power dies mid-move (the ~11 s a
full-travel move takes), the device boots with that flag still set and
immediately marks position untrusted:

- Span is **kept** (it's only written on a full calibration).
- Device reports **Position Unknown**: z2m motion is locked out, LED
  double-flashes, and only local hold-to-jog can move the blind.
- Fix: **Fn 3 s → jog Open → Fn** (the one-mark Re-home flow above). Lighter
  than a full recalibration because the span survives.

A clean power cycle (no move in progress) keeps full calibration — the
geartrain prevents the blind moving by hand while unpowered, so the device
boots trusting its last known position and taps work immediately.

## Bench verification checklist

On-hardware checklist (also the source of Task 13 in the implementation
plan). Requires the **rev 2 physical rig**: Seeed XIAO ESP32C6 + BIGTREETECH
TMC2209 V1.3 (`VCC_IO` wired to 3V3, `MS1`/`MS2` at GND, Vref set to
~1.69V, heatsink fitted) + motor + 24 V PSU + keypad + LED, and a z2m
instance with permit-join. See [HARDWARE.md](HARDWARE.md) for the full
wiring reference.

`src/main.c` and `platformio.ini` target the XIAO/TMC2209 map, so this
checklist is runnable. Nothing below has been exercised on rev 2 hardware
except the pin mapping.

- [x] XIAO D-number → GPIO mapping verified **2026-08-02** — all seven confirmed on a bare board with `tools/pinwalk` (drives one GPIO high at a time; probed against GND). STEP D8=GPIO19, DIR D7=GPIO17, EN D9=GPIO20, Up D4=GPIO22, Down D5=GPIO23, Fn D6=GPIO16, LED D2=GPIO2. Seeed's published pinout table was correct; `src/main.c` needs no change. Note the silkscreen prints only D-numbers, so this can't be checked visually — re-run the walker on any future board rather than eyeballing it.
- [ ] `VCC_IO` sanity check: temporarily lift it and confirm the driver goes fully inert (no response to `STEP`/`DIR`/`EN`) — this is the TMC2209's equivalent of the old DRV8825 `SLEEP̅`/`RESET̅` trap, worth confirming once rather than discovering it live
- [ ] Motor bench-run before mounting: direction, 1/8-µstep smoothness, Vref current check (~1.69V, not the old 0.6V), and confirm it's actually quiet (StealthChop2 is this module's factory default — if it isn't quiet, check the internal `SPRE` solder pad hasn't been re-bridged to SpreadCycle)
- [ ] Join as router; z2m shows the device via the converter (cover + `calibrated: false`)
- [ ] `motor_reversed` toggle from z2m and from keypad chord; both sides stay in sync; verify it wipes calibration
- [ ] z2m motion commands rejected while uncalibrated (buttons in z2m produce an error/no motion)
- [ ] Calibrate via keypad; deliberately try a wrong mark 2 (above mark 1) → five-flash error, mode stays
- [ ] Full open / close / 50% from z2m; live position tracks ~1 s during moves
- [ ] Keypad matrix (D4/D5/D6): tap up/down full travel; tap-while-moving stops; hold jogs clamped at limits
- [ ] Power-cut mid-travel → boots Position Unknown (double-flash, z2m locked) → re-home (Fn 3 s, jog Open, Fn)
- [ ] Clean power cycle at rest → still calibrated, taps work immediately
- [ ] Idle back-drive watch: leave the blind mid-travel overnight; if it creeps, revisit idle-hold (spec §2 fallback)
- [ ] OTA round-trip: tag a release, z2m offers + installs it, device reboots into new version (confirm no rollback after a further power cycle), position survives
- [ ] Router relay check with a downstream device
- [ ] OTA download during an active move (IRAM validation + recovery path)
- [ ] Calibration abort paths: no-jog abort (nothing changes), jogged abort (drops to Position Unknown), 10-minute timeout including mid-jog
- [ ] ZB preemption mid-move; ZB Stop mid-move; hold-jog preempted by ZB then released
- [ ] z2m lockout inside calibration mode (motion commands rejected while `s_cal_mode`)
- [ ] Uncalibrated taps inert (hold-to-jog still works)
- [ ] Zigbee-down keypad autonomy, then rejoin
- [ ] Identify → LED (steady rapid blink while Identify is active, D2)
- [ ] ≥10 consecutive full-travel cycles, checking both physical marks each time (open-loop drift)
- [ ] TMC2209 thermal soak in enclosure (built-in thermal shutdown is a backstop, not a substitute for adequate airflow — confirm temps stay reasonable under sustained cycling)
- [ ] Power-cycle during a jogged calibration session
- [ ] z2m Mode write while moving is rejected and stays in sync

## OTA release flow

Firmware updates are delivered over the air through zigbee2mqtt.
`.github/workflows/release-ota.yml` builds and publishes on a pushed `v*`
tag; identity comes from `include/ota_ids.h`
(`OTA_MANUFACTURER_CODE = 0xFEFE`, `OTA_IMAGE_TYPE = 0x0003`,
`OTA_MODEL_ID = "DFR-RollerBlinds"`).

### Cutting a release

```bash
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

The workflow then:

1. Checks out this repo (`ref: main`) and `linosteenkamp/esp-zb-common` at
   `v0.1.1` (for its `tools/`). `esp-zb-common` is **public**, so no token
   is needed for this checkout or for the component-manager clone `pio run`
   does during the build. (Earlier revisions used a `ZB_COMMON_PAT`
   fine-grained PAT while that repo was private; it repeatedly failed with
   403s during the first real release test — 2026-07-20 — so the repo was
   made public and the auth steps were removed rather than debugging PAT
   expiry/scope. Revisit if `esp-zb-common` ever needs to go private again.)
2. Derives `major`/`minor`/`patch` from the tag and packs
   `file_version = 0x%X%X%02X0000` (must match `OTA_PACK_VERSION` in
   `include/ota_ids.h`).
3. Builds `seeed_xiao_esp32c6_zigbee` with those version flags.
4. Wraps `firmware.bin` into `DFR-RollerBlinds-<tag>.ota`
   (`esp-zb-common`'s `tools/make_ota_image.py`).
5. Publishes a GitHub Release with that asset attached.
6. Updates `ota/index.json` (`tools/update_ota_index.py`) and commits it to
   `main`.

### z2m configuration

Add (or extend) the `ota:` block in zigbee2mqtt's `configuration.yaml`:

```yaml
ota:
  # Raw GitHub URL of ota/index.json on the default branch (main):
  zigbee_ota_override_index_location: "https://raw.githubusercontent.com/linosteenkamp/DFR-RollerBlinds/main/ota/index.json"
```

The converter already declares OTA support, so z2m polls that index and
offers updates through the normal z2m OTA UI (Device → OTA → Update).

### Rollback behavior

The bootloader runs with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. A freshly
flashed image stays pending-verify until `ota_client_mark_valid()` is called,
which `app_main()` does once boot reaches that point — "booted cleanly =
good", independent of Zigbee join. If the new image crashes before
`app_main()`, the bootloader reverts to the previous slot on the next boot.

### OTA during motion

Downloads coexist with motion — the step ISR runs from IRAM, so flash writes
during an OTA download never stall stepping. The library reboots into the
new image as soon as the download completes, with no idle wait — if that
reboot lands mid-move, the `move_in_progress` NVS flag catches it on the
next boot and the device drops to Position Unknown — recoverable with the
same one-mark Re-home flow above, not a hazard.

## See also

- [HARDWARE.md](HARDWARE.md) — BOM, wiring, Vref procedure.
- [PARTITIONS.md](PARTITIONS.md) — flash partition layout.
- [CLAUDE.md](CLAUDE.md) — architecture, module table, concurrency rule.
- [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md) — full design spec.

## Bench log

**2026-07-20** — first bench session complete: keypad verified (▲→5 ▼→6 Fn→7),
DRV8825 drive chain fixed (SLP̅+RST̅ → 3V3, Vref 0.6 V), joined z2m, calibrated,
remote position control verified (moved to 50 % via MQTT), motor_reversed
two-way sync working. Remaining: checklist items from "Bench verification"
above not yet ticked — taps/full-travel, wrong-mark error, power-cut re-home,
clean-cycle persistence, back-drive watch, OTA round-trip, drift cycles,
thermal soak, Identify LED.
