# Research: how to decrease the motor whine of the US-17HS4401S on the TMC2209

Researched on 2026-11-26. The question came from the owner: the new
greased gearbox is almost silent, but the motor still has a small
whine. The unit has the **US-17HS4401S** motor (the motor of the smaller
blinds). The setting is "2 m in 35 s", with the same speed up and down.
The motor shows no strain.

This document uses the TMC2209 datasheet, the BIGTREETECH (BTT) module
documents, the motor data sheet, the ESP-IDF documents and source, and
the code of this repository. Each claim has a source. An item without a
primary source has the mark **(judgement)**.

## Answer

- **At "35 s", the motor turns at approximately 330–450 rpm.** The exact
  value depends on the span of the unit. The STEP frequency is
  approximately 9–12 kHz. The full-step frequency is approximately
  1.1–1.5 kHz. Both are in the most sensitive band of the human ear.
  Refer to [The operating point](#the-operating-point).
- **The StealthChop chopper is probably not the whine.** The default
  PWM frequency is 35.1 kHz. Each output switches at half of that value
  (17.6 kHz). This is at the top of the audible range. A test can make
  sure (the pitch of a chopper tone does not change with speed).
- **The whine is most probably a motor resonance that the speed
  excites, and the printed housing makes it louder.** On a unit with
  the same motor type, the measured motor hum was lowest at
  approximately 250–275 motor rpm. It was worse at higher speed
  ([DEVELOPER_GUIDE.md:207-227](../../DEVELOPER_GUIDE.md#noise-is-not-monotonic-with-speed)).
  "35 s" is above that speed band.
- **A new finding: the firmware never lets StealthChop2 complete its
  automatic tuning step AT#1.** AT#1 needs the driver enabled at
  standstill at run current for more than 130 ms. The firmware enables
  the driver 5 µs before the first step and disables it at the last
  step. Refer to [Automatic tuning](#automatic-tuning-at1-never-occurs).

**Mitigations, in sequence of value for effort:**

1. **Change `travel_time`** (no hardware change, no firmware change).
   Sweep it and listen. The quietest speed is probably slower than now,
   near 250–275 motor rpm. On a span of 345 000 steps, this is a
   `travel_time` of approximately 44–48 s. Resonance bands can be
   narrow. Thus also try values near 35 s (for example 32–40 s).
2. **Decouple the motor from the printed housing** (mechanical). The
   housing is a large thin plastic panel, and it can act as a
   soundboard (judgement). First do the listening tests in
   [Test sequence](#recommended-test-sequence) to find the source.
3. **Keep the driver enabled for 200–300 ms after each move**
   (small firmware change). This lets AT#1 occur. The effect on noise
   is not measured.
4. **Decrease Vref a small quantity** (trimpot). Try 1.92 V → 1.75 V.
   Do not go below approximately 1.6 V without a test under the full
   load. The 17HS4401 at 24 V is already near the lower current limit
   of the StealthChop regulator.
5. **Make the STEP timing free of jitter** (firmware, medium effort).
   The datasheet says: "MicroPlyer only works perfectly with a
   jitter-free STEP frequency." The present ISR adds a variable
   latency to each step period. Measure the jitter on the rig before
   you change the code.
6. **Add UART to the driver** (wiring + firmware, high effort). UART
   gives a higher PWM frequency, a shorter blank time, a fixed
   `PWM_GRAD` and other settings. The blind units have no UART wire
   now.

## The operating point

### Conversion

At 1/8 microstep through the 1:15 reduction
([2026-11-21-speed-limit.md](2026-11-21-speed-limit.md#conversion-from-step-interval-to-rpm)):

```
motor rpm            = 37 500 / interval_us
STEP frequency       = 1 000 000 / interval_us          (1/8 microsteps)
full-step frequency  = STEP frequency / 8               (200 full steps/rev)
electrical frequency = full-step frequency / 4          (one sine period of the coil current)
MicroPlyer rate      = STEP frequency × 32              (interpolation from 8 to 256)
```

### From "35 s" to the step interval

- z2m writes `travel_time` in seconds. `ramp_us_from_travel_time()`
  divides it by the calibrated span and truncates the result to whole
  microseconds ([src/ramp.c:67-74](../../src/ramp.c)). The limits are
  60–1000 µs ([include/ramp.h:35-39](../../include/ramp.h)).
- The real step period is longer than the alarm value. The firmware
  sets the timer count to zero inside the ISR, after the ISR latency
  and after the 3 µs STEP pulse
  ([src/motion.c:50-52](../../src/motion.c), [src/motion.c:68-75](../../src/motion.c)).
  The measured mean cost is approximately 9.6 µs for each step
  ([DEVELOPER_GUIDE.md:177-205](../../DEVELOPER_GUIDE.md#set-about-3-seconds-below-the-time-you-actually-want)).
- The span depends on the effective diameter of the roll (tube plus
  fabric). The spool tube of the rig is 32 mm
  ([docs/superpowers/specs/2026-11-21-gearbox-test-rig-design.md:229](../superpowers/specs/2026-11-21-gearbox-test-rig-design.md)).
  One installed 17HS4401 unit has a span of approximately 345 000 steps
  ([DEVELOPER_GUIDE.md:179-180](../../DEVELOPER_GUIDE.md#set-about-3-seconds-below-the-time-you-actually-want)).
  The span of the unit with the whine is not known. Thus the table
  gives a range. **Assumption:** `travel_time` = 35 is the value set
  in z2m, not a stopwatch time.

| Case | Span (steps) | `cruise_us` | Real period (µs) | Motor rpm | STEP freq | Full-step freq | Electrical freq | Real travel |
|---|---|---|---|---|---|---|---|---|
| Measured span of the lounge unit | 345 000 | 101 | ≈ 110.6 | ≈ 339 | 9.0 kHz | 1.13 kHz | 283 Hz | ≈ 38 s |
| 2 m on a 45 mm effective roll | 339 531 | 103 | ≈ 112.6 | ≈ 333 | 8.9 kHz | 1.11 kHz | 278 Hz | ≈ 38 s |
| 2 m on a 40 mm effective roll | 381 972 | 91 | ≈ 100.6 | ≈ 373 | 9.9 kHz | 1.24 kHz | 311 Hz | ≈ 38 s |
| 2 m on a 36 mm effective roll | 424 413 | 82 | ≈ 91.6 | ≈ 409 | 10.9 kHz | 1.37 kHz | 341 Hz | ≈ 39 s |
| 2 m on a 32 mm roll (no fabric) | 477 465 | 73 | ≈ 82.6 | ≈ 454 | 12.1 kHz | 1.51 kHz | 378 Hz | ≈ 39 s |

The span for 2 m is `2 m / (π × d) × 24 000`. The fabric makes the
effective diameter larger as it winds on. Thus the true value is
between the rows.

**How to find the true motor speed without the span.** Count the turns
of the roller tube during one full travel, and measure the time with a
stopwatch. Then motor rpm = 15 × 60 × turns / seconds.

Other frequencies at this point:

- The first gear mesh (12 T on the motor shaft,
  [hardware/gearbox/README.md:25](../../hardware/gearbox/README.md)) is
  approximately 67–91 Hz. This is a low hum, not a whine.
- The jog speed (`JOG_CRUISE_US = 300`, [src/main.c:64](../../src/main.c))
  gives approximately 121 rpm and a full-step frequency of
  approximately 400 Hz.

### The motor

The motor is the US-17HS4401S. The research found **no data sheet that
has the "US-" prefix**. "US" is possibly the brand code of the seller
(this is not confirmed). This document uses the Handson Technology data
sheet for the 17HS4401S. The earlier speed research used the same data
sheet ([2026-11-21-speed-limit.md](2026-11-21-speed-limit.md#motor-data)).

| Value | 17HS4401S (Handson) |
|---|---|
| Rated current | 1.7 A/phase |
| Phase voltage | 2.6 V |
| Phase resistance | 1.5 Ω ±10% |
| Inductance | 2.8 mH ±20% (1 kHz) |
| Holding torque | 43 N·cm |
| Rotor inertia | 54 g·cm² |
| Body length, mass | 40 mm, 280 g |

Other vendor listings for "17HS4401S" give 1.5 A or 40 N·cm. Measure
the phase resistance with a multimeter. A value near 1.5 Ω confirms
the winding. No source gives a resonance frequency or a noise curve for
this motor.

**The present current.** HARDWARE.md sets Vref to 1.92 V for this motor
([HARDWARE.md:586-589](../../HARDWARE.md#setting-vref-current-limit)).
The BTT formula gives Irms ≈ Vref × 0.71 = 1.36 A RMS (1.93 A peak),
80% of the rating ([HARDWARE.md:581-583](../../HARDWARE.md#setting-vref-current-limit)).

**The voltage headroom at this speed.** At 340 rpm, the model of the
earlier research gives a back-EMF of approximately 5.9 V peak. The
coil needs approximately 13 V peak in total (resistance, back-EMF and
inductance). StealthChop can supply 23.3 V peak at 24 V
([2026-11-21-speed-limit.md](2026-11-21-speed-limit.md#torque-model)).
Thus the driver can supply the full current. The voltage corner of
this motor is at approximately 660 rpm. **The driver is not near its
high-speed limit at "35 s".**

## Where the whine comes from

### 1. The StealthChop chopper frequency: probably not the cause

- With the internal 12 MHz clock and the default `PWM_FREQ = %01`, the
  StealthChop PWM frequency is 35.1 kHz (datasheet §6.2, Table 6.1,
  p. 40). The OTP default for `PWM_FREQ` is `%01` (datasheet §5.1.1,
  OTP bit otp2.2, p. 26).
- The datasheet also says: "The frequency measured at each of the
  chopper outputs is half of the effective chopper frequency fPWM"
  (`PWM_FREQ` table, p. 47). This is 17.6 kHz.
- 17.6 kHz is at the top of the audible range. Most adults do not hear
  it (judgement). A child or a pet can possibly hear it.
- The datasheet recommends 20–50 kHz "for most applications" (§6.2,
  p. 40). The setting is in this range.
- **A chopper tone has a fixed pitch.** If the pitch of the whine
  changes when the speed changes, the chopper is not the cause.

### 2. The step rate and the motor resonance: the most probable cause

- **The 1/8 microstep is interpolated to 256.** `MS1` and `MS2` at GND
  select 8 microsteps (datasheet §3.4, table "MS1/MS2", p. 14). The
  `CHOPCONF.intpol` bit has the default 1: "The actual microstep
  resolution (MRES) becomes extrapolated to 256 microsteps" (§5.5,
  `CHOPCONF` bit 28, p. 33). The MRES comes from the pins unless
  `GCONF.mstep_reg_select` is set (same table). Thus the coil current
  is a fine sine, not a staircase of 8 levels.
- Even with a perfect sine current, a hybrid stepper makes torque
  ripple at the full-step frequency and at two times the electrical
  frequency. The detent torque and the small differences between the
  two phases cause this (judgement, general motor theory). At "35 s",
  these tones are at approximately 1.1–1.5 kHz and 560–760 Hz.
- **A measurement on the same motor type agrees.** On the lounge unit
  (17HS4401, span ≈ 345 000), the motor hum was worst at `travel_time`
  30 (~415 rpm) and lowest at 45 (~275 rpm). At 60 (~205 rpm), it was
  worse again ([DEVELOPER_GUIDE.md:207-227](../../DEVELOPER_GUIDE.md#noise-is-not-monotonic-with-speed)).
  "35 s" on a similar span is approximately 340 rpm. This is between the
  "worst" and the "lowest" points.
- DEVELOPER_GUIDE.md explains the noise above 275 rpm as StealthChop
  that "gradually loses control of the current as the back-EMF
  increases". The voltage headroom above does not support this
  explanation at 340 rpm. A mechanical resonance of the motor, its
  mount and the housing is a better explanation (judgement). The
  measured result (a minimum near 250–275 rpm) stays correct.

### 3. The housing as a soundboard

The motor is attached directly to the printed housing. The housing has
a motor-mount boss and a large flat floor
([hardware/gearbox/README.md:135-136](../../hardware/gearbox/README.md)).
A large thin plastic panel radiates the motor vibration well, and it
has its own resonances (judgement). The grease made the gear noise
lower. Thus the motor tone is now the loudest part.

### 4. Step jitter from the ISR

- MicroPlyer "interpolates the time in between of two step impulses at
  the step input based on the last step interval" (datasheet §13.3,
  p. 65). The same section says: "MicroPlyer only works perfectly with
  a jitter-free STEP frequency."
- The firmware schedules each step relative to the time when the ISR
  runs. It sets the count to 0 in the ISR
  ([src/motion.c:74](../../src/motion.c)). Thus each change of the
  interrupt latency changes the period of that step.
- The mean cost (9.6 µs) is measured. The variation (the jitter) is
  not measured. At 100 µs, a variation of 1 µs is 1% of the period. The
  effect on noise is not known.
- The ISR is IRAM-safe (`CONFIG_GPTIMER_ISR_CACHE_SAFE=y` in
  `sdkconfig.seeed_xiao_esp32c6_zigbee`). Thus flash writes do not
  stop the steps. Other interrupts (for example the radio) can still
  delay the ISR (judgement).
- The cruise interval is a constant integer. Thus the truncation to
  whole microseconds gives no step-to-step variation at cruise
  ([src/ramp.c:28-38](../../src/ramp.c)). It affects only the ramps.

### 5. The current level

- A higher current gives a higher magnetic force and thus more
  vibration and magnetostriction (judgement).
- A lower current is not free. The StealthChop regulator has a lower
  current limit: `I_LowerLimit = tBLANK × fPWM × VM / RCOIL`
  (datasheet §6.3.1, p. 42). The datasheet calls it "a thumb value for
  the minimum nominal IRUN motor current setting". It does not say if
  the value is RMS or peak.
- The OTP default blank time is `TBL = %10` = 32 clocks (§5.1.1,
  otp0.7, p. 27). The `CHOPCONF` reset value `0x10000053` (p. 32)
  shows `TBL = %00` (16 clocks). The datasheet is not consistent.
- For the 17HS4401 (1.5 Ω) at 24 V and 35.1 kHz, the limit is
  **1.50 A** (32 clocks), 1.12 A (24 clocks) or 0.75 A (16 clocks).
  The present setting is 1.36 A RMS and 1.93 A peak. **Thus this motor
  is already near the limit.** A large decrease of Vref can make the
  StealthChop regulation worse, not better.
- The 42HS60 (2.5 Ω, Vref 1.69 V) has a lower limit of 0.90 A at
  32 clocks. Thus it has more room for a lower current.

### 6. Automatic tuning: AT#1 never occurs

- StealthChop2 tunes itself in two steps. AT#1 needs "Motor in
  standstill and actual current scale (CS) is identical to run current
  (IRUN)". It takes up to 2^20 + 2 × 2^18 clocks, ≤ 130 ms (datasheet
  §6.1, table "Automatic tuning timing and border conditions", p. 38).
  Figure 6.2 (p. 39) shows: "Requires stand still for >130ms".
- AT#2 tunes `PWM_GRAD_AUTO` during motion. The `pwm_autograd`
  description gives the precondition: "PWM_OFS_AUTO has been
  automatically initialized. This requires standstill at IRUN for
  >130ms" (§5.5, `PWMCONF` bit 19, p. 35). The datasheet gives "A
  typical range is 60-300 RPM" for AT#2, and up to 400 full steps from
  the OTP default (p. 38).
- **The firmware does not give AT#1 its conditions.** `motion_start()`
  sets `EN` low, waits 5 µs and starts the steps
  ([src/motion.c:149-159](../../src/motion.c)). The ISR sets `EN` high
  at the last step ([src/motion.c:59-61](../../src/motion.c)). The
  blind firmware never calls `motion_set_hold_at_idle()`. Thus the
  driver is never enabled at standstill.
- The cruise speed (330–450 rpm) is also above the typical AT#2 range.
  The ramp is only 800 microsteps (100 full steps,
  [src/main.c:63](../../src/main.c)).
- The datasheet does not say if a disable through `ENN` clears the
  tuning result. It does say that tuning "adapts to changed conditions
  whenever AT#1 or AT#2 conditions are fulfilled" (p. 39).
- The effect on noise is not known. The datasheet links the tuning to
  the quality of the current: "To reduce amplitude jitter, use
  pre-determined PWM_GRAD and set pwm_autograd = 0" (§6.2, p. 40).
- **Time window for AT#1 in this module.** The BTT module pulls
  `PDN_UART` to GND through R5 (20 kΩ) (BTT TMC2209 V1.2 schematic). Thus
  standstill current reduction is on (datasheet §3.4, table "PDN_UART",
  p. 14). Standstill is detected 2^20 clocks (87 ms) after the last
  step (§13.3, p. 65). The current then stays at IRUN for `TPOWERDOWN`
  = 20 × 2^18 clocks (437 ms) (§5.2, `IHOLD_IRUN` / `TPOWERDOWN`,
  p. 28). Thus a driver that stays enabled for 200–300 ms after the
  last step fulfils AT#1.

### 7. Items that are not the cause

- **No change to SpreadCycle.** The OTP default `TPWMTHRS` is 0
  (datasheet §5.1.1, otp1.5–1.7, p. 26). The `SPREAD` pad of the BTT
  module selects StealthChop
  ([HARDWARE.md:411-417](../../HARDWARE.md)). Thus the driver stays in
  StealthChop at all speeds. A change to SpreadCycle would be louder.
- **The ramps.** The ramp is approximately 0.1 s at each end. A steady
  whine during the travel does not come from the ramps.
- **Hold current.** The driver is disabled at idle. Thus there is no
  standstill noise.

## Mitigations

### (a) No hardware change

| # | Action | Expected effect | Effort | Evidence |
|---|---|---|---|---|
| a1 | Sweep `travel_time` and keep the quietest value | Large | None (z2m) | Measured minimum at 250–275 rpm on the same motor type ([DEVELOPER_GUIDE.md:207-227](../../DEVELOPER_GUIDE.md#noise-is-not-monotonic-with-speed)) |
| a2 | Keep `EN` low for 200–300 ms after each move | Not known; possibly medium | Small code change, OTA | AT#1 conditions (datasheet p. 35, 38, 39) |
| a3 | Schedule each alarm from the last alarm, not from the ISR time | Small to not known | Small code change | ESP-IDF GPTimer "Dynamic Alarm Value Update"; datasheet §13.3 |
| a4 | Make the STEP edges in hardware (GPTimer alarm → ETM → GPIO toggle, or MCPWM/LEDC at cruise) | Not known; removes edge jitter | Medium | ESP-IDF ETM docs; datasheet §13.3 |

**a1: the speed.** For a span of 345 000 steps, 275 rpm needs a real
period of 136 µs. This is `cruise_us` ≈ 127 µs and `travel_time` ≈ 44.
250 rpm needs `travel_time` ≈ 48. For larger spans, the values are
higher:

| Span (steps) | `travel_time` for 275 rpm | `travel_time` for 250 rpm |
|---|---|---|
| 340 000–345 000 | ≈ 43–44 | ≈ 48 |
| 382 000 | ≈ 48 | ≈ 54 |
| 424 000 | ≈ 54 | ≈ 60 |
| 477 000 | ≈ 61 | ≈ 67 |

The minimum was found on one unit with four points. The resonance of
this unit can be at a different speed. Thus listen at each value. The
real travel is approximately 3 s longer than `travel_time`.

**a2: the AT#1 dwell.** In `timer_cb()`, the ISR sets `EN` high at the
last step ([src/motion.c:59-61](../../src/motion.c)). Change this: keep
`EN` low, and set `EN` high from a one-shot timer 250 ms later. Cancel
the timer when a new move starts. 250 ms is more than the 131 ms of
AT#1 and less than the 524 ms before the current goes to `IHOLD`. Make
sure that the "driver is disabled at idle" rule of the design does not
need a different value. **Quick test without that change:** a test
build that calls `motion_set_hold_at_idle(true)` keeps the driver
enabled at idle ([src/motion.c:111-119](../../src/motion.c)). This also
lets you hear the chopper at standstill. Do not keep this build on a
unit: the motor then takes `IHOLD` current (53% of `IRUN`, datasheet
p. 26) all the time.

**a3: the timer.** The ESP-IDF example sets the next alarm to
`edata->alarm_value + interval`, with `auto_reload_on_alarm = false`
(ESP-IDF GPTimer, "Dynamic Alarm Value Update"). Then the latency does
not add to the period, and the mean speed agrees with `travel_time`.
The edge of each pulse still moves with the latency.

**a4: hardware edges.** The ESP32-C6 has ETM, GPIO ETM tasks and timer
ETM events (`SOC_ETM_SUPPORTED`, `SOC_GPIO_SUPPORT_ETM`,
`SOC_TIMER_SUPPORT_ETM` in `soc_caps.h` of ESP-IDF 5.5.1).
`GPTIMER_ETM_EVENT_ALARM_MATCH` is in `hal/timer_types.h`. The ETM
connects peripherals "without the intervention of CPU interrupts"
(ESP-IDF ETM, "Introduction"). A toggle gives a rising edge on each
second alarm, because the TMC2209 uses only rising edges without
`dedge` (datasheet §13.1, p. 63). Thus the alarm rate must be two times
the step rate. **Measure the jitter first** (the rig has a logic
analyzer in its procedure). If the jitter is less than approximately
0.5%, this work is probably not worth the effort (judgement).

### (b) Settings that need UART

| Setting | Reset or OTP default | Change to try | Reason |
|---|---|---|---|
| `PWM_FREQ` | `%01`, 35.1 kHz (p. 26, 40) | `%10` (46.9 kHz) | Moves the output switching (23.4 kHz) above hearing. The lower current limit increases (p. 42). |
| `TBL` | `%10` from OTP (p. 27) | `%01` or `%00` | Lowers the lower current limit (p. 42, p. 47). "%00 or %01 is recommended for most applications" (p. 33). |
| `PWM_GRAD` + `pwm_autograd` | OTP; autograd = 1 (p. 26, 35) | Read `PWM_GRAD_AUTO` after a tuned move, write it, set `pwm_autograd = 0` | "To reduce amplitude jitter" (p. 40) |
| `PWM_REG` | OTP (p. 26) | Tune with a current probe | Regulation speed (p. 35, 40) |
| `IRUN` | 31 (p. 26) | 24–31 | Fine current steps without the trimpot. Keep `IRUN` 16–31 (p. 28) and 8–31 in StealthChop (p. 42). |
| `TPOWERDOWN`, `IHOLD` | 20, 16 (p. 26, 28) | Keep `TPOWERDOWN` ≥ 2 | "A minimum setting of 2 is required to allow automatic tuning" (p. 28) |
| `TPWMTHRS` | 0 (p. 26) | Keep 0 | SpreadCycle is louder |
| `MRES`, `intpol` | Pins (1/8), intpol = 1 | Keep | Already interpolated to 256 |

**Can the production units use UART?**

- **The present blind units: no.** `PDN` is not connected
  ([HARDWARE.md:399](../../HARDWARE.md)). The blind firmware has no UART
  code. D10/GPIO18 is spare for this purpose
  ([HARDWARE.md:542](../../HARDWARE.md)).
- **What it takes on a present unit.** Copy the rig wiring: D10/GPIO18
  through 1 kΩ to the first `PDN` pin, and D0/GPIO0 directly to the
  same pin for RX ([docs/wiring/rig-uart.svg](../wiring/rig-uart.svg),
  [RIG_GUIDE.md:46-49](../../RIG_GUIDE.md)). Then reuse the UART driver
  of rig ticket #33 (not written yet). The firmware must write the
  registers again after each VM power-up, because the driver resets
  then (memory note for the rig, judgement for the blind).
- **The carrier board rev A.** Solder jumper JP1 ("UART (open)")
  connects `SPARE_D10` to `TMC_UART` (the `PDN` pin)
  (`hardware/carrier-board/carrier-board.kicad_pcb`, footprint
  `SolderJumper-2_P1.3mm_Open`). It has no 1 kΩ resistor and no
  separate RX line. ESP-IDF 5.5.1 permits "TX and RX to share the same
  IO (single wire mode)", with open-drain and a pull-up as protection
  (`driver/uart.h`, `uart_set_pin()` note). Thus the carrier board can
  probably write and read the driver through one GPIO. This is not
  tested.

### (c) Vref

The formula of the BTT manual
([HARDWARE.md:581-583](../../HARDWARE.md#setting-vref-current-limit)):

```
Irms = 325 mV / (0.11 Ω + 0.02 Ω) × 1/√2 × (Vref / 2.5 V) ≈ Vref × 0.71
```

| Vref | Irms | Peak | % of 1.7 A | Comment |
|---|---|---|---|---|
| 1.92 V (present) | 1.36 A | 1.93 A | 80% | HARDWARE.md value |
| 1.80 V | 1.27 A | 1.80 A | 75% | First step |
| 1.70 V | 1.20 A | 1.70 A | 71% | Second step |
| 1.60 V | 1.13 A | 1.60 A | 67% | Near the 1.50 A lower limit (32 clocks) |
| 1.50 V | 1.06 A | 1.50 A | 62% | At the lower limit; not recommended |

- **CAUTION:** a lower current gives less torque. The bench stalled
  once at 71% current, at 100 µs
  ([HARDWARE.md:957-967](../../HARDWARE.md#motion-speed-tuning)).
  Test each Vref value with a full travel up on a full roll.
- Set the Vref with the motor disconnected
  ([HARDWARE.md, procedure](../../HARDWARE.md#setting-vref-current-limit)).
- A Vref change "invalidates the result of the automatic tuning process"
  (datasheet p. 39). Thus listen after two or three full travels, not
  after the first.
- The expected effect is small to medium (judgement).

### (d) Mechanical

These items have no primary source. They are general acoustics
(judgement).

| # | Action | Expected effect | Effort |
|---|---|---|---|
| d1 | Find the radiator: listen with a screwdriver handle on the motor, then on the housing; press a hand flat on each panel during a move | Diagnosis | None |
| d2 | Put a rubber isolator between the motor and the housing: a thin EPDM or similar gasket under the motor face, rubber grommets on the M3 screws, and shoulder spacers so that the screws do not clamp the motor hard to the plastic | Medium to large | Medium (CAD + print) |
| d3 | Isolate the full unit from the wall or the window frame with rubber washers | Medium, if the frame radiates | Small |
| d4 | Make the housing stiffer and heavier: thicker walls, ribs, more infill near the motor boss | Small to medium | Medium (print) |
| d5 | Line the enclosure with foam | Small for a tonal whine | Small |

**CAUTION:** a soft motor mount lets the 12 T pinion move against the
36 T gear. This can change the mesh and the backlash, and it can let
the motor turn on its mount under load. Keep the motor pilot (the
centre boss) in a hard fit, and isolate only the face and the screws.
Do a full-load test after the change.

Commercial "NEMA 17 vibration dampers" (a rubber layer between two
steel plates) move the motor away from the housing. This changes the
centre distance of the gears only if the motor shaft is not parallel
to the plates (judgement). Their compliance in rotation can be too
large for a blind under load. Test before you use them.

### Other options (low value)

- **1/16 microstep through the pins.** `MS1` and `MS2` at `VCC_IO`
  select 16 microsteps (datasheet p. 14). This halves the interpolation
  for each STEP pulse, and thus the effect of one late pulse. It doubles
  all step counts. Thus it needs a firmware change and a new
  calibration of each unit. The expected effect is small (judgement).
- **A lower VM.** The lower current limit is proportional to VM
  (p. 42). But the voltage corner also goes down, to approximately
  330 rpm at 12 V (judgement, from the model). This is too near the
  operating point. Do not do this.

## Recommended test sequence

Do these steps on the unit with the whine. Change one item at a time.
Record each result.

1. Write the present `travel_time` and Vref on a card.
2. Count the turns of the roller tube during one full travel. Measure
   the time. Calculate the motor rpm: 15 × 60 × turns / seconds.
3. Do one travel at `travel_time` 35. Listen at 1 m.
4. Jog the blind with a key hold (approximately 121 rpm). Compare the
   pitch with step 3.
5. If the pitch changes with the speed, the cause is the motion, not
   the chopper. Continue at step 6. If the pitch does not change, the
   chopper is the probable cause. Go to the UART options.
6. Put the handle of a screwdriver on the motor and your ear on the
   handle. Then do the same on the housing panels.
7. Press a hand flat on the largest panel of the housing during a move.
   If the whine becomes lower, the housing radiates. The mechanical
   items d2–d4 are then the best fix.
8. Sweep `travel_time` in steps of 2 s: 31, 33, 35, 37, 39, 41, then
   up to the value for 250 rpm in the table above. Do one full travel
   at each value. Mark the quietest value.
9. At the quietest value, decrease Vref from 1.92 V to 1.80 V. Remove
   the motor connector first. Do two full travels. Listen again.
10. If the whine is lower and the motor has no strain, try 1.70 V. Do a
    full travel up with the full roll. Do not go below 1.60 V.
11. If you make a test build: call `motion_set_hold_at_idle(true)`. Do
    three full travels with a rest of 1 s or more between them. Listen
    for a change. Then remove the test build.
12. Set the final values. Write them in the unit record.

**CAUTION:** do the speed sweep before the Vref change. A stall makes
the position incorrect, and the device does not know this. If the
motor stalls, do a Re-home.

## Open points

- The span of the unit with the whine is not known. Thus the rpm is a
  range (330–450 rpm).
- No data sheet with the "US-" prefix exists online. The motor data
  come from the Handson 17HS4401S data sheet.
- The step jitter of the ISR is not measured.
- The effect of the missing AT#1 on noise is not measured. The
  datasheet does not say if `ENN` clears the tuning result.
- The datasheet does not say if the lower current limit is an RMS or a
  peak value. It also gives two different defaults for `TBL`.
- DEVELOPER_GUIDE.md explains the noise above 275 rpm as a loss of
  StealthChop control. The model in this document does not support
  this at 340 rpm. Measure it with UART (`PWM_SCALE_SUM`) on the rig.
- The mechanical items have no primary source.

## Sources

- [TMC2209 datasheet rev 1.09 (2023-FEB-16), Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf).
  The analog.com server did not answer on 2026-11-26. This research
  read an identical copy from
  [slimcdk/esphome-custom-components](https://github.com/slimcdk/esphome-custom-components/blob/master/esphome/components/tmc2209/docs/TMC2209_datasheet_rev1.09.pdf)
  (Git LFS, SHA-256 `1d9e8eed…7b0893`, 86 pages, footer "Rev. 1.09 /
  2023-FEB-16"). Sections used: §1.4 (p. 6), §3.4 (p. 14), §5.1.1 OTP
  (p. 26–27), §5.2 `IHOLD_IRUN`/`TPOWERDOWN` (p. 28), §5.5 `CHOPCONF`
  (p. 32–33) and `PWMCONF` (p. 35–36), §6 and §6.1 automatic tuning
  (p. 38–39), §6.2 (p. 40, Table 6.1), §6.3.1 lower current limit
  (p. 42), §6.5 (p. 44), p. 47 (`PWM_FREQ`, `TBL`), §7 (p. 48),
  §13.1 and §13.3 MicroPlyer (p. 63, 65), §16 (p. 70–71).
- [BIGTREETECH TMC2209 V1.2 schematic](https://github.com/bigtreetech/BIGTREETECH-TMC2209-V1.2/blob/master/Schematic/TMC2209-V1.2.pdf)
  (R5 20 kΩ from `PDN_UART` to GND; R1 20 kΩ pull-up on `ENN`;
  0.11 Ω sense resistors). HARDWARE.md says that V1.2 and V1.3 have the
  same design.
- [BIGTREETECH TMC2209 V1.2 manual](https://github.com/bigtreetech/BIGTREETECH-TMC2209-V1.2/blob/master/manual/TMC2209-V1.2-manual.pdf)
  (Vref range 0.2–2.2 V, factory Vref 1.2 V, `PDN` UART pin, the
  mute-mode pad).
- [Handson Technology 17HS4401S data sheet](https://www.handsontec.com/dataspecs/17HS4401S.pdf)
  (1.7 A, 1.5 Ω, 2.8 mH, 43 N·cm, 54 g·cm²).
- [ESP-IDF GPTimer, ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gptimer.html)
  ("Dynamic Alarm Value Update", "Cache Safety"; the page is v6.1.1,
  and the project uses ESP-IDF 5.5.1).
- [ESP-IDF ETM, ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/etm.html)
  ("Introduction", "GPIO Tasks").
- ESP-IDF 5.5.1 source in `~/.platformio/packages/framework-espidf`:
  `components/soc/esp32c6/include/soc/soc_caps.h`,
  `components/hal/include/hal/timer_types.h`,
  `components/esp_driver_gptimer/include/driver/gptimer_etm.h`,
  `components/esp_driver_uart/include/driver/uart.h`.
- This repository: [src/motion.c](../../src/motion.c),
  [src/ramp.c](../../src/ramp.c), [include/ramp.h](../../include/ramp.h),
  [src/main.c](../../src/main.c), `sdkconfig.seeed_xiao_esp32c6_zigbee`,
  [HARDWARE.md](../../HARDWARE.md),
  [DEVELOPER_GUIDE.md](../../DEVELOPER_GUIDE.md),
  [RIG_GUIDE.md](../../RIG_GUIDE.md),
  [docs/wiring/rig-uart.svg](../wiring/rig-uart.svg),
  `hardware/carrier-board/` (JP1),
  [hardware/gearbox/README.md](../../hardware/gearbox/README.md),
  [2026-11-21-speed-limit.md](2026-11-21-speed-limit.md),
  [2026-11-21-42hs60-speed-limit.md](2026-11-21-42hs60-speed-limit.md).
