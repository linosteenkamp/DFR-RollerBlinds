# Research: skip-detection options on the XIAO and the TMC2209 V1.3

Ticket: GitHub issue #15, "Research: skip-detection options on the XIAO
and the TMC2209 V1.3". It is part of map #14 (gearbox test rig).
Researched on 2026-11-21 from manufacturer datasheets, the BTT V1.3
schematic and the Espressif documentation.

## Question

Which methods can find lost steps at the driven gear on this hardware?
For each method, this note gives what it detects, the wiring change on
the BIGTREETECH TMC2209 V1.3, and the limits.

## Recommendation

**Put a sensor on the driven gear. Use StallGuard4 only as an extra
signal.** StallGuard4 measures the load of the motor. It does not see
the gear train. A tooth that jumps in the 3D-printed gearbox gives a
position error at the driven gear, but the motor does not lose a step.
Thus StallGuard4 cannot find that error.

For the test rig, do these three items, in this sequence:

1. Install a slotted optical sensor (Vishay TCST2103) and one flag on
   the driven gear. At each edge, compare the step counter with the
   expected count. This finds all the lost steps that are more than
   approximately one motor stall (32 microsteps).
2. Connect `PDN_UART` and `DIAG`. Record `SG_RESULT` and the `DIAG`
   pulses. They tell you *why* a loss occurred (a motor stall or a
   gearbox fault). The `SG_RESULT` trend also shows a change in friction
   during a wear run.
3. Do a home check on the same flag at the start and at the end of each
   run. Approach the flag from the same direction each time.

An AS5600 magnetic encoder on the axis of the driven gear is a stronger
option, if the rig has space for a magnet on that axis. It gives the
angle continuously, at 4096 counts for each revolution.

## Facts about this hardware

| Item | Value | Source |
|---|---|---|
| Microsteps for each output revolution | 24 000 (1/8 microstep, 200 full steps, 1:15) | `CLAUDE.md`, `hardware/gearbox/README.md` |
| Gear train | 12 T → 36 T (3:1), 12 T → 60 T (5:1) | `hardware/gearbox/README.md` |
| Cruise range | 60–1000 µs for each microstep | `include/ramp.h` |
| Motor speed at the cruise range | 10.4–0.63 rev/s (625–37.5 rpm) | calculated: 1 / (interval × 1600) |
| Output speed at the cruise range | 41.7–2.5 rpm | calculated: motor speed / 15 |
| Start and stop interval of the ramp | 500 µs (1.25 rev/s at the motor) | `src/main.c` (`START_US`) |
| Spare XIAO pins on a blind unit | D0, D1, D6, D10. HARDWARE.md keeps D10 for `PDN_UART`. | `HARDWARE.md`, GPIO summary |
| Extra spare pins on the test board | D3, D4, D5 (no keypad) | issue #14 |

### Size of each fault, in microsteps

The size of a fault tells you which sensor resolution you need.

| Fault | Error at the driven gear | Microsteps |
|---|---|---|
| Motor stall (smallest loss) | 4 full steps of the motor | 32 |
| One tooth jumps at stage 1 (12 T driver into the 36 T wheel) | 1/36 of an idler revolution = 1/180 of an output revolution | 133 |
| One tooth jumps at stage 2 (12 T pinion into the 60 T gear) | 1/60 of an output revolution | 400 |
| The shaft attachment slips on the driven gear | Any value | Any value |

The datasheet says: "a stall always means the loss of four full steps"
([TMC2209 datasheet §11.3][ds]). The tooth values come from the tooth
counts.

## Method 1: TMC2209 StallGuard4 in StealthChop

### What it detects

StallGuard4 measures the load angle of the motor. The result is
`SG_RESULT` (0–510). A low value means a high load. The driver signals a
stall when `SG_RESULT ≤ SGTHRS × 2`. The driver updates `SG_RESULT` at
each full step ([datasheet §5.3 and §11][ds]). StallGuard4 is "optimized
for operation with StealthChop" ([§11.1][ds]). Thus it agrees with the
StealthChop2 mode of this board.

It detects:

- a motor stall: the rotor falls behind the field, and the motor loses
  steps in groups of four full steps;
- a high load before a stall, as a low `SG_RESULT`. A jam at the output
  gives a large torque at the motor, so StallGuard4 sees a jam well.

It does not detect:

- a tooth that jumps in the gearbox. The motor keeps its position, and
  the load is high only for a short time. A pulse at `DIAG` is possible
  only if the torque peak crosses the threshold. Do not rely on it;
