# Hardware & Wiring — DFR-RollerBlinds

This is the complete wiring reference, written up after the first bench
bring-up (2026-07-19/20) where most of the debugging time went into two
things: a keypad wired to the wrong pins, and a DRV8825 that looked dead but
was simply asleep. Both are covered explicitly below, with a troubleshooting
section at the end built directly from that session.

## Bill of materials

| Part | Role |
|---|---|
| DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`) | Controller (fallback: Seeed XIAO ESP32-C6 if it doesn't fit the enclosure — the design keeps the GPIO budget to 8 pins so a fallback is a pin remap, not a redesign) |
| 2HS60-1504JA05-020-03 bipolar stepper | Drive motor — 1.8°/step (200 full steps/rev), 1.5 A/phase class, 4-wire (2 coils) |
| DRV8825 breakout (Pololu-style) | Stepper driver, 1/8 microstep (hard-wired) |
| 3D-printed geartrain (`Blinds 2.step`) | 1:15 reduction to the roller tube |
| Mean Well 24 V DC PSU — **recommend LRS-50-24** (LRS-35-24 acceptable) | System supply |
| Step-down regulator 5 V / 3.2 A (VIN 5.3–50 V) | 24 V → 5 V for the FireBeetle |
| Membrane keypad: 2 arrow keys + function key | Local controls + calibration UX |
| External status LED (enclosure face) | State annunciator; onboard LED mirrors it |
| Multimeter | Required — for Vref, VMOT, and 3V3-rail checks below. Don't skip these. |

## Recommended bring-up order

Wiring everything at once and then debugging blind is how last session's
issues turned into a long back-and-forth. Do it in this order instead —
each stage is independently testable before moving to the next:

1. **FireBeetle alone.** Flash firmware, confirm serial boot log (see
   `DEVELOPER_GUIDE.md`). No other hardware connected yet.
2. **Keypad.** Wire and verify all three keys (procedure below) before
   touching the motor driver.
3. **DRV8825 logic pins only** (no motor, no VMOT yet). Verify `SLEEP̅`/`RESET̅`
   read 3.3 V and `EN̅` toggles per firmware.
4. **DRV8825 power + Vref**, motor still disconnected. Set current limit.
5. **Motor.** Connect coils last, VMOT already at the correct voltage.
6. **Full power chain** (24 V PSU + step-down to FireBeetle) together.

## Power chain

```
24 V PSU (LRS-50-24) ──┬─── DRV8825 VMOT  (+ ≥100 µF electrolytic across VMOT/GND,
                        │                    close to the driver — non-negotiable
                        │                    spike protection)
                        │
                        └─── Step-down regulator (5 V / 3.2 A, VIN 5.3–50 V)
                                     │
                                     └─── FireBeetle 2 5V/VCC pin
