# Developer Documentation — DFR-RollerBlinds

## Overview

This is the technical reference for developers who maintain or extend
the DFR-RollerBlinds firmware. The device is an ESP32-C6 roller-blind
controller with a stepper motor. It uses mains power. It joins Zigbee as
a **Router** and shows a standard **Window Covering (0x0102)** device to
zigbee2mqtt. A membrane keypad with three keys gives local control and
calibration. The firmware updates over the air.

This project uses the shared `esp-zb-common` library, pinned to
**v0.2.1**. The library came from the proven Zigbee and OTA code of
`../DFR-DoorSensor`. It has two parts:

- `zb_core`: stack initialization, join, and endpoint and cluster
  registration through a builder callback that the application supplies.
- `ota_client`: the OTA download and the rollback self-check.

For the full module table and the architecture, refer to
[CLAUDE.md](CLAUDE.md). For the vocabulary of this guide (Open/Closed/Lift,
Tap/Hold, Calibration Mode, Mark, Span, Position Unknown, Re-home,
Calibrated, Motor Reversed), refer to [CONTEXT.md](CONTEXT.md).

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
2. Flash the device and apply power
   (`pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor`). The
   device starts BDB steering automatically and joins as a Router.
3. Install the external converter:
   1. Copy `z2m/dfr_roller_blinds.js` into the zigbee2mqtt config
      directory.
   2. Register it under `external_converters:` in `configuration.yaml`.
   3. Restart z2m.

   If you do not install the converter, z2m shows the device as "Not
   supported". z2m identifies a device from its manufacturer and model,
   and z2m has no built-in definition for this DIY model.
4. In the z2m UI, start a **Reconfigure / Re-interview**. This runs the
   `configure()` function of the converter. The device then shows a
   native **cover** entity, a `calibrated` diagnostic flag and a
   `motor_reversed` toggle.
5. A device that you flashed but did not calibrate reports
   `calibrated: false`. It rejects all z2m motion commands. This is
   correct. Calibrate the device locally with the keypad before you use
   remote control (refer to the procedure below).

To change the name of the device, set `friendly_name` in the z2m UI.

## Calibration walkthrough

Calibration is **local only**, and you do it with the keypad. To enter
calibration mode, hold **Fn for approximately 3 s** while the blind is
stopped. The device ignores this gesture while the blind moves. Stop the
blind first (a tap on any key stops the blind).

**Full calibration** (the device is calibrated, or was never calibrated):

1. **Hold Fn for approximately 3 s.** The device enters calibration mode
   and waits for mark 1 (Open). The LED shows a **slow pulse: 500 ms on,
   1 s off**.
2. **Jog** the blind (hold Up or Down) until it is fully **open**.
3. **Tap Fn.** The device records mark 1 (the zero reference). The LED
   gives three flashes, then changes to a **blink at approximately 5 Hz**
   (it waits for mark 2).
4. **Jog** the blind until it is fully **closed**.
5. **Tap Fn.** The device records mark 2 and validates it.
   - If mark 2 is below mark 1 by the minimum travel or more, the device
     saves the calibration to NVS in one atomic operation. Then it exits
     the mode. The device now reports `calibrated: true` and 100% lift.
     The LED gives three flashes and then goes **off** (normal).
   - If mark 2 is not valid (above mark 1, or too near to it), the device
     **rejects** the mark. The LED gives **five rapid flashes**, and the
     mode continues to wait for mark 2. Jog the blind and tap Fn again.

**Re-home** (the device is in **Position Unknown**). The span is known,
but the device cannot trust its current position. For example, this
occurs after a power failure during a move.

1. **Hold Fn for approximately 3 s.** The device enters calibration
   mode. The span is known, so the device automatically selects the
   Re-home procedure with one mark. The LED shows a **slow pulse: 500 ms
   on, 1 s off**.
2. **Jog** the blind until it is fully **open**.
3. **Tap Fn.** The device sets the zero of the position again, and keeps
   the span. It exits the mode and is calibrated again. The LED gives
   three flashes and then goes **off**.