- a slip between the driven gear and the shaft;
- the position of the driven gear. StallGuard4 gives no position data.

### Sensitivity through the 1:15 reduction

The gearbox divides the output torque by 15 (and by the efficiency η)
before the torque gets to the motor. Example: a 1 kg weight on a spool
of 15 mm radius gives 0.147 N·m at the output. At the motor, this is
0.0098 N·m / η. If η is 0.8, the motor sees approximately 0.012 N·m.
This is approximately 3 % of the 0.40 N·m holding torque of the
17HS4401. Thus a normal load changes `SG_RESULT` only a small amount.
The efficiency of the printed gears is not measured. The value 0.8 is an
assumption.

The motor values (0.40 N·m, 1.7 A, 1.5 Ω, 2.8 mH) come from vendor
listings, not from a manufacturer datasheet. Different vendors give
different values ([17HS4401 listing][17hs]). Measure the coil resistance
of your motor before you use them.

When the weight lowers, the weight drives the gear train. Then the motor
load is low or negative, and `SG_RESULT` is high. StallGuard4 can detect
a stall only in the lift direction. (This is an inference from the
function principle in [§11][ds]. It is not a datasheet statement.)

### Speed limits

The datasheet says that StallGuard4 "operates best at medium motor
velocities". Velocities "well below one revolution per second" make the
measurement unstable ([§11.5][ds]). The datasheet gives a rule of thumb
for the minimum speed:

```
(HoldingTorque / (2 × I_COIL)) × (2π × RPM / 60) / (R_COIL + 0.34 Ω) × 0.34 Ω > 0.05 V
```

With the vendor values of the 17HS4401, the result is **RPM > 22 at the
motor** (1.5 rpm at the output). The slow end of the cruise range is
37.5 rpm at the motor. This value passes the rule, but it is below
1 rev/s. Thus expect a weak signal at the slow end. The fast end
(10.4 rev/s) gives a back EMF of approximately 7.7 V. This is less than
24 V, so the field-weakening limit of [§11.5][ds] does not apply.

The driver enables the stall output on `DIAG` only when
`TCOOLTHRS ≥ TSTEP > TPWMTHRS` ([§5.3, §11.2][ds]). `TSTEP` is the time
between two 1/256 microsteps, in units of 1/fCLK (12 MHz). At 1/8
microstep, `TSTEP ≈ interval_µs × 12 / 32`:

| Step interval | Motor speed | TSTEP |
|---|---|---|
| 1000 µs | 0.63 rev/s | 375 |
| 500 µs (`START_US`) | 1.25 rev/s | 188 |
| 300 µs (`JOG_CRUISE_US`) | 2.1 rev/s | 113 |
| 150 µs (`RAMP_DEFAULT_CRUISE_US`) | 4.2 rev/s | 56 |
| 60 µs | 10.4 rev/s | 23 |

Start with `TCOOLTHRS` = 190. Then the stall output stays off during the
slow start and the slow stop of the ramp. Keep `TPWMTHRS` = 0. Then the
driver stays in StealthChop at all speeds ([§5.2][ds]).

All the registers reset to 0 at power-up, unless the datasheet gives a
different value ([§5][ds]). Thus `TCOOLTHRS` and `SGTHRS` are 0 after
each power-up, and the stall output is off. The firmware must write
these registers at each boot.

### Tuning

The datasheet gives this procedure ([§11.2][ds]):

1. Run the motor at the normal velocity. Monitor `SG_RESULT`.
2. Increase the load slowly. Find the lowest `SG_RESULT` before the
   stall. Set `SGTHRS` to half of this value.
3. Monitor `DIAG` and stop the motor at a pulse. If the motor stops
   before a stall occurs, increase `SGTHRS`.
4. Check the setting from 75 % to 150 % of the velocity, and at the
   lowest and the highest temperature.

Each tuning is correct for one motor, one current and one velocity
range. Each unit has its own Vref and possibly a different motor. Thus
each unit needs its own `SGTHRS`.

### Wiring change on the BTT V1.3

The BTT V1.3 schematic ([BTT V1.3 schematic][btt-sch]) shows this:

- `PDN_UART` (chip pin 14) connects directly to header pin 5 of the
  logic row (net `UART_RX`). A 20 kΩ resistor (R6) pulls it to GND.
  This is the first `PDN` pin in the HARDWARE.md layout.
- The second `PDN` pin (net `UART_TX`) connects to `PDN_UART` only if
  you bridge the R10 pads. R10 is not installed at the factory
  ([BTT V1.3 manual][btt-man], pages 6–7).