```

- 24 V feeds the DRV8825's **VMOT** pin directly. The **≥100 µF electrolytic
  capacitor across VMOT/GND**, mounted close to the driver, is mandatory —
  without it, motor-current transients can spike VMOT high enough to damage
  the driver.
- The same 24 V rail feeds a step-down regulator to 5 V for the FireBeetle's
  5V/VCC pin. ESP32-C6 peak draw is well under 1 A, so there is large margin.
- **One common ground.** PSU −, both DRV8825 GND pins (there are usually two —
  one on the power side, one on the logic side), the step-down regulator's
  ground, and the FireBeetle GND must all tie together. If they don't, logic
  signals have no reliable 0 V reference and behavior gets erratic in ways
  that are hard to diagnose.
- At the driver's ~1.2 A/phase current limit (see [Vref](#setting-vref---current-limit)
  below) the **DRV8825 needs its heatsink fitted**, and the enclosure must
  provide airflow/venting around the driver — it runs hot at that current.
- **Never plug or unplug the motor connector while VMOT is powered.** The
  resulting inductive spike is one of the most common ways to kill a
  DRV8825. Power down VMOT (or the whole 24 V rail) first.

## DRV8825 complete pin-by-pin wiring

The DRV8825 is a small breakout with two columns of pins straddling a
central chip. **Read the silkscreen labels on your specific board** — pin
order can vary between clone manufacturers — but the signal names below are
standard and this is the complete list. There is no separate logic-supply
pin: the chip generates its own internal reference from VMOT, so every pin
that "needs 3.3 V" is a *control signal*, not a power feed.

| DRV8825 pin | Wire to | Notes |
|---|---|---|
| `EN̅` (ENABLE) | FireBeetle **GPIO 4** | Active-low enable. Firmware drives it high (disabled) at idle, low only during a move. |
| `M0` | FireBeetle **3V3** | ┐ |
| `M1` | FireBeetle **3V3** | ├ `M2:M1:M0 = 0:1:1` → **1/8 microstep** (not firmware-controlled) |
| `M2` | leave unconnected | ┘ internal pull-down on the board reads this as 0 |
| `RESET̅` | jumper to `SLEEP̅`, then that joined node → **3V3** | **Must be high or the chip is held in reset.** See the callout below — this was last session's root cause. |
| `SLEEP̅` | (tied to `RESET̅`, both → 3V3) | **Must be high or the chip is asleep.** Same callout. |
| `STEP` | FireBeetle **GPIO 2** | Step pulse train |
| `DIR` | FireBeetle **GPIO 3** | Direction level, set before each move |
| `VMOT` | 24 V PSU **+** | Plus the ≥100 µF capacitor to the adjacent GND, right at the pin |
| `GND` (power side, next to VMOT) | 24 V PSU **−** | |
| `A1`, `A2` | Motor coil **A** (one pair) | See coil identification below |
| `B1`, `B2` | Motor coil **B** (the other pair) | |
| `FAULT̅` | leave unconnected | Not used by this firmware |
| `GND` (logic side) | FireBeetle **GND** | Common ground — see power chain note above |

### ⚠️ The pin that caused an hour of "the motor is dead"

**`SLEEP̅` and `RESET̅` both have internal pull-*downs* on the DRV8825.** Left
unconnected, they sit at 0 V — which means the chip is **both asleep and
held in reset simultaneously**. In that state the driver is completely
inert: no current to the coils, no response to STEP/DIR, nothing. It looks
exactly like a dead board, a bad motor, or a wiring fault anywhere else in
the chain — the actual cause is almost always overlooked because "3.3 V
logic pins that need to be *high* to do nothing in particular" isn't where
people expect to look first.

**The fix is two jumper wires:** one between `RESET̅` and `SLEEP̅` (so a
single feed serves both), and one from that joined node to the FireBeetle's
**3V3** pin. After wiring, **verify both pins read ~3.3 V to GND with a
multimeter** before assuming the driver is awake — don't just trust that the
jumper is seated; breadboard rows are a common place for this to silently
fail (see the keypad section below for the same failure mode).

### Coil identification

The 2HS60-1504JA05-020-03 has 4 leads forming 2 coils. Before wiring, find
the pairing with a continuity check (multimeter on the resistance/continuity
setting, motor disconnected from everything):

1. Test resistance between every pair of the 4 leads.
2. Two pairs will show a few ohms (the coil windings); the other four
   combinations will show open/infinite.
3. One low-resistance pair → `A1`/`A2`. The other → `B1`/`B2`. Polarity
   within a pair doesn't matter for correctness — swapping A1↔A2 (or
   B1↔B2) just reverses that coil's field, which either does nothing
   detectable or reverses the motor's direction. If the motor runs the
   "wrong" way after wiring, don't re-wire coils to fix it — use the
   firmware-level `motor_reversed` setting instead (see
   [Direction check](#direction-check)); it's reversible without touching
   the connector.
4. **Wrong pairing** (e.g. one wire from coil A and one from coil B on the
   same terminal pair) is the case to actually avoid — it typically causes
   buzzing, vibration without rotation, or a much weaker/rougher motion than
   correct pairing.

### GPIO summary (`src/main.c`)

All 8 GPIOs used by this project:

| Signal | GPIO | Notes |
|---|---|---|
| `STEP` | GPIO 2 | Pulse train from `motion.c`'s GPTimer ISR |
| `DIR` | GPIO 3 | Level set before each move; meaning flips with `motor_reversed` |
| `EN̅` | GPIO 4 | High = driver **disabled**; firmware drives it low only during moves |
| Keypad Up | GPIO 5 | Internal pull-up |
| Keypad Down | GPIO 6 | Internal pull-up |
| Keypad Fn | GPIO 7 | Internal pull-up |
| External status LED | GPIO 14 | Through a series resistor to the LED, LED to GND |
| Onboard LED (mirror) | GPIO 15 | FireBeetle's onboard LED; `status_led.c` drives it as a plain output alongside GPIO 14. GPIO15 is a strapping pin, but it's only ever driven as an output *after* boot strapping is latched, so sharing it here is fine. |

Keep all of `STEP`/`DIR`/`EN̅`/keypad clear of the ESP32-C6 strapping pins
(GPIO8/9/15), the USB-Serial-JTAG pins, and FireBeetle reserved pins — same
rule as the sibling projects. The one deliberate exception is the onboard
LED mirror on GPIO15 (strapping pin): it is only ever driven as a
boot-after output, never read at reset, so it doesn't disturb strapping.

## Setting Vref (current limit)

Target current: **~1.2 A/phase** (80% of the motor's 1.5 A/phase rating).

For a standard DRV8825 breakout, the current limit is set by the onboard
trimpot per **Ilimit = 2 × Vref**, so:

```
Vref = Ilimit / 2 = 1.2 A / 2 = 0.6 V
```

Procedure — **do this after the driver is confirmed awake** (RESET̅/SLEEP̅ at
3.3 V, checked above), with the **motor still disconnected**:

1. Power the board from the 24 V rail. The trimpot only produces a
   meaningful reading once VMOT is present and the chip is out of sleep/reset
   — if you measure near 0 V here, that's a symptom of the driver being
   asleep, not a low current setting. Go back and check `SLEEP̅`/`RESET̅`
   before touching the pot.
2. Set a multimeter to DC volts.
3. Measure between the **Vref trimpot** (touch the probe to the metal top of
   the screw itself — that's the wiper) and **GND** (a nearby GND pin on the
   breakout, not the motor connector).
4. Gently adjust the trimpot — turning it usually raises the reading — until
   the meter reads **≈0.6 V** (0.55–0.65 V is fine).
5. Only then connect the motor.

Re-check after any driver swap — trimpot position doesn't transfer between
boards. If you ever measure Vref in the millivolt range with everything
wired and powered, treat it as a signal to re-check `SLEEP̅`/`RESET̅` and
`VMOT`, not as "the pot needs turning."

## Keypad wiring

3-key membrane keypad (Up / Down / Fn):

```
Membrane keypad          FireBeetle 2 ESP32-C6
┌──────────────┐        ┌──────────────────┐
│ Common ───────┼────────┤ GND              │
│ Up     ───────┼────────┤ GPIO 5 (pull-up) │
│ Down   ───────┼────────┤ GPIO 6 (pull-up) │
│ Fn     ───────┼────────┤ GPIO 7 (pull-up) │
└──────────────┘        └──────────────────┘
```

Common → GND; each key's line goes to its own GPIO configured with the
internal pull-up, so the pin idles HIGH and reads LOW when pressed. Firmware
debounce (via the `esp-zb-common` `debounce` module) handles switch bounce —
no external resistors needed.

**Identify the common pin before wiring**, don't assume it from the tail's
position — membrane tail pinouts aren't consistent across suppliers, and the
"common" is sometimes an end pin, sometimes not. With a multimeter on
continuity: hold one key down and probe pairs on the tail; the pin that
shows continuity for *every* key (tested one at a time) is the common. That
one goes to GND; the other three go to GPIO 5/6/7.

**Breadboards split rows down the centre channel.** A jumper landed on the
wrong half of a row, or in a neighbouring row entirely, won't show up
visually as "obviously wrong" but will leave that key permanently silent.
If one key never registers a press while the other two work fine, this is
the first thing to check — see [Troubleshooting](#troubleshooting) below.

### Verifying the keypad

Before trusting the wiring, confirm all three keys electrically with the
serial monitor:

```bash
pio run -e dfrobot_firebeetle2_esp32c6_zigbee -t upload -t monitor
```

Press each key in turn and watch for state changes propagating through the
dispatcher (motion starting on a hold, an ACK flash on a tap once
calibrated, etc.) — see `DEVELOPER_GUIDE.md` for the full gesture reference.
If a key produces no reaction at all, don't assume firmware first; the
overwhelming likelihood, based on last session, is wiring — go through the
[Troubleshooting](#troubleshooting) keypad checklist.

## LED wiring

```
GPIO 14 ── resistor (≈330 Ω–1 kΩ) ── LED anode
                                       LED cathode ── GND