**Abort**: the device aborts without a save after one of these events:

- a second **long press on Fn** (approximately 3 s) in the mode
- a **10-minute timeout** from when you entered the mode

The timeout occurs also if you already recorded a mark (for example, in
a full calibration while the device waits for mark 2). The span does not
change. But if you **jogged** the blind during the aborted session, the
stored position is not correct. Thus the device goes to **Position
Unknown** (the LED shows a double flash). It does not trust the old
state. An abort without a jog changes nothing.

### LED patterns per state

| Pattern | Meaning |
|---|---|
| Off | Normal: calibrated, idle |
| 500 ms on / 1 s off | Calibration mode, waits for mark 1 (Open) |
| Fast blink (~5 Hz) | Calibration mode, waits for mark 2 (Closed) |
| Double flash every 3 s | Not calibrated, or Position Unknown (z2m motion is locked) |
| Three quick flashes | Acknowledgement: the device accepted a mark, changed the direction, is already at that limit, got an Fn tap with nothing to mark, or did a jog that was too small to change the reported position |
| Five rapid flashes | Error: the device rejected a mark, or is not calibrated and will not move (calibrate it), or the hard-cap watchdog refused the move |
| Steady rapid blink | Zigbee Identify (0x0003) is active |
| 2 s on, 1 s off | Not joined to a Zigbee network (steering) |

## Re-home after power loss

The device writes the position to NVS only **after each completed
move**. A write during a move would cause wear of the NVS. The device
sets a `move_in_progress` flag when a move starts and clears it when the
move completes correctly. A full travel takes approximately 11 s. If the
power fails during a move, the flag is still set at the next boot. The
device then immediately marks the position as not trusted:

- The span **stays**. The device writes the span only during a full
  calibration.
- The device reports **Position Unknown**. z2m motion is locked out, the
  LED gives double flashes, and only local hold-to-jog can move the
  blind.
- Correction: **hold Fn for 3 s → jog to Open → tap Fn** (the Re-home
  procedure with one mark, above). This is shorter than a full
  calibration because the span stays.

After a clean power cycle (no move in progress), the full calibration
stays. When the power is off, the gear train prevents movement of the
blind by hand. Thus the device trusts its last known position at boot,
and taps operate immediately.

## Tuning travel time from z2m

The full-travel time is a setting for each unit. It is not a
compile-time constant. Set `travel_time` (in seconds) on the device in
z2m. NVS stores it, and it stays after OTA updates.

The device limits the requested value to the speed that its motor can
drive. The lower limit is `RAMP_MIN_CRUISE_US` (60 µs for each step).
The device **writes back the value that it applied**. If the field shows
a larger value than you requested, the device applied the limit. The
write did not fail.

To find the limit of a blind, decrease the value in steps until the
motor stalls. Then increase the value by a safe margin. The torque
margin changes with temperature, supply voltage drop and the quantity of
fabric on the roll. A value that operates on a warm afternoon with an
almost empty roll can stall on a cold morning with a full roll.

> **CAUTION:** Change the value in small steps. A stall makes the
> position incorrect, and the device does not know this (the step
> counting is open-loop). To recover, you must do a Re-home at the
> keypad, which needs physical access to the blind.

A device that never had a value set reports the duration that its
compile-time default gives. Thus a unit that you updated shows its
current speed, not `0`.

### Set about 3 seconds below the time you actually want

These values were measured on `lounge-blind-3` (17HS4401, span ≈
345 000 steps) at four settings. The times come from the 1 Hz position
reports:

| Set | Measured | Over |
|---|---|---|
| 30 | ~33 s | +3.3 |
| 45 | ~49 s | +4.0 |
| 53 | ~55 s | +2.0 |
| 60 | ~63 s | +2.6 |

