# Gearbox test rig (top speed and wear)

This is a copy of the spec issue [#27](https://github.com/linosteenkamp/DFR-RollerBlinds/issues/27), made on 2026-11-21. The issue is the master copy.

This spec comes from the wayfinder map "Map: gearbox test rig (top speed and wear)" (#14). Each decision below has its detail in a closed ticket of that map. The ticket names are in the Further Notes.

## Problem Statement

The roller-blind controller drives a 3D-printed 1:15 gearbox. The owner has two motors in the fleet: the 17HS4401 for the smaller blinds and the 42HS60-1504JA05-D20-03 for the larger blinds. Nobody has measured these values:

- The top speed at the driven gear before a step is lost, for each motor and load.
- The life of the printed PETG gearbox under the load of a real blind.
- Whether the gearbox can turn backwards under a hanging load when the driver is not powered.

The installed firmware counts steps with no feedback. Thus a lost step, a gear tooth that jumps, or a slip at the spline is not visible. `HARDWARE.md` says that the 17HS4401 sets the speed limit of the fleet. A model in the research says that the 42HS60 can set it. Only a measurement can decide.

The owner has a bare test board with only a Seeed XIAO ESP32C6 and a BIGTREETECH TMC2209 V1.3. The owner wants a test rig that measures top speed and wear, and runs unattended for days.

## Solution

A test rig with its own firmware environment in this repo. The rig hangs a known weight (a 5 L water container) on a cord from a 32 mm spool tube. The tube fits on the real shaft attachment of a complete gearbox unit, as a real roller tube does.

- An AS5600 magnetic encoder reads the angle of the spool tube. The firmware compares the angle with the step count during each move, and stops at the first lost step.
- The TMC2209 is connected through UART, for the hold current, the diagnostics and a SpreadCycle option.
- A KW11-2 limit switch under the board stops the rig at the top. A DS18B20 measures the motor case temperature.
- The device runs the test profiles by itself: the speed sweep, the wear test, the baseline step and the backlash measurement. The owner controls it through MQTT commands. The device publishes JSON records to the MQTT broker on the home-automation Raspberry Pi, where a logger writes them to a file.
- A summary on the device survives outages of the network and resets of the device.

The rig tests two complete gearbox units, one for each motor. The owner changes the unit with 4 M4 bolts.

## User Stories

### Setup and provisioning

1. As the owner, I want to flash the rig firmware from a separate PlatformIO env, so that the blind firmware does not change.
2. As the owner, I want to provision the Wi-Fi and MQTT credentials from 1Password with one script, so that no secret is in the repo or in the firmware image.
3. As the owner, I want to set the credentials with serial commands by hand, so that I can provision the rig when 1Password is not available.
4. As the owner, I want the `status` command to never show a password, so that a log or a screen capture does not leak a credential.
5. As the owner, I want the rig to drive the RF switch for the external antenna, so that the rig has a good Wi-Fi signal.
6. As the owner, I want the rig to get the time from SNTP, so that the records on the Pi and the summary on the device use the same time.

### Driver and safety

7. As the owner, I want the firmware to write all the TMC2209 registers at each boot and read them back, so that a wrong register or a UART fault stops the start of a test.
8. As the owner, I want the driver to keep a hold current of about 50 % between moves, so that the weight cannot turn the gearbox backwards.
9. As the owner, I want the driver to stay enabled at the hold current after a fault stop, so that the weight does not fall.
10. As the owner, I want a `release` command to disable the driver, so that I can remove the load when the container is on the floor.
11. As the owner, I want the driver to disable itself at a driver fault (overtemperature, short circuit, open load), so that the driver is protected.
12. As the owner, I want the rig to stop at once when the top limit switch opens or its wire breaks, so that the hook never winds into the spool.
13. As the owner, I want the rig to pause when the motor case is above 65 °C and continue below 55 °C, so that the PETG housing does not soften.
14. As the owner, I want the rig to pause at a driver temperature prewarning and continue when it clears, so that the driver does not shut down in a move.
15. As the owner, I want the rig to mark a run as interrupted when the angle at boot does not agree with the stored position, so that a run with a fall does not continue unseen.
16. As the owner, I want the rig to continue a test when Wi-Fi, the broker or the Pi stops, so that a network outage does not end a test of many days.
17. As the owner, I want a network or serial write to never delay the motion or the encoder check, so that a slow network cannot cause a lost step or a late stop.

### Units and weights

18. As the owner, I want to select the installed unit with `unit 1` or `unit 2`, so that the rig uses the correct motor settings.
19. As the owner, I want each unit to have its own `IRUN`, tolerance and backlash baseline in NVS, so that a unit change needs no screwdriver and no new calibration.
20. As the owner, I want each unit to have its own summary, so that the results of the two units never mix.
21. As the owner, I want the rig to require a home step after a unit change, so that the position is correct for the new unit.
22. As the owner, I want the rig to do the baseline step first when a unit has no baseline, so that each unit has a tolerance before a test.
23. As the owner, I want to set the weight with `weight <kg>`, so that each record carries the load that I hung.
24. As the owner, I want each record to show the unit and the weight, so that I can see a mistake in the setup.

### Lost-step detection

25. As the owner, I want the rig to compare the AS5600 angle with the step count about 1 000 times each second during a move, so that it finds a lost step at once.
26. As the owner, I want the rig to remove the offset between the angle and the steps at the start of each move, so that the constant play in the spline does not count as a loss.
27. As the owner, I want the rig to measure the normal difference in a baseline step, so that the tolerance agrees with the real windup of the printed parts.
28. As the owner, I want the tolerance to be the baseline difference plus 2 counts, so that normal windup does not stop a test.
29. As the owner, I want the rig to report whether it can find a single motor stall (32 microsteps), so that I know the limit of the detection.
30. As the owner, I want to set the tolerance by hand, so that I can override the baseline.
31. As the owner, I want the size of each loss in the record, so that I can tell a motor stall (32 microsteps), a tooth jump (133 or 400 microsteps) and a slip apart.
32. As the owner, I want the rig to stop and hold at a loss, so that I can see the state of the rig at the fault.

### Home and backlash

33. As the owner, I want a home step against the top limit switch, so that the rig has an absolute reference.
34. As the owner, I want a home check at each wear cycle, so that drift in the step count is found before it gets to an end of the travel.
35. As the owner, I want the rig to measure the backlash automatically with the container on the floor pad, so that I do not have to remove the load by hand.
36. As the owner, I want each backlash measurement repeated 3 times with the mean and the spread, so that one bad reading does not mislead me.
37. As the owner, I want the first backlash measurement of a unit to be its baseline, so that the later measurements compare with it.

### Speed test

38. As the owner, I want a speed sweep through a common grid of intervals (300 to 40 µs, 50 µs for the 42HS60), so that the two motors compare at the same speeds.
39. As the owner, I want 3 full cycles at each speed, so that the lift and the lowering are both tested.
40. As the owner, I want the same acceleration as the blind, so that the result is true for the blind.
41. As the owner, I want the rig to repeat a speed one time after a loss, so that a single random event does not decide the result.
42. As the owner, I want the sweep to stop at the first failed speed, so that the gearbox gets no shock for no new information.
43. As the owner, I want a rest of 30 s between the speeds, so that the temperature stays in the limits.
44. As the owner, I want a run order of StealthChop2 then SpreadCycle at 1, 2.5 and 4 kg, then a 5 kg stress run, so that the useful data is recorded before the heaviest runs.
45. As the owner, I want a backlash measurement before and after the speed tests of each unit, so that I see damage from the speed tests.
46. As the owner, I want one result record for each sweep, so that I can compare the sweeps in one table.

### Wear test

47. As the owner, I want a wear test at 4 kg, 150 µs and StealthChop2 for 10 000 cycles, so that it copies about 10 years of heavy use of the worst real blind.
48. As the owner, I want each 10th cycle to stop at 50 %, 25 % and 75 %, so that the test includes starts and reversals at mid positions.
49. As the owner, I want a rest of 30 s after each move at the hold current, so that the duty is realistic.
50. As the owner, I want the summary to count the cycles, the starts and the reversals, so that I can compare the units.
51. As the owner, I want a backlash measurement each 250 cycles, so that I see the wear trend.
52. As the owner, I want the wear test to end at 2× the baseline backlash or at +50 % in 24 hours, so that it stops before a part fails completely.
53. As the owner, I want the rig to pause at 1 000, 5 000 and 10 000 cycles for a visual check, so that I can examine the spline mark, the bolts, the dust and the motor mount.
54. As the owner, I want a command to continue after a visual check, so that the test continues only when I approve.
55. As the owner, I want a 4 kg speed sweep at the end of the wear test, so that I see if the wear reduced the torque margin.
56. As the owner, I want one result record at the end of the wear test, so that I have the full result in one place.

### Records and monitoring

57. As the owner, I want one record for each move, so that I can examine each move after the test.
58. As the owner, I want each fault, pause and restart as an event with QoS 1, so that no event is lost on the network.
59. As the owner, I want live values each second during a move, so that I can watch the rig from a distance.
60. As the owner, I want a retained status topic, so that Home Assistant or any client sees the current state at once.
61. As the owner, I want a summary on the device with the totals and the last 32 fault events, so that the results survive an outage or a reset.
62. As the owner, I want a `status` command that reads the summary, so that I can examine the rig with no logger.
63. As the owner, I want a logger service on the Pi that writes all the rig topics with timestamps to a file, so that I have the full record without the Mac.
64. As the owner, I want the logger to add to the Pi without a change to z2m, so that my home automation is not at risk.

## Implementation Decisions

### Build and modules

- A new PlatformIO env builds the rig firmware with its own `app_main`. It has no Zigbee and no keypad.
- The rig uses these modules of the blind firmware again without a change: `motion` (GPTimer step ISR), `ramp`, `key_filter` (for the limit switch input), and the RF-switch code.
- The rig env lowers `RAMP_MIN_CRUISE_US` for the rig only, to permit intervals down to 40 µs. The blind firmware keeps its floor.
- New modules:
  - **`rig_ctl`** (pure): all the decisions, behind a `rig_ports_t` table, with one entry point `rig_handle(event)`. Only the dispatcher task calls it, one event at a time. This copies the `ctl` pattern of the blind firmware.
  - **`skip_check`** (pure): compares the AS5600 angle with the step count. It handles the 4096-count wraparound, the count over many turns (24 000 microsteps = 4096 counts for each output revolution), the offset at the start of a move and the tolerance. It runs in the 1 kHz sampler task and posts a loss event. It makes no other decision.
  - **Command parser** (pure): turns a text command from MQTT or serial into an event.
  - Thin adapters with no decisions: TMC2209 UART, AS5600 (I²C), DS18B20 (1-Wire), limit switch, MQTT and Wi-Fi, SNTP, serial console, and NVS storage for the rig.
- The concurrency rule of the blind firmware applies. The step ISR and the 1 kHz sampler make no decisions. Only `rig_handle` decides. Network and serial writes do not block. When a write cannot go out, the data is dropped.

### `rig_ports_t` (port groups)

- Motion: start a move with a profile, stop, read the current steps.
- Driver: write and read back the registers, read `DRV_STATUS` and `SG_RESULT`, enable and disable.
- Sensors: read the encoder angle (for home and backlash), read the motor case temperature, read the limit switch.
- Storage: load and save the settings and the summary for each unit.
- Publish: publish a record structure on a topic, with QoS and retain flags. The adapter makes the JSON.
- Clock and timers: the current time, the rest timers.

### Pin map (Seeed XIAO ESP32C6)

| Pin | GPIO | Function |
|---|---|---|
| D0 | GPIO0 | TMC2209 UART RX (wired to `PDN_UART`) |
| D1 | GPIO1 | DS18B20 data (4.7 kΩ pull-up to 3V3) |
| D2 | GPIO2 | Status LED (optional) |
| D3 | GPIO21 | KW11-2 limit switch (NC to GND, internal pull-up) |
| D4 | GPIO22 | AS5600 SDA |
| D5 | GPIO23 | AS5600 SCL |
| D6 | GPIO16 | Not used. It stays empty as a buffer next to DIR. |
| D7 | GPIO17 | DIR |
| D8 | GPIO19 | STEP |
| D9 | GPIO20 | EN |
| D10 | GPIO18 | TMC2209 UART TX, through 1 kΩ to `PDN_UART` |
| internal | GPIO3, GPIO14 | RF switch (GPIO3 low, GPIO14 for the U.FL antenna) |

The research suggested D6 for UART RX. This spec uses D0, because `HARDWARE.md` keeps D6 empty as a buffer next to DIR (GPIO17). `DIAG` is not connected.

### Driver settings

- `MS1` and `MS2` on GND: UART address 0, and 1/8 microstep.
- `GCONF.pdn_disable` is set. `I_scale_analog = 1`, `vsense = 0`.
- Vref stays at 1.92 V for the two units.
- `IRUN = 31` for the 17HS4401 (1.36 A RMS). `IRUN = 27` for the 42HS60 (1.19 A RMS).
- `IHOLD = 16` for the two units. `TPOWERDOWN` stays at the default. `IHOLDDELAY` gives a gradual decrease.
- The default mode is StealthChop2 with `TPWMTHRS = 0`, as on the blind. A test parameter selects SpreadCycle.

### Stop conditions

| Event | Action | Restart |
|---|---|---|
| Loss larger than the tolerance | Stop and hold. Record the loss. | The speed sweep applies its own rule. The wear test stops. |
| Top limit switch, or a broken wire | Stop at once and hold. Fault. | A command |
| Driver fault (`ot`, short circuit, open load) | Disable the driver. | A command |
| Driver prewarning (`otpw`) | Finish the move, then pause at the hold current. | Automatic, when `otpw` clears |
| Motor case above 65 °C | Finish the move, then pause at the hold current. | Automatic, below 55 °C |
| Interrupted run at boot | Do not continue. | A command, after a home step |
| Network outage | Continue. | — |

### Procedures

- **Home step:** lift slowly until the limit switch opens, then set the reference.
- **Baseline step:** a few cycles at 300 µs with the heaviest weight of the next test. The tolerance is the largest normal difference plus 2 counts. NVS stores it for the unit.
- **Backlash measurement:**
  1. Lower until the encoder shows that the load has gone (the container is on the pad).
  2. Lower about 10 mm more.
  3. Turn slowly forward, then back, until the angle changes each time. Count the steps between the two changes.
  4. Repeat 3 times.
  5. Wind back up.
- **Speed sweep:** refer to the speed-test user stories (38–46).
- **Wear test:** refer to the wear-test user stories (47–56).

### Commands (MQTT and serial)

`wifi`, `mqtt` (serial only), `unit <1|2>`, `weight <kg>`, `home`, `baseline`, `backlash`, `start speed` (with the mode as a parameter), `start wear`, `continue`, `stop`, `release`, `tolerance <microsteps>`, `status`. The plan sets the exact syntax.

### MQTT topics (JSON)

| Topic | When | QoS / retain |
|---|---|---|
| `rig/<unit>/move` | End of each move | 0 |
| `rig/<unit>/event` | Each fault, pause or restart | 1 |
| `rig/<unit>/backlash` | Each backlash measurement | 1 |
| `rig/<unit>/baseline` | Each baseline step | 1 |
| `rig/<unit>/speedtest` | End of each sweep | 1 |
| `rig/<unit>/weartest` | End of each wear test | 1 |
| `rig/<unit>/live` | Each second during a move | 0 |
| `rig/status` | Each change of state | 1, retained |

The fields of each record are in the resolution of "Grilling: records and failure criteria" and of the two profile tickets.

### NVS (rig namespace)

- The credentials (Wi-Fi and MQTT).
- The installed unit.
- For each unit: the motor name, `IRUN`, the tolerance, the backlash baseline, and the summary (cycles, starts, reversals, revolutions, run hours, faults, last 32 fault events).
- The state of the current run (profile, step, position), for the boot check of an interrupted run.
- The summary is saved at intervals and at each event, not at each move, to limit flash wear.

### Mechanics (owner work in Fusion)

- A board of 300 × 1200 mm on a 2 m work surface, with 300 mm past the edge, held by two clamps.
- Bracket 1 (printed L with fins for the full height) holds the gearbox housing at its 4 holes with M4 bolts and nuts. It must carry the 42HS60 unit (about 600 g).
- Bracket 2 (printed L with fins for the full height) holds the support bearing, and the AS5600 1–2 mm in front of a diametric magnet in the end of the tube.
- The spool tube has an outside diameter of 32 mm and the internal spline of the roller tube. The cord winds in one layer, in an area 40 mm wide or more.
- A slot cord guide in a hole 100 mm or more beyond the edge of the work surface.
- A KW11-2 switch under the board, and a stopper on the cord.
- The travel is about 1450 mm (about 13.6 rev).
- A new Fusion design "Gearbox test rig" in the project `BlindsController2`, exported with the same conventions as the gearbox.

## Testing Decisions

- A good test drives a module only through its public interface and checks what it does through its ports. It does not check internal state.
- **`rig_ctl`** is tested on the host with Unity in the `native` env, with fake ports that record the calls. Prior art: `test_ctl`. The tests cover:
  - each row of the stop table;
  - the speed sweep (pass, a loss with a repeat that passes, a loss with a repeat that fails, the stop at the first failed speed, the 42HS60 grid);
  - the wear test (the mixed cycle each 10th cycle, the backlash interval, the visual-check pauses, the end conditions 2× and +50 % in 24 hours, the end sweep);
  - the baseline and the "single stall cannot be found" result;
  - the backlash procedure (load release, the two changes, 3 repeats);
  - the `unit` change (the home step is required, the baseline when there is none, separate summaries);
  - the boot check of an interrupted run;
  - the hold rules (enabled after a fault stop, disabled only by `release` or a driver fault);
  - that a publish failure does not change a decision.
- **`skip_check`** gets its own host test suite. Prior art: `test_key_filter` and `test_ramp`. The tests cover:
  - the wraparound at 4096 counts in the two directions;
  - the count over many turns;
  - the offset at the start of a move;
  - a loss just below and just above the tolerance;
  - losses of 32, 133 and 400 microsteps;
  - encoder noise of ±1 count that must not cause a loss.
- **The command parser** is tested in the `rig_ctl` suite: each command, wrong arguments, and that `status` never contains a password.
- `motion`, `ramp` and `key_filter` keep their present tests.
- The adapters (UART, I²C, 1-Wire, MQTT, Wi-Fi, NVS) get no host tests. A bench checklist tests them on the hardware.
  - Measure the real STEP period at each interval of the grid with a logic analyzer.
  - Read back the registers.
  - Find the NC pin of the KW11-2 with a multimeter.
  - Measure the coil resistance of the 42HS60.

## Out of Scope

- A formal qualification of the gearbox (a certified result such as "good to X rpm for Y hours").
- Changes to the blind firmware. The research found three possible problems:
  - the step period is probably 4–5 µs longer than the timer value;
  - the weight can turn the gearbox backwards when the driver is disabled;
  - the 42HS60 can set the fleet speed limit.

  The rig measures these. Changes to the blind firmware and to `HARDWARE.md` come after the measurement, as separate work.
- StallGuard for skip detection. `SG_RESULT` is only recorded as a load trend.
- Automatic detection of the installed unit.
- The test of the driver temperature in an enclosure.
- The Fusion CAD work itself. The owner does it.

## Further Notes

### Decisions on the map

- Research: skip-detection options on the XIAO and the TMC2209 V1.3 (#15)
- Research: speed limit of the 17HS4401 on the TMC2209 at 24 V (#16)
- Research: speed limit and driver settings for the 42HS60-1504JA05-D20-03 (#25)
- Grilling: spool load (#18)
- Grilling: skip-detection method for the rig (#17)
- Grilling: control interface for the rig (#19)
- Grilling: driver settings for the rig (#20)
- Grilling: rig frame and mechanics (#23)
- Grilling: records and failure criteria (#24)
- Grilling: speed test profile (#21)
- Grilling: wear test profile (#22)

The three research notes are on the local branches `research/skip-detection`, `research/speed-limit` and `research/42hs60-speed-limit`. They are not pushed.

Concept drawing (private): https://claude.ai/artifact/FXLXc8T28VVHLh5yVLnLWD

### Open checks before the plan

- Make sure that the MQTT broker on the Pi accepts a new client, and find which authentication it uses.
- Read the part number on the 42HS60 label. `HARDWARE.md` spells it `2HS60-1504JA05-020-03`.
- Measure the coil resistance of the 42HS60. The research expects 2.5 Ω ±10 %.
- If the bare test board has a buck regulator to the XIAO 5V pin, fit the 1N5822 diode against USB backfeed.