```

One external status LED on the enclosure face, driven from GPIO 14 through a
series resistor. The onboard LED mirrors the same pattern (`status_led.c`
drives both together) so the state is visible with the enclosure open during
bench work even before the external LED is wired.

See the design spec §2 for the full LED pattern table (off / 1 Hz / ~5 Hz /
double-flash / ack / error / identify) and [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)
for what each pattern means during calibration.

## Direction check

After first power-up (or after any rewiring), verify motor direction sense
matches the keypad:

1. From standstill, **hold Up** (the device starts uncalibrated, so this is
   a hold-to-jog, not a tap — taps don't move an uncalibrated device).
2. If the blind moves **up** (toward Open), direction is correct — done.
3. If the blind moves **down** instead, the motor/gearing sense is flipped
   for this installation. Fix it either:
   - **remotely**: flip `motor_reversed` in zigbee2mqtt (Mode attribute,
     `motor_reversed` expose), or
   - **locally**: hold **Up + Down together for ~3 s** (the reverse chord).

Either path flips `motor_reversed` in NVS and — because all stored step
counts were measured under the old direction sense — **wipes the current
calibration**. Recalibrate afterward (see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)).

## Motion speed tuning

Cruise speed is a `#define` in `src/main.c` (`CRUISE_US`, microseconds per
step at 1/8 microstep through the 1:15 reduction). Lower = faster and, on
this motor, was found to run *quieter* as well (moves through resonance
bands faster). Current bench value is `CRUISE_US = 100` (~2.4 s per output
revolution, ~60 s full travel for a 2.5 m blind on a 30 mm rod). This has
not yet been verified under real blind load — if the motor stalls or the
blind stops short of its calibrated end (visible as position silently
drifting over repeated cycles, since the driver is open-loop and can't
detect a missed step), back off toward `150`–`300` and re-test. `JOG_CRUISE_US`
and `CAL_TIMEOUT_US` are tuned together with cruise speed — the calibration
timeout must comfortably exceed the time to jog the full span at jog speed,
or a calibration attempt on a long blind can time out mid-session.