The added time is **constant in seconds, not a percentage**. It is
approximately 3 s at all settings. Thus it is ~11% at 30 s but only ~4%
at 60 s. This shows a fixed cost for each step of approximately
**9.6 µs**:

- `STEP_PULSE_US` (3 µs, a blocking delay in the ISR) is one third of
  this cost.
- The ISR entry and exit, and the new setting of the GPTimer alarm, are
  the remainder.

The device pays this cost approximately 345 000 times for each travel.

`ramp_us_from_travel_time()` intentionally does not correct for this
cost. The value is specific to the board and the build. A fixed constant
in the conversion would be a guess that looks like precision. Instead,
subtract approximately 3 s manually.

### Noise is not monotonic with speed

These values were also measured on that unit, by ear:

| Set | ≈ motor RPM | Motor hum | Gearbox noise |
|---|---|---|---|
| 30 | ~415 | worst | highest |
| **45** | **~275** | **lowest** | middle |
| 53 | ~235 | ≈ 45 | lower |
| 60 | ~205 | worse than 45 | lowest |

Two independent sources have opposite effects. The mechanical noise
increases with speed. The motor hum has a **minimum at approximately
250–275 RPM**. Below that speed, the motor goes into a mid-band
resonance. Above that speed, StealthChop2 (a voltage-mode chopper)
gradually loses control of the current as the back-EMF increases.

**Thus a lower speed can make a humming blind louder.** 60 s is slower
than 53 s, and it is louder. If a unit hums, first try a higher speed
near ~275 RPM. Do not assume that it must go slower.

None of the four settings showed strain. Thus on this blind, the speed
is a question of comfort, not of torque.

## Recovering a device that has lost the network

When you remove a device from zigbee2mqtt, the device now clears its
Zigbee state and starts steering again automatically. It tries again
without a limit, with a backoff from 1 s to 60 s. It joins again
automatically the next time that permit-join is open. You do not need a
power cycle or USB. While it searches, the LED shows **2 s on, 1 s off**.

If the device does not come back, the probable cause is that it did not
receive the leave frame. Hold **all three keys for 5 seconds**. This
erases the Zigbee state of the device and restarts it. The device then
starts as factory-new and starts steering.

**The calibration, the position and `travel_time` stay**. They are in
the `nvs` partition, and the reset erases only `zb_storage`. When the
device joins again, it already knows the position of its blind.

While you hold all three keys, the device suppresses the calibration
long press and hold-to-jog. Thus they cannot start before the 5-second
mark. This suppression uses only the current key state. When you release
a key, the other keys operate again immediately. Intentionally, it is
not a latch. The latches that this gesture used before could stay after
the gesture. They then locked the keypad until a power cycle.

**Known limitation:** a device that does not receive its leave frame
*and* is not accessible at the keypad stays stuck. To find when the
coordinator ignores the device is not in the scope of this design. It
needs a separate design.

## Bench verification checklist

This is the checklist on hardware. It is also the source of Task 13 in
the implementation plan. It needs the **rev 2 physical rig**:

- Seeed XIAO ESP32C6
- BIGTREETECH TMC2209 V1.3 (`VCC_IO` wired to 3V3, `MS1`/`MS2` at GND,
  Vref set to ~1.69 V, heatsink installed)
- motor, 24 V PSU, keypad and LED
- a z2m instance with permit-join

For the full wiring reference, refer to [HARDWARE.md](HARDWARE.md).

`src/main.c` and `platformio.ini` use the XIAO/TMC2209 map. Thus you can
do this checklist. When this checklist was written, only the pin mapping
had been tested on rev 2 hardware.

