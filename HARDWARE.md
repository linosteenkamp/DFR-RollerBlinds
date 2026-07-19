# Hardware & Wiring — DFR-RollerBlinds

## Bill of materials

| Part | Role |
|---|---|
| DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`) | Controller (fallback: Seeed XIAO ESP32-C6 if it doesn't fit the enclosure — the design keeps the GPIO budget to 7 pins so a fallback is a pin remap, not a redesign) |
| 2HS60-1504JA05-020-03 bipolar stepper | Drive motor — 1.8°/step (200 full steps/rev), 1.5 A/phase class |
| DRV8825 breakout | Stepper driver, 1/8 microstep (hard-wired) |
| 3D-printed geartrain (`Blinds 2.step`) | 1:15 reduction to the roller tube |
| Mean Well 24 V DC PSU — **recommend LRS-50-24** (LRS-35-24 acceptable) | System supply |
| Step-down regulator 5 V / 3.2 A (VIN 5.3–50 V) | 24 V → 5 V for the FireBeetle |
| Membrane keypad: 2 arrow keys + function key | Local controls + calibration UX |
| External status LED (enclosure face) | State annunciator; onboard LED mirrors it |

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
- At the driver's ~1.2 A/phase current limit (see [Vref](#setting-vref---current-limit)
  below) the **DRV8825 needs its heatsink fitted**, and the enclosure must
  provide airflow/venting around the driver — it runs hot at that current.

## DRV8825 wiring

All 7 GPIOs used by this project (`src/main.c`):

| Signal | GPIO | Notes |
|---|---|---|
| `STEP` | GPIO 2 | Pulse train from `motion.c`'s GPTimer ISR |
| `DIR` | GPIO 3 | Level set before each move; meaning flips with `motor_reversed` |
| `EN̅` | GPIO 4 | High = driver **disabled**; firmware drives it low only during moves |
| Keypad Up | GPIO 5 | Internal pull-up |
| Keypad Down | GPIO 6 | Internal pull-up |
| Keypad Fn | GPIO 7 | Internal pull-up |
| External status LED | GPIO 14 | Through a series resistor to the LED, LED to GND |

`M0`, `M1`, `M2` are **hard-wired for 1/8 microstep** (not firmware-controlled
— this saves 3 GPIOs and is bench-retunable only by rewiring the jumpers).
At 1/8 microstep through the 1:15 output reduction this is 1600
microsteps/motor-rev, 24 000 microsteps/output-rev.

`SLEEP̅` is **tied high** (to `RESET̅`) — the driver is never put to sleep;
idle power-down is done purely via `EN̅` (driver disabled between moves, per
the idle-behavior decision in the design spec §2).

Keep all of `STEP`/`DIR`/`EN̅`/keypad/LED clear of the ESP32-C6 strapping pins
(GPIO8/9/15), the USB-Serial-JTAG pins, and FireBeetle reserved pins — same
rule as the sibling projects.

## Setting Vref (current limit)

Target current: **~1.2 A/phase** (80% of the motor's 1.5 A/phase rating).

For a standard DRV8825 breakout, the current limit is set by the onboard
trimpot per **Ilimit = 2 × Vref**, so:

```
Vref = Ilimit / 2 = 1.2 A / 2 = 0.6 V
```

Procedure:

1. Power the board from the 24 V rail with the **motor disconnected**.
2. Set a multimeter to DC volts.
3. Measure between the **Vref trimpot wiper** and **GND** (a nearby GND pin
   on the breakout, not the motor connector).
4. Adjust the trimpot until the meter reads **≈0.6 V**.
5. Only then connect the motor.

Re-check after any driver swap — trimpot position doesn't transfer between
boards.

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

1. From standstill, **tap Up**.
2. If the blind moves **up** (toward Open), direction is correct — done.
3. If the blind moves **down** instead, the motor/gearing sense is flipped
   for this installation. Fix it either:
   - **remotely**: flip `motor_reversed` in zigbee2mqtt (Mode attribute,
     `motor_reversed` expose), or
   - **locally**: hold **Up + Down together for ~3 s** (the reverse chord).

Either path flips `motor_reversed` in NVS and — because all stored step
counts were measured under the old direction sense — **wipes the current
calibration**. Recalibrate afterward (see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)).

## See also

- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) — pairing, calibration walkthrough,
  bench checklist, OTA release flow.
- [PARTITIONS.md](PARTITIONS.md) — flash partition layout.
- [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md) — full design spec (§2 hardware, §5 Zigbee, §6 calibration).