If more speed is ever needed beyond what 1/8 microstep supports cleanly,
the next lever is rewiring `M0` from 3V3 to GND for 1/4 microstep (doubles
step rate for the same pulse frequency; louder, and the calibration span in
steps will double too — recalibrate after changing microstep wiring).

## Troubleshooting

Built directly from the first bench session's debugging path — work through
these roughly in order; each stage assumes the previous one is confirmed
good.

### Nothing works at all / device won't boot

- Check the FireBeetle's power LED and confirm `/dev/cu.usbmodem*` (macOS)
  appears when USB is plugged in.
- Confirm 5 V is actually reaching the FireBeetle's 5V pin from the
  step-down regulator (multimeter, 5V pin to GND) if running off the 24 V
  rail rather than USB.
- A rail sagging well below its nominal voltage (e.g. 3.3 V reading ~1.7 V)
  usually means something elsewhere is loading it down — check continuity
  between 3V3 and GND for an accidental short before re-powering.

### One or more keypad keys do nothing

1. **Confirm the physical wiring order.** ▲→GPIO5, ▼→GPIO6, Fn→GPIO7 is the
   firmware's expectation; wires landed in rotated or swapped positions will
   make keys register as the *wrong* key rather than not at all (▲ acting
   like Fn, etc.) — if presses do something but the wrong thing, recheck
   which physical wire lands on which GPIO row.
2. **A key that does nothing at all**, while its neighbours work, is almost
   always a breadboard row issue: wrong row entirely, or the jumper on the
   wrong half of a row relative to the centre channel split. Re-seat the
   jumper in the exact row for that GPIO.
3. **Confirm the common pin.** If every key is silent, the common likely
   isn't actually reaching GND — verify with continuity as described above,
   don't assume from tail position.
4. Watch the serial monitor while pressing — a GPIO level that never
   changes on press confirms a wiring problem (not firmware) immediately.

### Motor doesn't turn (or doesn't even hum/resist)

Work through in this order — this is the exact sequence from the session
that found the fault:

1. **`SLEEP̅` and `RESET̅` at 3.3 V?** Measure both to GND. This was the
   actual root cause last time — a completely inert motor with correct
   STEP/DIR/EN wiring, correct VMOT, and correct Vref procedure, because the
   chip was simply asleep. Check this **first**, before anything else on
   this list.
2. **VMOT at ~24 V?** Measure VMOT to GND. Confirms the 24 V rail is
   actually reaching the driver (PSU on, correct wire, correct breadboard
   row — the same "split rail" failure mode as above can bite here too).
3. **Vref sensible (~0.6 V), not near 0 mV?** If it reads near-zero with
   VMOT confirmed present, that's usually not "the pot is at zero" — it's
   the driver not being awake yet (see #1) or not having VMOT at all
   (see #2). The pot reading is a *symptom* of driver state, not just a
   dial you're forgetting to turn.
4. **Coil pairing correct?** Buzzing, vibration-without-rotation, or a very
   weak/rough motion with everything above confirmed good points at swapped
   coil pairs (A1/A2 mixed with B1/B2) rather than swapped polarity within
   a pair.
5. **EN̅ actually toggling?** With everything above confirmed, watch GPIO4
   with a meter during a commanded move — it should sit high at idle and
   pull low only while moving. If it never moves, that's a firmware/logic
   issue rather than a power-stage one.

### Motor turns but in the wrong direction

Not a wiring fault — see [Direction check](#direction-check) above. Use
`motor_reversed`, don't re-wire the coils.

### USB serial port disappears mid-session

If the FireBeetle drops off `/dev/cu.usbmodem*` unexpectedly with no
firmware change and no cable movement, check the 3V3 rail for sag/short
before assuming the board failed — a browning-out ESP32-C6 can drop USB
enumeration. Re-seating the USB cable and, if that doesn't restore it,
fully power-cycling (24 V off, USB unplugged, then back) is usually enough
if the underlying rail issue is also fixed.

## See also

- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) — pairing, calibration walkthrough,
  bench checklist, OTA release flow.
- [PARTITIONS.md](PARTITIONS.md) — flash partition layout.
- [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md) — full design spec (§2 hardware, §5 Zigbee, §6 calibration).