- [x] XIAO D-number → GPIO mapping verified **2026-08-02**. All seven
  pins were confirmed on a bare board with `tools/pinwalk`. This tool
  sets one GPIO high at a time, and a probe measures it against GND.
  STEP D8=GPIO19, DIR D7=GPIO17, EN D9=GPIO20, Up D4=GPIO22,
  Down D5=GPIO23, Fn D6=GPIO16, LED D2=GPIO2. The pinout table that Seeed
  publishes was correct. `src/main.c` did not need a change. The
  silkscreen shows only the D-numbers. Thus you cannot do a visual check.
  On each new board, run the walker again. Do not trust a visual
  inspection.
  - **The LED moved to D3/GPIO21 on 2026-08-08, and that pin is also
    confirmed.** D3 was *not* in the original walk. The walk set only
    the seven pins that were in use, and D3 was a spare. Thus at first,
    only the Seeed table supported it. The LED came on with the new
    firmware, which verified the pin on hardware. All eight D-number →
    GPIO pairs that the project used are now confirmed by test.
  - **The pin map changed in v2.4.0:** Fn D3/GPIO21, LED D2/GPIO2. The
    two pairs were already confirmed on hardware (GPIO21 as the LED,
    GPIO2 in the original walk). The new map ran on `bench3` on
    2026-09-26.
- [~] `VCC_IO` sanity check: **intentionally not done** on the built
  rev 2 board. The test lifts `VCC_IO` to confirm that the driver stops
  operating. On a soldered board, this needs desoldering to prove a
  failure mode that we already understand. The test was useful on the
  breadboard of rev 1. A correct Vref reading (below) already shows that
  the digital core has power.
- [x] Motor bench run before installation **2026-08-02**. Vref was set to
  1.69 V with the motor disconnected. Then the motor turned on Up holds
  and on Down holds, with the motor not connected to the gear train. It
  was **quiet and smooth** at `CRUISE_US = 100`.
  - Quiet operation confirms that StealthChop2 is active (the `SPRE`
    pad has its factory bridge).
  - Smooth operation confirms the A/B coil pairs.
  - The LED double flash (`LED_UNCAL`) confirms the D2 harness.
  - The keypad holds confirm the full path: keypad → dispatcher → motion.
  - The direction is not yet important. It is checked after the motor
    is coupled, and corrected with `motor_reversed` if necessary. Never
    rewire the coils to correct it.
  - **Not yet proven: torque under the real blind load.** Quiet
    operation without load does not show if 100 µs is sufficient with
    the 1:15 reduction and a 2.5 m blind.
- [x] Join as router **2026-08-02**. The device joined and published as
  `lounge-blind-3`, with linkquality ~138. The converter showed it as a
  cover with `calibrated: false`. The rev 1 converter did not need a
  change, because the hardware change did not change the Zigbee identity.
- [x] `motor_reversed` toggle: the z2m side operated correctly on
  **2026-08-02**. The keypad Up+Down chord that also set it is
  **retired**. Now you can set it only from z2m.
- [x] z2m motion commands rejected while not calibrated **2026-08-02**.
  Open and close were pushed in z2m, and the motor did not move. The
  device applies the lockout in `zb_goto_request()`. Thus the lockout
  operates, independent of what the converter reports.
- [x] Calibrate with the keypad **2026-08-02**. The calibration on the
  bench rig (17HS4401 + small blind) was successful. The five-flash
  rejection of an intentionally incorrect mark 2 is **not yet tested
  again** on rev 2.
