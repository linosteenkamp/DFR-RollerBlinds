# Research: which 24 V → 5 V buck goes on the carrier board

Ticket: GitHub issue #2, "Which 24→5 V buck goes on the board?"
Researched 2026-11-06 from manufacturer datasheets and vendor product pages.
Every figure below is cited. Prices are DigiKey cut-tape prices on 2026-11-06,
included only to show the rough cost. Check them again when ordering.

## Recommendation

**Put a Diodes Inc. AP64200 (`AP64200SP-13`, SO-8EP) on the board.** Its
absolute maximum on VIN is 42 V DC and 45 V for 400 ms, so a 30 V transient
still leaves 12 V of margin. It supplies 2 A, against the 1 A required. It uses
PFM at light load and draws 25 µA of quiescent current, which is what the idle
budget needs. Its package is a leaded SO-8 with an exposed pad, which PCBWay can
place and a person can inspect or rework by hand. Its layout guidance is written
for top and bottom copper, so it suits a 2-layer board. It is also the only
candidate on the shortlist that DigiKey had in stock, at about $0.80 each in
quantities of 100.

**Keep a Schottky between the buck and the XIAO 5V pin**, as an SMD SS14 (SMA)
in place of the through-hole 1N5822. None of the regulators below removes the
need for it. The reasons are under
[USB backfeed](#usb-backfeed-keep-the-schottky).

Expected idle draw on the 24 V rail is **about 8–10 mA if the Mini560 caused
most of the rise from 8 to 25 mA**. That is the best guess in HARDWARE.md, but
nobody has measured it yet. See [Idle estimate](#idle-estimate).

## Constraints, from the ticket and HARDWARE.md

- 24 V nominal in from a Mean Well LRS. Allow for transients of 30 V or more,
  so the abs-max rating should be at least 36 V, preferably with margin.
- At least 1 A at 5 V. HARDWARE.md says ESP32-C6 peak draw is "well under 1 A".
- Low quiescent current and good light-load efficiency. Idle draw rose from
  ~8 mA (rev 1, Pololu buck) to ~25 mA (rev 2, Mini560 Pro)
  ([HARDWARE.md, Idle power draw](../../HARDWARE.md#idle-power-draw)).
- PCBWay must be able to source and assemble it. PCBWay buys from "authorized
  component distributors including Digi-Key, Mouser Electronics, Arrow
  Electronics, Avnet" and assembles QFN and other leadless packages
  ([PCBWay PCB assembly](https://www.pcbway.com/pcb-assembly.html)).
- The reference layout has to be simple enough for 2 layers.
- The XIAO 5V pin is tied to USB VBUS, and the bench showed USB backfeeding
  through the buck onto VM
  ([HARDWARE.md, Power chain](../../HARDWARE.md#power-chain)).

## Shortlist

| | **AP64200** (Diodes) | LMR36015 (TI) | LMR33620 (TI) | Pololu D24V10F5 (module) |
|---|---|---|---|---|
| Vin, operating | 3.8–40 V | 4.2–60 V | 3.8–36 V | 5.1–36 V |
| Vin, abs max | **42 V DC / 45 V (400 ms)** | **66 V** | **38 V** | 36 V (the max of its operating range; no abs-max given) |
| Iout | 2 A | 1.5 A | 2 A | 1 A typ. max |
| Iq / no-load input | 25 µA non-switching; 1 µA shutdown | 26 µA typ. (18–36), non-switching, PFM variant; 26 µA in regulation at 24 V in, no load | 24 µA typ. (34 max) non-switching; 25 µA in regulation at 12 V in | ~200 µA no-load |
| Light-load mode | PFM, with zero-cross cutoff at 60 mA | PFM on the `A`/`B` variants (`FB` is forced PWM) | Auto PFM/PWM | "power-save mode" at light load |
| Package | SO-8EP | VQFN-HR 12-pin, 3 × 2 mm | HSOIC-8 (DDA) or VQFN-HR 3 × 2 mm | 18 × 13 mm board, 5-pin 0.1" header |
| Compensation | **External** (RC on COMP) plus an RT resistor | Internal | Internal | n/a |
| External parts for 5 V | L 10 µH; Cin 2 × 10 µF; Cout 2 × 22 µF; Cboot 100 nF; FB 52.3 k / 10 k; COMP 14 k + 5.6 nF; RT 200 k (≈500 kHz) | L 15 µH; Cin 4.7 µF + 2 × 220 nF; Cout 3 × 22 µF (2 × 22 µF min); FB 100 k / 24.9 k; Cff 20 pF; plus Cboot, Cvcc (400 kHz) | L 10 µH; Cin 10 µF + 220 nF; Cout 4 × 22 µF; FB 100 k / 24.9 k; Cboot 100 nF; Cvcc 1 µF (400 kHz) | none (a cap at the input is good practice) |
| Price (DigiKey) | $1.38 ×1, $0.80 ×100. **9,671 in stock** | $3.54 ×1, $2.20 ×100. **0 in stock** (due 16-Nov) | $2.89 ×1, $1.78 ×100. **0 in stock** (due 28-Dec) | $13.95 ×1, $10.86 ×100 (Pololu) |
| Verdict | **Recommended** | Strong alternative if the transient margin matters more than the package | Rejected: 38 V abs max | Fallback if the carrier keeps a module footprint |

Sources:
- AP64200 datasheet DS43573 Rev. 2-2, Dec 2024
  ([diodes.com](https://www.diodes.com/assets/Datasheets/AP64200.pdf)).
  Abs max VIN −0.3 to +42 V DC and +45 V for 400 ms. Recommended VIN 3.8–40 V.
  IQ 25 µA (EN floating, no load, non-switching). ISHDN 1 µA. PFM peak 600 mA,
  zero-cross 60 mA. "Up to 88% efficiency at 5 mA light load". Table 1 BOM at
  500 kHz. Layout note 1 recommends "2oz copper for both the top and bottom
  layers". Diodes' product page lists the same figures
  ([diodes.com/part/AP64200](https://www.diodes.com/part/AP64200)).
- LMR36015 datasheet SNVSB49D
  ([ti.com](https://www.ti.com/lit/ds/symlink/lmr36015.pdf)) and product page
  ([ti.com/product/LMR36015](https://www.ti.com/product/LMR36015)). Abs max
  VIN 66 V. I_Q-nonSW 18/26/36 µA (PFM variant). I_SUPPLY 26 µA at 24 V in,
  3.3 V out, no load. ISD 5 µA. Table 10-1 BOM. Device Comparison table
  (`ARNX` 400 kHz, no FPWM; `FBRNX` FPWM). The layout example assumes an inner
  GND plane.
- LMR33620 datasheet SNVSAW1E
  ([ti.com](https://www.ti.com/lit/ds/symlink/lmr33620.pdf)) and product page
  ([ti.com/product/LMR33620](https://www.ti.com/product/LMR33620)). Abs max
  VIN to PGND 38 V. IQ 24/34 µA. I_SUPPLY 25 µA at 12 V in. Table 9-2 BOM.
  `ADDA` = HSOIC-8 at 400 kHz.
- Pololu D24V10F5
  ([pololu.com/product/2831](https://www.pololu.com/product/2831)). 5.1–36 V,
  1 A, 200 µA no-load, power-save mode, "do **not** have reverse-voltage
  protection", 0.7" × 0.5", $13.95 / $10.86 (×100).
- DigiKey price and stock:
  [AP64200SP-13](https://www.digikey.com/en/products/result?keywords=AP64200SP-13),
  [LMR36015ARNXR](https://www.digikey.com/en/products/result?keywords=LMR36015ARNXR),
  [LMR33620ADDAR](https://www.digikey.com/en/products/result?keywords=LMR33620ADDAR).

### Why the AP64200 over the others

- **Against the LMR36015.** The TI part is the better regulator on paper. It
  has 66 V abs max, internal compensation and fewer passives. But its 2 × 3 mm
  HotRod QFN cannot be inspected or reworked by hand. TI's layout example
  depends on an inner GND plane, which a 2-layer board does not have. It costs
  about 2.7× as much. DigiKey also had none in stock on the day this was
  checked, which is a real risk for a PCBWay turnkey order. Choose it instead if
  you want more than 42 V of headroom, for example if the rail may one day be
  shared with longer motor runs.
- **Against the LMR33620.** Its 38 V abs max is only 2 V above its own 36 V
  operating ceiling, which fails the "≥ 36 V with margin" constraint. It was
  also out of stock.
- **Against the Pololu D24V10F5.** It is easy to fit and idles well (~200 µA
  no-load, power-save mode). But its 36 V ceiling has no margin, it has no
  reverse-voltage protection, and at about $11 it costs more than the whole
  AP64200 circuit. It is a hand-fitted module, not a part PCBWay places. Use it
  only if the board keeps a module footprint so that a module can be swapped
  for debugging.
- **Mini560 Pro.** Excluded. No manufacturer datasheet could be found, so its
  ratings and Iq can't be confirmed from a primary source. It is also the
  leading suspect for the 25 mA idle draw.
- **Pololu D24V22F5 / D36V28F5** (the rev 1 class). The D24V22F5 is rated
  5.3–36 V with ~1 mA no-load
  ([pololu.com/product/2858](https://www.pololu.com/product/2858)). The
  D36V28F5 is rated 5.3–50 V with 2–3 mA no-load
  ([pololu.com/product/3782](https://www.pololu.com/product/3782)). Both cost
  $24–27 and are far larger than needed. Their no-load draw is 40–120× that of
  the AP64200.

### Design notes for the AP64200 circuit

- Use the Table 1 values for 5 V: R1 52.3 k / R2 10 k, L 10 µH, C1 2 × 10 µF,
  C2 2 × 22 µF, C3 100 nF boot, R5 14 k + C5 5.6 nF on COMP, RT 200 k. That is
  about 11 parts around the IC, 2–3 more than the TI parts need because the
  AP64200 is externally compensated.
- Rate Cin at 50 V. Ceramics lose much of their capacitance at DC bias, and the
  rail can see 30 V transients. The 220 µF VM bulk capacitor at the driver
  stays where it is.
- Choose a shielded 10 µH inductor with Isat at or above the high-side peak
  current limit (2.5 / 3.5 / 4.5 A min / typ / max, datasheet). The inductor
  then won't saturate into a short on the 5 V output. The real load is far
  lower.
- Tie EN to VIN, or leave it floating, for auto-start. The EN pin is rated to
  42 V.
- Follow the datasheet's layout order: input caps tight across VIN/GND, the
  inductor close to SW, and FB parts close to FB. Use a solid bottom GND pour
  with vias under the exposed pad.

## USB backfeed: keep the Schottky

**Why the regulator can't fix it.** All four candidates are synchronous bucks
with an integrated high-side MOSFET. With VIN unpowered and 5 V held on the
output by USB, current runs backwards from the output through the inductor and
the high-side switch's body diode into VIN. That is the path the bench saw:
4.47 V on VM from USB alone
([HARDWARE.md, Power chain](../../HARDWARE.md#power-chain)). The AP64200 does
stop *sinking* current at light load ("does not sink current from the output
when the output load is light and while the device is in PFM", datasheet §PFM),
but that describes the low-side switch only. It does not block a path through
the high-side body diode. None of the datasheets above claims reverse blocking
from output to input. *(That last point is an inference from the topology and
the bench measurement, not a datasheet statement.)* So the regulator choice does
**not** make the diode unnecessary.

**Schottky vs ideal diode.**

| | Schottky, Vishay SS14 (SMA) | Ideal diode, TI LM66100 (SC-70-6) |
|---|---|---|
| Rating | 40 V, 1 A, VF ≤ 0.50 V at 1 A, IR ≤ 0.2 mA at 40 V / 25 °C ([Vishay SS12–SS16](https://www.vishay.com/docs/88746/ss12.pdf)) | 1.5–5.5 V, 1.5 A, 79 mΩ at 5 V, IQ 150 nA, abs max 6 V on VIN/VOUT ([TI LM66100](https://www.ti.com/lit/ds/symlink/lm66100.pdf), [product page](https://www.ti.com/product/LM66100)) |
| Drop at ~35 mA | ~0.3 V (HARDWARE.md measured ~0.3–0.4 V with the 1N5822) | ~3 mV |
| Blocking | Passive, always | Needs CE tied to VOUT (always-on RCB mode). Its comparator switches it off when VOUT rises above VIN by VOFF (datasheet §8.3.2) |
| Cost / stock | Pennies, widely stocked | $0.19 ×100. DigiKey: 0 in stock, due 21-Dec ([DigiKey](https://www.digikey.com/en/products/result?keywords=LM66100DCKR)) |

**Recommendation: SS14 (or any 40 V 1 A SMA Schottky).** The 0.3 V drop
already works: the XIAO's 3.3 V regulator is fine with ~4.6 V on its 5V pin
(HARDWARE.md). At ~35 mA, 0.3 V costs ~10 mW, about 0.4 mA on the 24 V rail.

The Schottky has one useful side effect that an ideal diode loses. Because the
XIAO 5V pin is tied straight to VBUS, any 5 V the buck puts on that pin is also
put on the USB host's VBUS when a cable is plugged in. With the Schottky, the
pin sits at ~4.6–4.7 V, below a normal host's VBUS, so the PC supplies the XIAO
and the buck stays out of the way. With an LM66100 the pin would sit at about
5.0 V, level with VBUS, and the buck would share load with (and slightly drive)
the host. *(This is reasoning from the circuit, not from a datasheet.)* Don't
"fix" the drop by trimming the buck up to 5.3 V for the same reason.

## Idle estimate

What is known: rev 2 measured ~25 mA (0.6 W) on the 24 V rail with the Mini560,
and rev 1 measured ~8 mA (0.19 W) with a Pololu buck. Rev 1 also had a
different MCU board and driver, so the rise is not yet attributed (HARDWARE.md).

The AP64200's own overhead is negligible: 25 µA × 24 V ≈ 0.6 mW when not
switching. In PFM, conversion loss dominates. Diodes claims up to 88% at a 5 mA
load, and the operating point here is a few tens of mA at 5 V.

- If the Mini560 caused most of the rise (HARDWARE.md's best guess), the
  downstream load is close to rev 1's ~0.17 W. At 80–88% efficiency that gives
  ≈ 0.17 W / 0.85 / 24 V ≈ **8 mA**, plus ~0.4 mA for the Schottky drop, so
  **~8–10 mA**.
- If the TMC2209 standing current or the XIAO caused it, the buck can't recover
  it. The board would idle at ~20–24 mA, still about 1 mA better than today.

This is an estimate, not a measurement. The three-step attribution in
HARDWARE.md (buck alone, then buck + XIAO, then everything) settles it. On the
carrier board, a 0 Ω link or jumper in the buck → XIAO path and another in the
24 V → VM path would let that measurement be made with a meter.

## Open questions for Lino

1. **Package vs margin.** Choose between the AP64200 (SO-8EP, 42 V, external
   compensation, in stock) and the LMR36015 (QFN, 66 V, internal compensation,
   out of stock today). 42 V should be enough for an LRS rail that sees 30 V
   transients.
2. **Module fallback footprint.** Should the board also carry a 0.1" footprint
   for a Pololu D24V10F5 as a debug option, or commit fully to the on-board
   buck?
3. **Attribute the idle draw before freezing the board?** Running the three
   measurements from HARDWARE.md on bench3 would show whether the regulator
   change is worth the ~15 mA it is expected to save.
4. **HARDWARE.md describes the rev 1 Pololu two ways.** The BOM says "5 V/2.5 A
   (VIN 6–38 V)". The power-chain diagram says "5 V / 3.2 A, VIN 5.3–50 V",
   which matches the D36V28F5. Which part was it? Rev 1's idle draw is the
   baseline used in the estimate above.
