# Hardware & Wiring — DFR-RollerBlinds

**Revision 2 hardware** (2026-07-25): Seeed XIAO ESP32C6 controller +
BIGTREETECH TMC2209 V1.3 stepper driver, replacing the original DFRobot
FireBeetle 2 ESP32-C6 + DRV8825 combo. The driver swap is specifically for
TMC2209's StealthChop2 silent-chopping mode, which — per the vendor's own
manual (bigtreetech/BIGTREETECH-TMC2209-V1.2 on GitHub, the module this
board shares its design with) — **ships as the factory default**, no
wiring required. See [Setting Vref (current limit)](#setting-vref-current-limit)
below for the one setting that *does* need attention. The original
FireBeetle/DRV8825 bring-up notes (breadboard debugging, the `SLEEP̅`/`RESET̅`
trap) are no longer applicable and live in git history if ever needed.

This is the complete wiring reference for the current hardware, cross-checked
against BTT's own TMC2209 pin-designation manual (their V1.2/V1.3 modules
share the same 16-pin header layout). Numbers not yet confirmed on the bench
(motion speed under real load, whether the XIAO's D-number → GPIO mapping
matches your specific board) are flagged explicitly below — don't treat them
as settled until measured.

## Bill of materials

| Part | Role |
|---|---|
| Seeed XIAO ESP32C6 (`seeed_xiao_esp32c6`) | Controller. Only 11 GPIOs are broken out (D0–D10) — the design keeps the GPIO budget to 7 used + 1 reserved (see [GPIO summary](#gpio-summary-srcmainc)), leaving 3 spare. |
| 2HS60-1504JA05-020-03 bipolar stepper | Drive motor for the **larger** blinds — 1.8°/step (200 full steps/rev), 1.5 A/phase class, 60 mm body, 4-wire (2 coils) |
| 17HS4401 bipolar stepper | Drive motor for the **smaller** blinds — 1.8°/step, 1.7 A/phase class, 40 mm body, 4-wire. Lower torque than the 2HS60 (roughly half to two-thirds), and takes a **different Vref** — see [Setting Vref](#setting-vref-current-limit). Its lower torque is what sets the fleet-wide `CRUISE_US` ceiling; see [Motion speed tuning](#motion-speed-tuning). |
| BIGTREETECH TMC2209 V1.3 breakout | Stepper driver, StealthChop2 silent chopping — ships factory-default (no wiring needed for it). 1/8 microstep via `MS1`/`MS2` pin-strapping (same resolution as before — preserves all step-count math). Needs its own logic-supply pin (`VCC_IO`), which the DRV8825 never had — see the pin table below. |
| 3D-printed geartrain (`Blinds 2.step`) | 1:15 reduction to the roller tube |
| Mean Well 24 V DC PSU — **recommend LRS-50-24** (LRS-35-24 acceptable) for a single unit | System supply. See [Multi-unit installations](#multi-unit-installations-shared-psu) for sharing one PSU across several controllers. |
| Mean Well **LRS-150-24** (24 V, 6.5 A, ~156 W) — *optional, in place of the LRS-50-24 above* | Single shared supply for **3 blind controllers** on one PSU instead of one PSU per unit. See [Multi-unit installations](#multi-unit-installations-shared-psu) for sizing rationale and distribution wiring. |
| Step-down regulator 5 V / 3.2 A (VIN 5.3–50 V) | 24 V → 5 V for the XIAO |
| Membrane keypad: 2 arrow keys + function key | Local controls + calibration UX |
| External status LED (enclosure face) | State annunciator. This revision drops the onboard-LED mirror entirely — the external LED is the only status indicator (frees a GPIO on the XIAO's smaller header; see [GPIO summary](#gpio-summary-srcmainc)). |
| Multimeter | Required — for Vref, VMOT, and 3V3-rail checks below. Don't skip these. |

## Recommended bring-up order

Wiring everything at once and then debugging blind turned a routine bring-up
into a long back-and-forth last time. Do it in this order instead — each
stage is independently testable before moving to the next:

1. **XIAO alone.** Flash firmware, confirm serial boot log (see
   `DEVELOPER_GUIDE.md`). No other hardware connected yet.
2. **Keypad.** Wire and verify all three keys (procedure below) before
   touching the motor driver.
3. **TMC2209 logic pins only** (no motor, no VM yet). Wire `STEP`/`DIR`/`EN̅`,
   the mode-select pins (`MS1`/`MS2`, both to GND per the table below), and
   **`VCC_IO` to XIAO 3V3** — don't skip this one, the driver's digital core
   won't run without it. Verify `EN̅` toggles per firmware.
4. **TMC2209 power + Vref**, motor still disconnected. Set current limit
   (new formula below — **do not reuse the old DRV8825 Vref value**).
5. **Motor.** Connect coils last, VM already at the correct voltage.
6. **Full power chain** (24 V PSU + step-down to XIAO) together.

## Power chain

```
24 V PSU (LRS-50-24) ──┬─── TMC2209 VM  (+ ≥100 µF electrolytic across VM/GND,
                        │                  close to the driver — non-negotiable
                        │                  spike protection)
                        │
                        └─── Step-down regulator (5 V / 3.2 A, VIN 5.3–50 V)
                                     │
                                     └─── XIAO ESP32C6 5V pin
```

- 24 V feeds the TMC2209's **VM** pin directly. The **≥100 µF electrolytic
  capacitor across VM/GND**, mounted close to the driver, is mandatory —
  without it, motor-current transients can spike VM high enough to damage
  the driver. (BTT's V1.3 module accepts 4.75–28 V on VM, so 24 V has
  plenty of headroom either direction.)
- The same 24 V rail feeds a step-down regulator to 5 V for the XIAO's 5V
  pin. ESP32-C6 peak draw is well under 1 A, so there is large margin.
- **One common ground.** PSU −, both TMC2209 GND pins (there are usually
  two — one on the power side, one on the logic side), the step-down
  regulator's ground, and the XIAO GND must all tie together. If they
  don't, logic signals have no reliable 0 V reference and behavior gets
  erratic in ways that are hard to diagnose.
- At the driver's ~1.2 A/phase current limit (see
  [Vref](#setting-vref-current-limit) below), fit the TMC2209's heatsink and
  make sure the enclosure provides airflow around the driver. TMC2209 has
  built-in thermal shutdown (unlike the DRV8825, which had none), so a
  thermal problem here means throttling/cutout rather than damage — but
  still worth avoiding, and still worth a bench thermal-soak check (see
  `DEVELOPER_GUIDE.md`).
- **Never plug or unplug the motor connector while VM is powered.** The
  resulting inductive spike is one of the most common ways to kill a
  stepper driver. Power down VM (or the whole 24 V rail) first.

## Multi-unit installations (shared PSU)

Several controllers can share one larger 24 V supply instead of one PSU per
blind. This section covers sizing, distribution wiring, and protection for
that case — everything else in this document (per-unit TMC2209 wiring,
Vref, keypad, LED) is unchanged and applies identically to each unit.

### Sizing the shared PSU

Anchor point: the single-unit recommendation (LRS-50-24, 35–50 W) already
has generous margin for one axis's real draw. Two things dominate that
draw, and both stay small:

- **Idle:** the TMC2209 is disabled (`EN̅` high) between moves, so idle draw
  per axis is near-zero — just ESP32-C6 + driver quiescent current.
- **Moving:** the TMC2209's current limit caps the motor at ~1.2 A/phase,
  but because the driver chops the 24 V bus down to whatever the coil
  actually needs, the **bus-side** current at our modest cruise speed
  (~6 rev/s motor shaft) is meaningfully lower than the coil current — a
  few watts per axis, not the 24 V × 1.2 A a naive calculation would
  suggest.

For **3 units**, a **Mean Well LRS-150-24** (24 V, 6.5 A, ~156 W actual max)
is comfortably sized — even a deliberately pessimistic estimate (all three
moving simultaneously, well above expected per-axis draw) lands nowhere
near the supply's rating. You'd need all three motors **stalled**
simultaneously near their current limit to seriously load this supply, and
there's still headroom even then (Mean Well's LRS series also tolerates
short overload peaks above the rated figure).

**Don't just trust the estimate — measure it.** With one unit already
running, put a multimeter (or better, a clamp meter) on the 24 V feed while
it moves, and again while deliberately stalling the motor against a hard
limit. Thirty seconds of measurement turns "should be fine" into "measured
fine," and gives you the real number if you ever scale beyond 3 units.

### Distribution wiring

Star topology from the shared PSU — each unit gets its own pair of feeder
wires back to the supply, not a daisy-chain from one unit to the next
(daisy-chaining means a downstream unit's current also flows through the
upstream unit's wiring and connectors, which complicates both voltage-drop
and fusing calculations for no benefit):

```
                    ┌─── (fuse) ──── Blind 1 (VM / GND)
Mean Well LRS-150-24 ├─── (fuse) ──── Blind 2 (VM / GND)
   24 V / 6.5 A       └─── (fuse) ──── Blind 3 (VM / GND)
```

- **Per-branch fuse (~2 A fast-blow)** on each branch. With one supply
  feeding several installations, a wiring fault or locked rotor on one
  blind shouldn't be able to brown out or damage the other branches — this
  wasn't a concern in the single-unit design (nothing else to protect) but
  matters once several units share a source.
- Each unit's own **≥100 µF electrolytic across VM/GND at the driver**
  (see [Power chain](#power-chain)) is still required regardless of shared
  supply — that's a per-driver spike-protection requirement, not something
  the shared PSU's own bulk capacitance substitutes for.
- **Logic ground stays local to each unit.** The three controllers are
  independent Zigbee nodes with no wired link between them — only the 24 V
  return and that *same unit's* XIAO/TMC2209 grounds need to be common (per
  [Power chain](#power-chain)). There's no need to run a shared
  logic-ground bus between units.

### Wire gauge (voltage drop)

Sized for **5 m** one-way runs (10 m round trip, since current returns via
GND too) using `Vdrop = I × ρ × (2×L) / A` with `ρ ≈ 0.0175 Ω·mm²/m`
(copper). Typical guidance is to stay under ~3–5 % drop; the driver itself
doesn't care about a few hundred millivolts off 24 V (VM floor is ~4.75 V):

| Conductor | Drop @ 1.5 A (motor's rated current — a safe sizing figure) | Drop @ 2.5 A (pessimistic worst case) |
|---|---|---|
| 0.5 mm² (~AWG 20) | 0.53 V (2.2 %) | 0.88 V (3.6 %) |
| **0.75 mm² (~AWG 18) — recommended** | 0.35 V (1.5 %) | 0.58 V (2.4 %) |
| 1.0 mm² (~AWG 17) | 0.26 V (1.1 %) | 0.44 V (1.8 %) |
| 1.5 mm² (~AWG 16) | 0.18 V (0.7 %) | 0.29 V (1.2 %) |

**0.75 mm² (18 AWG) or thicker** per branch: comfortably under 2.5 % drop
even at the pessimistic current figure, and its ampacity covers the ~2 A
branch fuse with margin. At 5 m, wire gauge is really about mechanical
robustness (thin wire is fragile at connectors) rather than electrical
necessity — recompute the table above if your actual runs are much longer.

## TMC2209 complete pin-by-pin wiring

The TMC2209 V1.3 breakout is a 16-pin module (two 8-pin columns straddling
the chip), plus `DIAG`/`INDEX`/`VREF` broken out separately near the top and
the current-limit trimpot. Pin names below come straight from BTT's own
TMC2209 pin-designation manual (their V1.2 and V1.3 modules share this same
layout) — still **read the silkscreen on your specific board** before
wiring, since clones vary.

| TMC2209 pin | Wire to | Notes |
|---|---|---|
| `EN` (ENABLE) | XIAO **D9 / GPIO20** | Active-low enable, same convention as the DRV8825 this replaces. Firmware drives it high (disabled) at idle, low only during a move. |
| `MS1` | **GND** | ┐ |
| `MS2` | **GND** | ├ `MS2:MS1 = GND:GND` → **8 microsteps (1/8)** — matches the old DRV8825 setting exactly, so `1600 microsteps/motor-rev` / `24 000 microsteps/output-rev` and every derived constant (ramp tuning, `MIN_SPAN_STEPS`, calibration span math) carry over unchanged. |
| `PDN` (×2 adjacent pins, silkscreened `PDN`) | Leave both unconnected | UART pin — factory-bridged to the first of the two positions internally. Not using UART in this revision — plain STEP/DIR standalone mode, same control model as the DRV8825 it replaces. **D10 / GPIO18** is the suggested spare on the XIAO side if a future revision wants UART (register-based current control, StallGuard, etc.) — it sits next to `EN` on D9, so it lands in the same corner of the header as the rest of the driver harness. |
| `CLK` | Leave unconnected | Internal oscillator is used by default; no external clock needed. |
| `STEP` | XIAO **D8 / GPIO19** | Step pulse train |
| `DIR` | XIAO **D7 / GPIO17** | Direction level, set before each move |
| `VM` | 24 V PSU **+** | Plus the ≥100 µF capacitor to the adjacent GND, right at the pin |
| `GND` (power side, next to VM) | 24 V PSU **−** | |
| `A1`, `A2` | Motor coil **A** (one pair) | See coil identification below |
| `B1`, `B2` | Motor coil **B** (the other pair) | |
| `VCC_IO` | **XIAO 3V3** | **This is the pin the DRV8825 never had.** DRV8825 self-derives its logic reference from VMOT; the TMC2209's digital core needs its own 3–5 V logic supply here, or `STEP`/`DIR`/`EN` won't be recognized at all. Tie to the XIAO's 3.3 V rail (not 5V) so the logic threshold matches what the XIAO's GPIOs actually drive. |
| `GND` (logic side, next to `VCC_IO`) | XIAO GND | Common ground — see power chain note above |
| `DIAG`, `INDEX`, `VREF` | `DIAG`/`INDEX` unconnected; `VREF` is the trimpot test point | `DIAG`/`INDEX` are stall-detection / step-position outputs, not used by this firmware (no closed-loop features, spec §11 out of scope). `VREF` is where you measure current limit — see below. |

**No `SPREAD` pin exists on this module's external header.**
StealthChop2 vs SpreadCycle is an internal PCB solder-pad (silkscreened
`SPRE`, on the underside) — not something reachable from the header at all.
Per BTT's manual, **the factory-default bridge selects StealthChop2 ("mute
mode")**, which is exactly what this swap is for. Nothing to wire; just
don't touch that solder pad (re-bridging it selects SpreadCycle — loud).

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

XIAO ESP32C6 exposes only 11 GPIOs on its header (`D0`–`D10`). This
revision uses 7 for the same signals as before, drops the onboard-LED
mirror (XIAO has no easily-probed user LED broken out to a header pin,
and the external LED alone was always the primary indicator), and leaves
4 spare.

Assignments follow the **implementation board's physical layout**: the
three driver signals sit together on `D7`–`D9` at one end of the header,
the keypad's 3-pin harness on `D4`–`D6` at the other, and the LED on `D2`.
That grouping is the reason for the pin choice — each harness lands on
contiguous header pins and solders straight down without jumpers.

| Signal | XIAO pin | GPIO | Notes |
|---|---|---|---|
| `STEP` | D8 | GPIO19 | Pulse train from `motion.c`'s GPTimer ISR |
| `DIR` | D7 | GPIO17 | Level set before each move; meaning flips with `motor_reversed` |
| `EN̅` | D9 | GPIO20 | High = driver **disabled**; firmware drives it low only during moves |
| Keypad Up | D4 | GPIO22 | Internal pull-up (this pin doubles as I²C SDA on XIAO's silkscreen — unused here, plain GPIO input) |
| Keypad Down | D5 | GPIO23 | Internal pull-up (doubles as I²C SCL — unused here) |
| Keypad Fn | D6 | GPIO16 | Internal pull-up |
| External status LED | D2 | GPIO2 | Through a series resistor to the LED, LED to GND. Sole status indicator this revision — no onboard-LED mirror. |
| *(spare)* | D0, D1, D3, D10 | GPIO0, GPIO1, GPIO21, GPIO18 | Unused headroom. `D10` is the suggested pick if `PDN_UART` is ever wired for a future TMC2209 UART upgrade — it neighbours `EN` on D9, keeping the driver harness in one corner. |

**`D6`/`D7` are the ESP32-C6's default UART0 TX/RX pins**, and this design
uses both (Fn button and `DIR`). That is safe *only* because the console
runs on the built-in USB-Serial-JTAG rather than UART0 —
`sdkconfig.defaults` sets `CONFIG_ESP_CONSOLE_UART_DEFAULT=n` /
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, leaving
`CONFIG_ESP_CONSOLE_UART_NUM = -1`. If anyone ever switches the console
back to UART0, the console would drive the Fn line and fight its pull-up.
Don't make that change without moving these two signals first.

None of the ESP32-C6 strapping pins (GPIO4/5/8/9/15) are exposed on the
XIAO's header at all, so there's no strapping-pin caution needed here — a
simplification versus the FireBeetle, where the onboard-LED mirror had to
share a strapping pin deliberately.

**Before wiring, confirm this D-number → GPIO-number mapping against your
specific XIAO board's silkscreen/schematic.** It's sourced from Seeed's
published pin-multiplexing reference, not yet cross-checked against a
physical board in hand.

## Setting Vref (current limit)

**Vref is per-unit and depends on which motor that unit has** — the fleet
uses two (see [BOM](#bom)). Target 80% of the motor's rated phase current.
Also note **the formula and resulting voltage differ from the DRV8825** —
don't reuse the old 0.6 V figure.

BTT's own manual gives the formula directly (0.11 Ω sense resistors, standard
on this module):

```
Irms = 325mV / (Rsense + 20mΩ) × 1/√2 × (Vref / 2.5V)
     = 325mV / 130mΩ × 1/√2 × (Vref / 2.5V)
     ≈ Vref × 0.71        (equivalently: Vref ≈ Irms × 1.41)
```

| Motor | Blinds | Rated | Target (80%) | **Vref** |
|---|---|---|---|---|
| 2HS60-1504JA05-020-03 | larger | 1.5 A/phase | 1.2 A | **1.69 V** |
| 17HS4401 | smaller | 1.7 A/phase | 1.36 A | **1.92 V** |

Setting a 17HS4401 unit to 1.69 V under-drives it (that's only ~71% of its
rating) and costs torque you need — this was hit on the bench 2026-08-02 and
showed up as a stall. Confirm your motor's rating against its own datasheet
rather than trusting the table; 17HS4401 variants exist.

The 17HS4401 is the 42×42×**40 mm** body, versus the 2HS60's 60 mm. Less
thermal mass, so it warms faster even at its own correct current — worth a
touch-check during the thermal soak.

**This board does not ship at that value** — BTT's manual states the
factory-default Vref is 1.2 V ±0.1 V (≈0.9 A), so the trimpot genuinely needs
adjusting, unlike a board that happens to already be close.

Procedure — with the **motor still disconnected**:

1. Power the board from the 24 V rail (VM present) with `EN`/`MS1`/`MS2`/
   `VCC_IO` already wired per the table above — **`VCC_IO` matters here too**:
   without it the chip's digital core isn't running and the Vref reading
   won't behave sensibly.
2. Set a multimeter to DC volts.
3. Measure between the **`VREF` test point** and **GND** (a nearby GND pin
   on the breakout, not the motor connector) — consult your board's
   silkscreen for the exact test-point location.
4. Gently adjust the trimpot until the meter reads **≈1.69 V** (valid range
   for this board is 0.2–2.2 V, so 1.69 V has headroom either side).
5. Only then connect the motor.

Re-check after any driver swap — trimpot position doesn't transfer between
boards.

## Keypad wiring

3-key membrane keypad (Up / Down / Fn):

```
Membrane keypad          XIAO ESP32C6
┌──────────────┐        ┌──────────────────┐
│ Common ───────┼────────┤ GND              │
│ Up     ───────┼────────┤ D4 / GPIO22 (pull-up) │
│ Down   ───────┼────────┤ D5 / GPIO23 (pull-up) │
│ Fn     ───────┼────────┤ D6 / GPIO16 (pull-up) │
└──────────────┘        └──────────────────┘
```

Common → GND; each key's line goes to its own GPIO configured with the
internal pull-up, so the pin idles HIGH and reads LOW when pressed. Firmware
debounce (via the `esp-zb-common` `debounce` module) handles switch bounce
— no external resistors needed.

**Identify the common pin before wiring**, don't assume it from the tail's
position — membrane tail pinouts aren't consistent across suppliers, and the
"common" is sometimes an end pin, sometimes not. With a multimeter on
continuity: hold one key down and probe pairs on the tail; the pin that
shows continuity for *every* key (tested one at a time) is the common. That
one goes to GND; the other three go to D4/D5/D6.

**Breadboards split rows down the centre channel.** A jumper landed on the
wrong half of a row, or in a neighbouring row entirely, won't show up
visually as "obviously wrong" but will leave that key permanently silent.
If one key never registers a press while the other two work fine, this is
the first thing to check — see [Troubleshooting](#troubleshooting) below.

### Verifying the keypad

Before trusting the wiring, confirm all three keys electrically with the
serial monitor:

```bash
pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor
```

Press each key in turn and watch for state changes propagating through the
dispatcher (motion starting on a hold, an ACK flash on a tap once
calibrated, etc.) — see `DEVELOPER_GUIDE.md` for the full gesture reference.
If a key produces no reaction at all, don't assume firmware first; the
overwhelming likelihood, based on the first bring-up session, is wiring —
go through the [Troubleshooting](#troubleshooting) keypad checklist.

## LED wiring

```
GPIO 2 (D2) ── resistor (150 Ω) ── LED anode
                                     LED cathode ── GND
```

One external status LED on the enclosure face — **Kingbright L-7104SURC-E**,
3mm through-hole, Hyper Red (AlGaInP) — driven from D2/GPIO2 through a
series resistor. **This revision has no onboard-LED mirror** — the external
LED is the only status indicator, so it needs to be wired and visible before
doing any bring-up beyond stage 1 (flash + serial log only).

Per the datasheet (Kingbright DSAB9908, Rev V.19A): `V_f` = 1.9V typ / 2.5V
max at 20mA (±0.1V tolerance), **30mA absolute max DC current** (a "200mA"
figure that shows up on some retailer listings is a *pulsed* rating — 1/10
duty cycle, 0.1ms pulses — not continuous, and doesn't apply to a steady
status LED), and a genuinely bright 3100mcd typical at 20mA.

**Resistor: 150 Ω.** Working through the full `V_f` tolerance band
(`R = (3.3V − V_f) / I_f`) rather than a single nominal value: worst-case
low `V_f` (1.8V) draws 10.0mA, typical (1.9V) draws 9.3mA, worst-case high
`V_f` (2.6V) draws 4.7mA — every case stays 3–6× under the 30mA rating, and
since this LED's luminous intensity is roughly linear with current, even the
4.7mA low end is still visibly bright (~700mcd) given how efficient this
part is at 20mA. If the enclosure puts the LED 2–4m from the board, that
run's added resistance (24 AWG, ~0.0842 Ω/m, round-trip since GND returns
over the same distance) is 0.34–0.67 Ω — under 0.5% of 150 Ω, so it doesn't
change the value. For a run that long, **put the resistor at the LED end,
not the XIAO end**: it limits current identically either way, but if the
two wires ever short together along the run, a resistor at the LED still
protects the GPIO pin, whereas one back at the board would be bypassed by
the short.

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
step at 1/8 microstep through the 1:15 reduction). The DRV8825-era finding
that "lower `CRUISE_US` also ran quieter" was specific to that driver's
SpreadCycle-only chopping (moving through resonance bands faster reduced
audible buzz); StealthChop2 changes the noise character altogether.

**Bench result 2026-08-02:** at the inherited `CRUISE_US = 100`
(~2.4 s/output-rev), the TMC2209 runs **quiet and smooth free-running**, motor
uncoupled, Vref 1.69 V. No re-tune needed for noise. That also confirms the
`SPRE` pad is at its factory StealthChop2 bridge and the A/B coil pairing is
correct.

**Torque under load is a separate question, and it turned on current, not
speed.** Coupled to the 1:15 reduction and a real blind, a tap at
`CRUISE_US = 100` started and then vibrated in place — a stall. Hold-to-jog
kept working throughout, which was the tell: jog uses `JOG_CRUISE_US = 300`, so
the only difference was cruise speed.

The actual cause was Vref: the bench unit had a **17HS4401 still set to the
2HS60's 1.69 V**, i.e. driven at ~71% of its rating. With Vref corrected to
1.92 V, both 200 µs and **150 µs run clean full travel in both directions**
(bench 2026-08-02, small blind). `CRUISE_US = 150` is the tuned value.

Because 100 µs was only ever tested under-driven, the true stall point at
correct current is unmeasured — it may well be below 150. That makes 150 at
least the ~1.5× margin the rule of thumb wants, and possibly more. **Check Vref
against the fitted motor before ever concluding a stall means "too fast".**

**A stall silently desyncs position.** Step counting is open-loop, so any
vibrate-in-place event means the stored position no longer matches reality —
re-home before trusting it. If a move ends somewhere unexpected, suspect lost
steps before suspecting the position logic.

### `CRUISE_US` is fleet-wide but the motors are not

The two motors have materially different torque (see [BOM](#bom)), and
`CRUISE_US` is a single compile-time constant while Vref is a per-board
trimpot. The 17HS4401 on the smaller blinds is the weaker motor, so **it sets
the ceiling** — a value tuned on a 2HS60 unit would stall the small ones.

Note the loads differ too: the weaker motor drives the lighter blinds, so the
torque-to-load ratio may come out closer than the raw motor specs suggest.
Until both are measured, that's an open question, not an assumption to build
on. Get a tuned figure for each, then decide between:

- tune for the worst case and ship one firmware (simplest; costs speed on the
  stronger units, and is the right default if the two figures land close),
- a per-variant build flag (splits the OTA image identity, which currently
  assumes one image type `0x0003` — real added complexity), or
- an NVS-backed runtime setting exposed through z2m (one firmware, one OTA
  image, per-unit tuning; most work).

Don't add the configuration machinery before the second measurement exists.

**Before blaming speed, check Vref matches the fitted motor.** The bench stall
above was measured with a 17HS4401 still set to the 2HS60's 1.69 V — i.e.
under-driven to ~71% of rating. Correct current first, then tune speed.

**The DRV8825-era "next lever" (rewire `M0` for 1/4 microstep to go
faster) doesn't have a direct equivalent here.** The TMC2209's pin-only
`MS1`/`MS2` table only offers 1/8, 1/16, 1/32, or 1/64 — 1/8 (what we're
using) is already the *coarsest* resolution available without UART, so
there's no pin-strap escalation path to a faster (coarser) microstep. If
more speed is needed beyond what `CRUISE_US` tuning gives at 1/8, the
options are (a) push `CRUISE_US` lower — the TMC2209's STEP timing headroom
is well beyond anything used so far — or (b) move to UART mode (the
spare `PDN_UART`/GPIO D10 pin) for full register control, which is new
scope beyond this hardware swap.

## Troubleshooting

### Nothing works at all / device won't boot

- Check the XIAO's power LED and confirm the USB serial port (macOS:
  `/dev/cu.usbmodem*`) appears when USB is plugged in.
- Confirm 5 V is actually reaching the XIAO's 5V pin from the step-down
  regulator (multimeter, 5V pin to GND) if running off the 24 V rail rather
  than USB.
- A rail sagging well below its nominal voltage (e.g. 3.3 V reading ~1.7 V)
  usually means something elsewhere is loading it down — check continuity
  between 3V3 and GND for an accidental short before re-powering.

### One or more keypad keys do nothing

1. **Confirm the physical wiring order.** ▲→D4, ▼→D5, Fn→D6 is the
   firmware's expectation; wires landed in rotated or swapped positions will
   make keys register as the *wrong* key rather than not at all (▲ acting
   like Fn, etc.) — if presses do something but the wrong thing, recheck
   which physical wire lands on which pin.
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

1. **`VCC_IO` actually wired to 3V3?** This is the TMC2209's equivalent of
   the DRV8825's old `SLEEP̅`/`RESET̅` trap — a completely inert driver with
   otherwise-correct `STEP`/`DIR`/`EN`/`VM` wiring, because the digital core
   has no logic supply and simply isn't running. Easy to skip since the
   DRV8825 never needed this pin at all. Check this **first**.
2. **`EN` actually toggling?** With everything wired, watch the `EN` pin
   with a meter during a commanded move — it should sit high at idle and
   pull low only while moving. If it never moves, that's a firmware/logic
   issue rather than a power-stage one.
3. **VM at ~24 V?** Measure VM to GND. Confirms the 24 V rail is actually
   reaching the driver (PSU on, correct wire, correct breadboard row).
4. **Vref sensible (~1.69 V), not near 0 mV?** If it reads near-zero with
   VM and `VCC_IO` both confirmed present, check `EN` again before assuming
   the pot needs turning.
5. **Coil pairing correct?** Buzzing, vibration-without-rotation, or a very
   weak/rough motion with everything above confirmed good points at swapped
   coil pairs (A1/A2 mixed with B1/B2) rather than swapped polarity within
   a pair.

### Motor turns, but noticeably louder/buzzier than expected

This is new to the TMC2209 swap — the DRV8825 had no quiet mode to fail
into. StealthChop2 ships as this module's factory default (no wiring
required), so if it's not actually quiet:

1. **Check nobody re-bridged the `SPRE` solder pad** on the underside of
   the module. There's no external header pin for this — it's a PCB-level
   solder jumper, factory-set to StealthChop2. Re-bridging it selects
   SpreadCycle (the same loud chopping style as the old DRV8825). Compare
   against the board's silkscreen/manual before assuming it's been touched.
2. **Confirm `VCC_IO` is actually connected.** An unpowered digital core can
   produce erratic/undefined chopper behavior rather than a clean failure.
3. **Confirm `MS1`/`MS2` are both at GND**, not floating — floating
   mode-select pins can read as an unintended combination.

### Motor turns but in the wrong direction

Not a wiring fault — see [Direction check](#direction-check) above. Use
`motor_reversed`, don't re-wire the coils.

### USB serial port disappears mid-session

If the XIAO drops off its USB serial port unexpectedly with no firmware
change and no cable movement, check the 3V3 rail for sag/short before
assuming the board failed — a browning-out ESP32-C6 can drop USB
enumeration. Re-seating the USB cable and, if that doesn't restore it,
fully power-cycling (24 V off, USB unplugged, then back) is usually enough
if the underlying rail issue is also fixed.

## See also

- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) — pairing, calibration walkthrough,
  bench checklist, OTA release flow.
- [PARTITIONS.md](PARTITIONS.md) — flash partition layout.
- [docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md) — full design spec (§2 hardware, §5 Zigbee, §6 calibration).