- [x] Full travel from keypad taps **2026-08-02**. Correct in the two
  directions at `CRUISE_US = 150` / Vref 1.92 V. First, the Vref had to
  be corrected for the installed motor. Refer to
  [HARDWARE.md](HARDWARE.md#motion-speed-tuning). The live position
  during z2m moves is not yet checked separately.
- [ ] Keypad matrix (Fn D3, Up D4, Down D5 since v2.4.0): a tap on Up or
  Down gives full travel; a tap during a move stops the blind; a hold
  jogs, and the jog stops at the limits
- [ ] Power failure during travel → boots in Position Unknown (double
  flash, z2m locked) → Re-home (Fn 3 s, jog to Open, Fn)
- [~] Clean power cycle at rest → still calibrated **2026-08-02**. (This
  was seen during the OTA persistence check: the calibration stayed after
  a power cycle at rest.) "Taps operate immediately after" is not
  confirmed separately.
- [ ] Idle back-drive check: leave the blind at mid-travel for one
  night. If it moves slowly, examine the idle hold again (spec §2
  fallback).
- [x] OTA round trip **2026-08-02**. z2m offered v2.0.0. The device
  downloaded and installed it, and restarted into it. After one more
  clean power cycle, the device was **still on v2.0.0, with the
  calibration intact**. That power cycle is the important part.
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` reverts an image that fails
  its self-check, without a message. Thus the result proves that the
  device accepted the image, not only that it wrote the image. The full
  path is proven on rev 2: tag → CI build → `.ota` asset →
  `ota/index.json` on main → z2m offer → install → persist.
- [ ] Router relay check with a downstream device
- [ ] OTA download during a move (IRAM validation and recovery path)
- [ ] Calibration abort paths: abort without a jog (nothing changes),
  abort after a jog (goes to Position Unknown), 10-minute timeout, also
  during a jog
- [ ] ZB preemption during a move; ZB Stop during a move; a hold jog
  that ZB preempts and that you then release
- [ ] z2m lockout in calibration mode (motion commands rejected while
  `s_cal_mode`)
- [ ] Taps do nothing when not calibrated (hold-to-jog still operates)
- [ ] Keypad operation while Zigbee is down, then join again
- [ ] Identify → LED (steady rapid blink while Identify is active, D2
  since v2.4.0)
- [ ] 10 or more full-travel cycles in sequence. Check the two physical
  marks each time (open-loop drift).
- [ ] TMC2209 thermal soak in the enclosure. The built-in thermal
  shutdown is a backstop. It does not replace sufficient airflow. Make
  sure that the temperatures stay correct during long cycling.
- [ ] Power cycle during a calibration session with a jog
- [ ] A z2m Mode write during a move is rejected and stays in sync

**v2.4.0 (review fixes) bench checks**
- [ ] Calibrated unit with power on and the coordinator off: the LED
  shows NO_NETWORK from power-on
- [ ] Tap exactly when a full travel ends: no second travel from the old
  position
- [ ] Hold a jog, then release: the blind stops. Hold during heavy z2m
  traffic, then release: the blind stops
- [ ] Motor Reversed toggled from z2m while idle: ACK, calibration
  erased, z2m shows the new value
- [ ] Motor Reversed toggled from z2m during a move: refused, z2m shows
  the value that did not change
- [ ] Fn held 3 s during a travel: ERROR flash, no Calibration Mode
- [ ] Three-key reset during a travel: ERROR flash, no reset
- [ ] `riscv32-esp-elf-nm` shows the ramp functions and timer_cb at
  0x408… (IRAM)

## Known issues

### `position: 0` published while uncalibrated

While the device is not calibrated, it reports lift `0xFF`
(`POSITION_LIFT_UNKNOWN`, `covering.c:31`). This value means "position
unknown". The custom `fzCalibrated` handles the value correctly and
reports `calibrated: false`.

The **stock `m.windowCovering` extend** in `z2m/dfr_roller_blinds.js`
does not handle it correctly. It does not know the `0xFF` sentinel. It
uses 255 as a real percentage, and calculates `position: 0` from it.
Thus z2m publishes `calibrated: false` and `position: 0` in the same
payload. Home Assistant then shows the cover as fully closed, but the
device says that it does not know its position.

**This is cosmetic. It is not a safety issue.** The device applies the
lockout in `zb_goto_request()`. Thus an incorrect position cannot cause
motion. The issue was already in rev 1. The rev 2 hardware change did
not cause it. Nobody saw it before.

To correct it, replace the position handling of the stock extend with
a guarded version. That version must not publish `position` when the
lift is `> 100`. Then deploy the converter again. This work is postponed. The
issue was seen on 2026-08-02.

## OTA release flow

zigbee2mqtt sends the firmware updates over the air.
`.github/workflows/release-ota.yml` builds and publishes an image when a
`v*` tag is pushed. The identity comes from `include/ota_ids.h`
(`OTA_MANUFACTURER_CODE = 0xFEFE`, `OTA_IMAGE_TYPE = 0x0003`,
`OTA_MODEL_ID = "DFR-RollerBlinds"`).

### Cutting a release

```bash
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

The workflow then does these steps:

1. It checks out this repo at **the pushed tag**
   (`ref: ${{ github.ref }}`). Thus it builds the commit that you
   tagged, which is not always `main`. It also checks out
   `linosteenkamp/esp-zb-common` at `v0.2.1` (for its `tools/`).
   `esp-zb-common` is **public**. Thus this checkout does not need a
   token. The component-manager clone that `pio run` does during the
   build also does not need a token.

   (Before, the workflow used a fine-grained PAT, `ZB_COMMON_PAT`, while
   that repo was private. The PAT failed with 403 errors many times
   during the first real release test on 2026-07-20. Thus the repo was
   made public, and the authentication steps were removed. Nobody
   debugged the PAT expiry or scope. If `esp-zb-common` must become
   private again, examine this again.)
2. It gets `major`/`minor`/`patch` from the tag and packs
   `file_version = 0x%X%X%02X0000`. This value must agree with
   `OTA_PACK_VERSION` in `include/ota_ids.h`.
3. It builds `seeed_xiao_esp32c6_zigbee` with those version flags.
4. It wraps `firmware.bin` into `DFR-RollerBlinds-<tag>.ota` with
   `tools/make_ota_image.py` from `esp-zb-common`.
5. It publishes a GitHub Release with that asset attached.
6. It updates `ota/index.json` (`tools/update_ota_index.py`) and commits
   it to `main`.

### z2m configuration

Add the `ota:` block to the `configuration.yaml` of zigbee2mqtt, or
extend the block:

```yaml
ota:
  # Raw GitHub URL of ota/index.json on the default branch (main):
  zigbee_ota_override_index_location: "https://raw.githubusercontent.com/linosteenkamp/DFR-RollerBlinds/main/ota/index.json"
```

The converter already declares OTA support. Thus z2m polls that index
and offers updates through the normal z2m OTA UI (Device → OTA →
Update).

### Rollback behavior

The bootloader runs with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. A
new image stays pending-verify until the firmware calls
`ota_client_mark_valid()`. `app_main()` calls it when the boot gets to
that point ("booted cleanly = good"). The Zigbee join does not affect
this. If the new image crashes before `app_main()`, the bootloader goes
back to the previous slot at the next boot.

### OTA during motion

A download can occur during motion. The step ISR runs from IRAM. Thus
flash writes during an OTA download never stop the steps. The library
restarts into the new image immediately when the download is complete.
It does not wait for idle. If that restart occurs during a move, the
`move_in_progress` NVS flag catches it at the next boot, and the device
goes to Position Unknown. This is not a hazard. To recover, use the same
Re-home procedure with one mark (above).

## See also

- [HARDWARE.md](HARDWARE.md): the BOM, the wiring and the Vref
  procedure.
- [PARTITIONS.md](PARTITIONS.md): the flash partition layout.
- [CLAUDE.md](CLAUDE.md): the architecture, the module table and the
  concurrency rule.
- [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md):
  the full design specification.

## Bench log

**2026-07-20**: the first bench session is complete. Results:

- The keypad was verified (▲→5 ▼→6 Fn→7).
- The DRV8825 drive chain was repaired (SLP̅+RST̅ → 3V3, Vref 0.6 V).
- The device joined z2m and was calibrated.
- Remote position control was verified (moved to 50 % through MQTT).
- The two-way sync of motor_reversed operated.

Remaining: the items in "Bench verification" above that are not yet
ticked:

- taps and full travel
- the incorrect-mark error
- the Re-home after a power failure
- persistence after a clean power cycle
- the back-drive check
- the OTA round trip
- the drift cycles
- the thermal soak
- the Identify LED