- `DIAG` (chip pin 11) connects directly to the downward pin of the
  `DIAG` row (header 18). There is no resistor. `DIAG` is a push-pull
  output at the `VCC_IO` level, high at a stall or at a driver error
  ([datasheet §2.2][ds]). `VCC_IO` is 3.3 V on this board, so the
  XIAO can read `DIAG` directly.
- `MS1` and `MS2` are on GND. In UART mode, these pins also set the UART
  address. Thus the address is 0 ([§4.3][ds]). The microstep setting
  stays 1/8, because `mstep_reg_select` resets to 0.

Make these changes:

1. Connect `DIAG` to a free XIAO GPIO. Use an interrupt on the rising
   edge.
2. Connect `PDN_UART` to the XIAO. The datasheet shows two methods
   ([§4.3, Figure 4.1][ds]):
   - **Two pins (recommended for the rig):** XIAO TX through a 1 kΩ
     resistor to `PDN_UART`. XIAO RX directly to `PDN_UART`.
   - **One pin:** ESP-IDF lets TX and RX share one GPIO ("single wire
     mode"). Espressif tells you to protect the pad with open drain and
     a pull-up ([ESP-IDF UART][idf-uart]). The 20 kΩ pull-down on the
     BTT board is stronger than the internal pull-up of the ESP32-C6.
     Thus add an external pull-up of 1–4.7 kΩ to 3.3 V.
3. In the firmware, set `GCONF.pdn_disable` = 1. The datasheet says:
   "Set this bit, when using the UART interface!" ([§5.1][ds]).

Keep the Vref trimpot. `GCONF.I_scale_analog` resets to 1, so the
driver still uses the `VREF` voltage for the current ([§5.1][ds]).

The UART accepts 9000 baud to fCLK/16 and detects the baud rate
automatically ([§4.1][ds]). The ESP32-C6 has two HP UARTs. The GPIO
matrix connects UART1 to the selected pins ([ESP-IDF UART][idf-uart]).
The console uses USB-Serial-JTAG, so UART0 and UART1 are free.

Suggested pins on the test board: D10 (GPIO18) for TX, D6 (GPIO16) for
RX, D0 (GPIO0) for `DIAG`. On a blind unit, use D10 alone (one-pin
method) and D0 for `DIAG`.

### Limits

- It detects a motor stall only. It does not see the gearbox or the
  shaft.
- It needs tuning for each unit, each current and each speed range.
- The signal is weak below approximately 1 rev/s at the motor, and near
  a resonance speed ([§11.5][ds]).
- It works in the lift direction only, when a weight is on the spool.
- `DIAG` also goes high at a driver error (over-temperature, short
  circuit, charge pump under-voltage) ([§15.4][ds]). Read `DRV_STATUS`
  through UART to find the cause. `EN` high resets the error.
- The firmware must write the registers again after each power-up.

### Additional use for the rig

Read `SG_RESULT` through UART at regular intervals during a wear run.
The value goes down when the load goes up. A slow decrease over many
hours can show more friction in the gearbox. This uses the same wiring.

## Method 2: a sensor on the driven gear

### What it detects

A sensor on the driven gear sees the real angle of the gear. At each
pulse, the firmware compares the step counter with the expected count.
The difference is the loss. This finds all the faults in the table
above: a motor stall, a tooth jump at each stage, and (if the flag is on
the shaft attachment) a slip on the shaft. It also finds the backlash if
you compare the edge in the two directions.

### Resolution

The pitch diameter of the 60 T module 1 gear is 60 mm. At a flag radius
of 25 mm, one output revolution is 157 mm of travel. Thus one microstep
is approximately 6.5 µm at the flag. The repeatability of the sensor
edge sets the smallest loss that you can see.

| Sensor | Data | Smallest loss that you can see (estimate) |
|---|---|---|
| Slotted optical sensor, Vishay TCST2103 | Gap 3.1 mm, aperture 1 mm, resolution 0.6 mm (path of the shutter), daylight filter ([TCST2103][tcst]) | The edge position repeats much better than the 0.6 mm transition width. Expect better than 32 microsteps. Measure it on the rig. |
| Hall switch, TI DRV5023 (unipolar, open drain) | 2.5–38 V supply, 30 kHz bandwidth, hysteresis 1.5 mT typ. (FA version), status active ([DRV5023][drv5023]) | The edge moves with the magnet gap and the temperature. Expect tooth-jump size (133 microsteps), but not a single stall. |
| Magnetic encoder, ams AS5600 | 12-bit angle (4096 counts for each revolution), I²C at address 0x36, or analog or PWM output on `OUT`. It needs a diametric magnet on the axis. 3.0–3.6 V operation is permitted. System INL ±1° ([AS5600][as5600]) | 5.9 microsteps for each count. The INL is a fixed error for each angle. Thus compare the same angle from revolution to revolution. |

The INL of the AS5600 (±1°, 67 microsteps) limits the absolute angle.
It does not limit the change from one revolution to the next at the same
angle.

### Number of pulses for each revolution

- **One pulse for each revolution.** You need one flag. At 2.5–42 rpm,
  this is one check each 24–1.4 s. This is sufficient for a wear run.
- **More pulses (N slots).** The firmware checks every 24 000 / N
  microsteps. The print has a small error in the slot positions. Thus
  record the count for each slot on the first revolution. Then compare
  each slot with its own reference value.
- **Continuous angle (AS5600).** The firmware compares the angle with
  the step counter at any time.

### Wiring

None of these sensors needs a change on the BTT V1.3. They connect only
to the XIAO.

- **TCST2103:** LED anode through approximately 100 Ω to 3.3 V
  (approximately 20 mA; VF is 1.25 V typ. at 60 mA). LED cathode to GND.
  Collector to a GPIO with a 10 kΩ pull-up to 3.3 V. Emitter to GND.
  The collector current is 4 mA typ. at IF = 20 mA, so a 10 kΩ pull-up
  is correct ([TCST2103][tcst]).
- **DRV5023:** VCC to 3.3 V. GND to GND. OUT to a GPIO with a 10 kΩ
  pull-up. Put a magnet in a pocket of the gear.
- **AS5600:** in 3.3 V mode, connect VDD5V and VDD3V3 together to 3.3 V
  ([AS5600][as5600]). SDA and SCL to D4 and D5 on the test board (the
  default I²C pins of the XIAO, [XIAO pin map][xiao-mux]).

Suggested pins: on the test board, D3 (GPIO21) for a pulse sensor. On a
blind unit, D1 (GPIO1). D0–D2 are also ADC inputs
([XIAO pin map][xiao-mux]). Thus D1 can also read the analog `OUT` of
an AS5600.

### Limits

- You must add a flag, a magnet pocket or a magnet on the axis to the
  printed gear. The 60 T gear is the part that changed last, so this is
  a CAD change.
- The optical sensor needs a fixed bracket that holds the 3.1 mm gap
  accurately. Dust in the gap can block the beam.
- The Hall switch edge moves with the magnet gap and the temperature.
- On a blind unit, the driven gear is inside the gearbox. The space for
  a sensor is not checked.
- The AS5600 needs a magnet on the axis of rotation. On the rig, the
  free end of the shaft can hold it. On a blind unit, the roller tube is
  on that axis.

## Method 3: a mark on the gear with a home check

### What it detects

This method uses a reference point on the driven gear, but it checks it
only at specified times. Example: at the start of a run and after each
N cycles. The device moves to the mark, finds the edge, and compares
the step counter with the expected value. The difference is the total
loss since the last check.

There are two versions:

- **Visual mark.** Put a line on the gear and a fixed pointer on the
  housing. A person reads the offset. This needs no wiring. A 1 mm
  offset at a radius of 28 mm is approximately 1/176 of a revolution,
  or approximately 136 microsteps. Thus a person can see a stage-2 tooth
  jump (400 microsteps), possibly a stage-1 tooth jump (133), but not a
  single stall (32).
- **Sensor mark.** Use the sensor of method 2, but read it only during
  the home check. The resolution is the same as in method 2.

### Wiring

The visual mark needs no wiring. The sensor mark uses the same wiring
as method 2.

### Limits

- It gives the total loss since the last check. It does not tell you
  when the loss occurred, or how many events caused it.
- Approach the mark from the same direction each time. If you do not,
  the backlash adds to the result.
- A loss of more than half a revolution (12 000 microsteps) is
  ambiguous. This is very unlikely between two checks.
- The visual version needs a person at the rig. Thus it is not
  sufficient for a long unattended run.

## Comparison

| Method | Motor stall (32) | Tooth jump (133 / 400) | Shaft slip | When the loss occurred | Wiring change on the BTT V1.3 | XIAO pins | Main limit |
|---|---|---|---|---|---|---|---|
| StallGuard4 + `DIAG` | Yes (lift direction, tuned speed) | No | No | Yes (a pulse) | Connect `PDN_UART` and `DIAG`. Add 1 kΩ (two pins) or a pull-up (one pin). | 2–3 | Sees the motor only. Needs tuning for each unit. Weak below 1 rev/s. |
| Optical flag on the driven gear (TCST2103) | Probably (measure the edge repeatability) | Yes | Yes, if the flag is on the shaft attachment | Within one revolution | None | 1 | Needs a flag and a bracket in the CAD. |
| Hall switch on the driven gear (DRV5023) | No | Yes | Yes, if the magnet is on the shaft attachment | Within one revolution | None | 1 | Edge moves with the gap and the temperature. |
| Magnetic encoder on the axis (AS5600) | Yes (5.9 microsteps for each count) | Yes | Yes | At once | None | 2 (I²C) or 1 (analog) | Needs a magnet on the axis. Not possible on a blind unit. |
| Mark + home check, visual | No | Stage 2: yes. Stage 1: possibly. | Yes | No | None | 0 | Needs a person. Total loss only. |
| Mark + home check, sensor | As the sensor | As the sensor | As the sensor | Only between two checks | None | 1 | Total loss only. |

**Recommendation for the rig:** use an optical flag on the driven gear
(method 2) as the primary detector. Use the same flag for the home
check at the start and the end of each run (method 3). Connect
`PDN_UART` and `DIAG` to record `SG_RESULT` and stall pulses (method 1).
These signals tell a motor stall from a gearbox fault, and they show the
load trend. If the rig has space for a magnet on the free end of the
shaft, use an AS5600 in place of the optical flag.

## Sources

- [ds]: Analog Devices (Trinamic), *TMC2209 Datasheet*, Rev. 1.09,
  2023-02-16.
  https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf
  Sections used: §2.2 (signal descriptions), §4 (UART), §5.1 (`GCONF`),
  §5.2 (`TSTEP`, `TPWMTHRS`), §5.3 (`TCOOLTHRS`, `SGTHRS`,
  `SG_RESULT`), §11 (StallGuard4), §15.4 (`DIAG`, `INDEX`).
- [btt-sch]: BIGTREETECH, *TMC2209 V1.3 schematic*.
  https://github.com/bigtreetech/BIGTREETECH-Stepper-Motor-Driver/blob/master/TMC2209/V1.3/Schematic/TMC2209%20V1.3-SCH.pdf
- [btt-man]: BIGTREETECH, *TMC2209 V1.3 User Manual*, v1.00,
  2024-01-26, pages 6–7 (UART pin and R10).
  https://github.com/bigtreetech/BIGTREETECH-Stepper-Motor-Driver/blob/master/TMC2209/V1.3/manual/BIGTREETECH%20TMC2209%20V1.3%20User%20Manual.pdf
- [idf-uart]: Espressif, *ESP-IDF UART driver, ESP32-C6*, note on
  `uart_set_pin()` (single wire mode, GPIO matrix).
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html
- [xiao-mux]: Seeed Studio, *XIAO ESP32C6 pin multiplexing*.
  https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp32c6/ and
  https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/
- [tcst]: Vishay, *TCST2103, TCST2202, TCST2300 datasheet*, document
  81147. https://www.vishay.com/docs/81147/tcst2103.pdf
- [drv5023]: Texas Instruments, *DRV5023 product page and datasheet*.
  https://www.ti.com/product/DRV5023 and
  https://www.ti.com/lit/ds/symlink/drv5023.pdf
- [as5600]: ams OSRAM (now Infineon), *AS5600 datasheet*.
  https://www.infineon.com/assets/row/public/documents/24/49/infineon-as5600-datasheet-en.pdf
- [17hs]: 17HS4401 vendor listing (not a manufacturer datasheet).
  https://www.datasheetcafe.com/17hs4401-pdf-27114/

[ds]: https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf
[btt-sch]: https://github.com/bigtreetech/BIGTREETECH-Stepper-Motor-Driver/blob/master/TMC2209/V1.3/Schematic/TMC2209%20V1.3-SCH.pdf
[btt-man]: https://github.com/bigtreetech/BIGTREETECH-Stepper-Motor-Driver/blob/master/TMC2209/V1.3/manual/BIGTREETECH%20TMC2209%20V1.3%20User%20Manual.pdf
[idf-uart]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html
[xiao-mux]: https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp32c6/
[tcst]: https://www.vishay.com/docs/81147/tcst2103.pdf
[drv5023]: https://www.ti.com/product/DRV5023
[as5600]: https://www.infineon.com/assets/row/public/documents/24/49/infineon-as5600-datasheet-en.pdf
[17hs]: https://www.datasheetcafe.com/17hs4401-pdf-27114/
