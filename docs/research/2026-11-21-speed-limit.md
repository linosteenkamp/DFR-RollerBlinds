# Research: the speed limit of the 17HS4401 on the TMC2209 at 24 V

Ticket: GitHub issue #16 (part of map #14, the gearbox test rig).
Researched on 2026-11-21 from the TMC2209 datasheet, motor vendor data,
the ESP-IDF source and the code of this repository.

## Answer

- The motor, not the firmware and not the driver, sets the top speed.
- The model in this document gives a voltage corner at approximately
  **660 motor rpm (57 µs for each step, 44 rpm at the driven gear)**.
  Above the corner, the torque decreases quickly. At 40 µs, the torque
  is only approximately 40% of the torque at 150 µs.
- **Expected safe top speed: 70–80 µs for each step (470–540 motor rpm,
  31–36 rpm at the driven gear).** At this speed, the motor keeps
  approximately 87–90% of the torque that it has at the proven 150 µs.
- The present floor `RAMP_MIN_CRUISE_US = 60` is near the corner. It has
  almost no margin above the corner.
- The GPTimer ISR can make steps at 40–50 kHz (20–25 µs) without a
  problem. The TMC2209 accepts 187 kHz at 1/8 microstep. Thus the two
  limits are far above the limit of the motor.
- **The rig must sweep from 150 µs down to 40 µs** (16.7 to 62.5 rpm at
  the driven gear). Refer to [Sweep range for the rig](#sweep-range-for-the-rig).

The torque values are a model, not a measurement. No vendor publishes a
24 V pull-out curve for the 17HS4401. The rig must measure the real
limit.

## Conversion from step interval to rpm

At 1/8 microstep, the motor needs 1600 steps for each revolution
(200 full steps × 8). The 1:15 gearbox gives 24 000 steps for each
output revolution. Thus:

```
motor rpm      = 60 000 000 / (interval_us × 1600) = 37 500 / interval_us
driven-gear rpm = motor rpm / 15                    =  2 500 / interval_us
```

## Motor data

The 17HS4401 is a generic part number. Many Chinese vendors sell it, and
the values are not identical. The values below come from the Handson
Technology data sheet. A copy of the MotionKing data agrees, but gives
40 N·cm.

| Value | 17HS4401 |
|---|---|
| Rated current | 1.7 A/phase |
| Phase resistance | 1.5 Ω ±10% |
| Inductance | 2.8 mH ±20% (1 kHz) |
| Holding torque | 40–43 N·cm (this document uses 40 N·cm) |
| Rotor inertia | 54 g·cm² |
| Step angle | 1.8° (50 pole pairs) |

Sources: [Handson 17HS4401S data sheet](https://www.handsontec.com/dataspecs/17HS4401S.pdf),
[17HS4401 data (MotionKing), DatasheetCafe copy](https://www.datasheetcafe.com/17hs4401-datasheet-stepper-motor/).
Neither source gives a torque curve.

**Run current.** Bench3 uses Vref 1.92 V. The BTT formula in
[HARDWARE.md](../../HARDWARE.md#setting-vref-current-limit) gives
Irms ≈ Vref × 0.71 = **1.36 A RMS (1.93 A peak)**. This is 80% of the
rating of the motor.

**Reference curve.** The nearest motor with a published 24 V curve is
the 17HS16-2004S (42×40 mm, 2 A, 1.1 Ω, 2.6 mH, 45 N·cm). Its pull-out
torque at 2 A, 24 V, half step is approximately:

| Motor rpm | 60 | 90 | 150 | 300 | 375 | 450 | 525 |
|---|---|---|---|---|---|---|---|
| Pull-out torque (N·cm) | 22 | 34 | 32 | 31 | 30 | 29 | 28 |

Source: [OYOSTEPPER 17HS16-2004S pull-out torque curve](https://www.oyostepper.com/images/upload/File/17HS16-2004S1_Torque_Curve.pdf)
(values read from the graph). The curve stops at 525 rpm. The low value
at 60 rpm is the low-speed resonance of a half-step drive. The
microstep drive of this project does not have this problem to the same
degree.

## Torque model

The model has two parts.

**1. Voltage limit (physics).** The TMC2209 datasheet (§6.4) gives the
back-EMF constant as `CBEMF = HoldingTorque / (2 × ICOILNOM)` (RMS for
each coil). For the 17HS4401, this is 0.118 V/(rad/s) RMS, or
0.166 V/(rad/s) peak. The highest coil voltage in StealthChop is
`VM × 248/256` ≈ 23.3 V peak (datasheet §6.4, the `UPWM` formula). The
model finds the largest peak current that satisfies:

```
(I·R + E)² + (ωe·L·I)² ≤ Vmax²       E = CBEMF_peak × ωm,  ωe = 50 × ωm
```

The model then clips the current to the 1.93 A peak that Vref sets.
Below approximately 660 motor rpm, the driver can supply the full
current. Above this speed, the current and the torque decrease quickly.

**2. Real losses (from the reference curve).** The ideal model gives
45 N·cm for the 17HS16-2004S up to 525 rpm. The measured curve gives
34 N·cm at 90 rpm and 28 N·cm at 525 rpm. Thus the real torque is 76%
of the model at 90 rpm and 62% at 525 rpm. The losses are iron loss,
detent torque and the dynamic load angle. This document applies the
same derate factor to the 17HS4401:
`d(rpm) = 0.76 − 0.000322 × (rpm − 90)`.

## Speed table

Column notes:

- "Motor torque" is the modeled pull-out torque of the motor.
- "Driven-gear torque" assumes 80% efficiency for the 3D-printed 1:15
  gearbox. This value is an assumption, not a measurement.
- "Margin vs 150 µs" is the torque divided by the torque at 150 µs. The
  bench proved 150 µs on a real small blind on 2026-08-02
  ([HARDWARE.md, Motion speed tuning](../../HARDWARE.md#motion-speed-tuning)).
  A value below 1.0 means less torque than at the proven speed.

| Step interval (µs) | Motor rpm | Driven-gear rpm | Motor torque (N·cm) | Driven-gear torque (N·m) | Margin vs 150 µs |
|---|---|---|---|---|---|
| 1000 | 38 | 2.5 | 24.4 | 2.93 | 1.07 |
| 500 (`START_US`) | 75 | 5.0 | 24.4 | 2.93 | 1.07 |
| 300 (`JOG_CRUISE_US`) | 125 | 8.3 | 24.0 | 2.88 | 1.06 |
| 200 | 188 | 12.5 | 23.4 | 2.80 | 1.03 |
| **150** (`RAMP_DEFAULT_CRUISE_US`, proven) | 250 | 16.7 | 22.7 | 2.73 | 1.00 |
| 120 | 313 | 20.8 | 22.1 | 2.65 | 0.97 |
| 100 | 375 | 25.0 | 21.4 | 2.57 | 0.94 |
| 90 | 417 | 27.8 | 21.0 | 2.52 | 0.92 |
| 80 | 469 | 31.2 | 20.5 | 2.46 | 0.90 |
| 70 | 536 | 35.7 | 19.8 | 2.37 | 0.87 |
| 60 (`RAMP_MIN_CRUISE_US`) | 625 | 41.7 | 18.9 | 2.26 | 0.83 |
| 50 | 750 | 50.0 | 14.4 | 1.73 | 0.63 |
| 40 | 938 | 62.5 | 8.7 | 1.05 | 0.38 |
| 30 | 1250 | 83.3 | 2.3 | 0.28 | 0.10 |

The load torque of the blind is not known. Thus the table cannot give an
absolute margin. The bench result gives one fact: the real load stays
below the torque at 150 µs. The 100 µs stall on 2026-08-02 occurred at
the wrong Vref (71% current). Thus it does not give a second fact.

## StealthChop2 against SpreadCycle at high speed

Facts from the TMC2209 datasheet (rev 1.09):

- **This hardware uses StealthChop2 at all speeds.** The `SPREAD` pin at
  GND selects StealthChop (§3.4). All OTP bits are 0 from the factory
  (§5.1.1). With OTP 0, `TPWMTHRS = 0`. `TPWMTHRS = 0` means
  "StealthChop only" (§6.5). Thus no automatic change to SpreadCycle
  occurs.
- **Back-EMF limit.** "With rising motor velocity, the motor generates an
  increasing back EMF voltage. … It reduces the PWM voltage effective at
  the coil resistance and thus current decreases" (§6.4). StealthChop
  compensates with `PWM_GRAD` until the PWM amplitude reaches 255. Above
  that point, the current decreases (Figure 6.6). §6.6.1 names "a high
  velocity motion where the back EMF reaches the supply limit" as a
  cause of a current that does not reach the target.
- **The datasheet recommends SpreadCycle for high speed.** "For high
  velocity consider SpreadCycle in combination with StealthChop" (§6).
  "SpreadCycle may bring more stable operation in the upper velocity
  range" (§6.5). §1.4 says the same.
- **StealthChop reacts slowly to a load step.** The current regulation
  of StealthChop adapts over many PWM cycles (`PWM_REG`). §6.2 says that
  the feed-forward mode "will not react to … events like a motor stall".
  SpreadCycle regulates the current in each chopper cycle (§1.4). Thus a
  sudden load near the limit is more dangerous in StealthChop.
- **Automatic tuning (AT#2).** StealthChop2 learns `PWM_GRAD_AUTO`
  during motion "at a velocity where a significant amount of back EMF is
  generated". The typical range is 60–300 rpm. Up to 400 full steps are
  necessary from the OTP default `PWM_GRAD = 14` (§6.1). 400 full steps
  are 3200 steps at 1/8 microstep. Thus the first move after power-up
  can have less torque at high speed than the later moves.
- **The change-over (`TPWMTHRS`) needs UART or OTP.** A change at high
  speed causes a jerk. The back-EMF causes a phase shift of up to 90°
  between the voltage and the current. "A high jerk may even produce a
  temporary overcurrent condition" (§6.5). The datasheet recommends a
  low change-over speed. The pin-only wiring of this project cannot set
  `TPWMTHRS`. The `PDN_UART` pin (D10) is free for this
  ([HARDWARE.md](../../HARDWARE.md#motion-speed-tuning)).
- **Resonance.** SpreadCycle "offers … good resonance dampening over a
  wide range of speed and load" (§1.4). StealthChop gives "reduced
  mechanical resonance" (§1.4). The datasheet gives no resonance
  frequency for a specific motor. The rig must find it.

The voltage limit is the same for the two chopper modes. Both modes
cannot supply more than VM. Thus SpreadCycle does not move the corner
of 660 rpm by much. It can give more stable torque near the corner, and
it can recover from a short load step.

Source: [TMC2209 datasheet rev 1.09, Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf).
The analog.com server did not answer an automated download. This
research used an identical copy of rev 1.09 (2023-FEB-16) from a public
GitHub repository.

## Step-rate limits of the firmware and the driver

### TMC2209 STEP input

Datasheet §13.1 and §20.2:

- The minimum STEP high time and low time are 100 ns. The firmware uses
  a 3 µs pulse (`STEP_PULSE_US` in `src/motion.c`). This has a large
  margin.
- The maximum full-step rate is `fCLK/512`. With the internal 12 MHz
  clock, this is 23.4 kHz of full steps. At 1/8 microstep, this is
  187 kHz STEP input (5.3 µs), or 7000 motor rpm.
- MicroPlyer interpolates each 1/8 step to 256 microsteps (`intpol = 1`
  is the default, §5 `CHOPCONF`). It uses the last step interval. It
  operates down to approximately 12 Hz (§13.3). Thus the ramp start at
  500 µs (2 kHz) is not a problem.

### GPTimer ISR on the ESP32-C6

The code of `timer_cb()` in `src/motion.c` does these steps for each
step:

1. It sets STEP high, waits 3 µs (`esp_rom_delay_us`), and sets STEP
   low.
2. It updates the position and the index.
3. It calls `ramp_interval_us()` (`src/ramp.c`). In the ramp zone, this
   function does three 32-bit divisions and one 64-bit division. The
   64-bit division (`__udivdi3`) is in ROM on the ESP32-C6
   (`esp32c6.rom.libgcc.ld`, ESP-IDF 5.5.1). Thus it is IRAM-safe.
4. It calls `gptimer_set_raw_count(0)` and `gptimer_set_alarm_action()`.
   `sdkconfig.defaults` puts these functions and the ISR in IRAM
   (`CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM`, `CONFIG_GPTIMER_ISR_IRAM_SAFE`).

The CPU clock is 160 MHz (the ESP-IDF default for the ESP32-C6). The
3 µs pulse uses most of the ISR time. The interrupt entry and exit, the
arithmetic and the two GPTimer calls add an estimated 1–3 µs. This
estimate is not a measurement.

**Result.** The ISR uses approximately 4–6 µs for each step. At 20 µs
for each step, the ISR uses approximately 25% of the CPU. This leaves
time for the Zigbee stack. Thus the ISR can make 40–50 kHz (approximately
1900 motor rpm) in practice. This is approximately three times more
than the motor can do.

**A side effect: the real interval is longer than the alarm value.**
The ISR resets the counter to 0 only after the interrupt latency and the
3 µs pulse. Thus the real period is approximately
`interval_us + 4…5 µs`. At 60 µs, the motor turns approximately 7%
slower than the nominal value. At 150 µs, the error is approximately 3%.
The travel time that the firmware reports to z2m is thus a little short.
To remove this error, use `auto_reload_on_alarm` with a changed alarm
value, or subtract a measured offset. Measure the real STEP period on
the rig with a logic analyzer before you correct it.

## Expected safe top speed

- **Expected safe top speed: 70–80 µs for each step.** This is 470–540
  motor rpm, or **31–36 rpm at the driven gear**. The modeled torque is
  87–90% of the proven torque at 150 µs. The speed stays 20–30% below
  the voltage corner.
- **Expected stall zone: 50–57 µs and faster** (more than 660 motor rpm,
  more than 44 rpm at the driven gear). Here the torque falls quickly.
- **Do not trust 60 µs on an installed blind until the rig proves it.**
  The model gives only 17% less torque than at 150 µs. But the voltage
  corner is only 6% faster. A low supply voltage, a warm coil (higher R)
  or a different 17HS4401 version can move the corner below 60 µs.
- The 2HS60 has a different R, L and torque. Do not use these numbers
  for the 2HS60.

## Sweep range for the rig

1. Do the first sweep in StealthChop2, as installed (`SPREAD` at GND).
2. Start at 300 µs (the jog speed) as a baseline.
3. Go from 150 µs to 100 µs in steps of 10 µs.
4. Go from 100 µs to 40 µs in steps of 5 µs.
5. Stop the sweep at the first interval that loses steps. Then do the
   next weight.
6. Do the sweep at a minimum of three weights on the spool.
7. If you can change the `SPREAD` pad on the rig board, do the same
   sweep in SpreadCycle. Compare the stall intervals.

This range is **16.7 to 62.5 rpm at the driven gear** (250 to 938 motor
rpm). It includes the modeled corner with a margin on the two sides.

Before you start, do these steps:

- Lower `RAMP_MIN_CRUISE_US` for the rig environment only. The present
  floor of 60 µs blocks the sweep below 60 µs.
- Set the rig Vref to 1.92 V, as on bench3. A different Vref makes the
  results not valid for the installed units.
- Supply 24 V at the driver. A lower VM moves the corner to a lower
  speed.
- Record the real STEP period with a logic analyzer at each interval.

## Open points

- No vendor curve exists for the 17HS4401 at 24 V. The derate factor
  comes from a different motor and a half-step current-mode driver.
- The gearbox efficiency of 80% is an assumption.
- The ISR overhead of 1–3 µs is an estimate. The rig must measure it.

## Sources

- [TMC2209 datasheet rev 1.09, Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf):
  §1.4, §3.4, §5.1.1, §6, §6.1, §6.2, §6.4, §6.5, §6.6.1, §13.1, §13.3,
  §20.2.
- [Handson Technology 17HS4401S data sheet](https://www.handsontec.com/dataspecs/17HS4401S.pdf).
- [17HS4401 (MotionKing) data, DatasheetCafe copy](https://www.datasheetcafe.com/17hs4401-datasheet-stepper-motor/).
- [OYOSTEPPER 17HS16-2004S pull-out torque curve, 2 A, 24 V, half step](https://www.oyostepper.com/images/upload/File/17HS16-2004S1_Torque_Curve.pdf)
  and the [17HS16-2004S1 product page](https://oyostepper.com/goods-45-Nema-Size-17-Stepper-Motor-Bipolar-45Ncm-64ozin-2A-42x40mm-4-Wires-w-1m-Cable-Connector.html).
- ESP-IDF 5.5.1: `components/esp_rom/esp32c6/ld/esp32c6.rom.libgcc.ld`
  (`__udivdi3` in ROM) and
  `components/esp_system/port/soc/esp32c6/Kconfig.cpu` (160 MHz default).
- This repository: `src/motion.c`, `src/ramp.c`, `include/ramp.h`,
  `src/main.c`, `sdkconfig.defaults`, and
  [HARDWARE.md](../../HARDWARE.md) (Vref formula from the BTT manual,
  bench results of 2026-08-02).
