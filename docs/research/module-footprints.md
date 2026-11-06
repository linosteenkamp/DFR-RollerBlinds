# Module footprints and keep-outs: XIAO ESP32C6 + BTT TMC2209 V1.3

Research for issue #4 ("What are the XIAO ESP32C6 and TMC2209 V1.3
footprints and keep-outs?"), feeding the KiCad carrier board. Both modules
plug into female headers on the carrier.

Researched 2026-11-06 from vendor primary sources only. Seeed's own KiCad
design files for the XIAO ESP32C6 were the main source: the full
schematic and PCB project (`.kicad_pcb`), not only the library footprint.
Every number below was read from those files or from BTT's own drawings,
unless it is marked **UNVERIFIED** or **SCALED**. SCALED means measured off
a vendor drawing that has no dimension for that feature.

Sources (cited inline as [S1] etc.):

| Tag | Source |
|---|---|
| S1 | Seeed wiki, XIAO ESP32C6 getting started — https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/ |
| S2 | Seeed KiCad project (SCH + PCB + gerbers), `XIAO_ESP32_C6_v1.0_SCH&PCB_260114.zip`, file `XIAO ESP32 C6.kicad_pcb` — https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32C6/XIAO_ESP32_C6_v1.0_SCH&PCB_260114.zip |
| S3 | Seeed XIAO KiCad footprints, `New_XIAO_Series_Footprints.zip` → `XIAO-ESP32-C6-DIP.kicad_mod`, `XIAO-ESP32-C6-SMD.kicad_mod` — https://files.seeedstudio.com/wiki/XIAO-KiCad-Library/New_XIAO_Series_Footprints.zip |
| S4 | Seeed XIAO KiCad symbols, `XIAO_Series_SCH_Symbols.zip` → `Seeed_Studio_XIAO_Series.kicad_sym` (symbol `XIAO-ESP32-C6-SMD`) — https://files.seeedstudio.com/wiki/XIAO-KiCad-Library/XIAO_Series_SCH_Symbols.zip |
| S5 | Seeed OPL KiCad library (same footprint, byte-identical to S3's `XIAO-ESP32-C6-DIP.kicad_mod`, SHA-1 `4ec33d09…`) — https://github.com/Seeed-Studio/OPL_Kicad_Library/tree/master/Seeed%20Studio%20XIAO%20Series%20Library |
| S6 | XIAO ESP32C6 schematic PDF — https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32C6/XIAO_ESP32_C6_v1.0_SCH_260114.pdf |
| B1 | BTT TMC2209 **V1.3** folder, `bigtreetech/BIGTREETECH-Stepper-Motor-Driver` — https://github.com/bigtreetech/BIGTREETECH-Stepper-Motor-Driver/tree/master/TMC2209/V1.3 |
| B2 | V1.3 dimension drawing `Schematic/TMC2209 V1.3-SIZE.pdf` (in B1) |
| B3 | V1.3 schematic `Schematic/TMC2209 V1.3-SCH.pdf` (in B1) |
| B4 | V1.3 pin card `Schematic/TMC2209-V1.3-Pin.jpg` (in B1) |
| B5 | V1.3 manual `manual/BIGTREETECH TMC2209 V1.3 User Manual.pdf` (in B1), v1.00, 2024-01-26 |
| B6 | V1.3 3D model `3D/TMC2209 V1.3.step` (in B1) |
| B7 | BTT TMC2209 **V1.2** repo (manual + schematic) — https://github.com/bigtreetech/BIGTREETECH-TMC2209-V1.2 (`manual/TMC2209-V1.2-manual.pdf`, `Schematic/TMC2209-V1.2.pdf`) |
| B8 | BTT docs, TMC2209 page — https://github.com/bigtreetech/docs/blob/master/docs/TMC2209.md |
| K1 | KiCad std lib footprint `Module.pretty/Pololu_Breakout-16_15.2x20.3mm.kicad_mod` — https://gitlab.com/kicad/libraries/kicad-footprints/-/blob/master/Module.pretty/Pololu_Breakout-16_15.2x20.3mm.kicad_mod |
| K2 | KiCad std lib `Connector_PinSocket_2.54mm.pretty/PinSocket_1x08_P2.54mm_Vertical.kicad_mod` — https://gitlab.com/kicad/libraries/kicad-footprints/-/blob/master/Connector_PinSocket_2.54mm.pretty/PinSocket_1x08_P2.54mm_Vertical.kicad_mod |
| K3 | KiCad std lib symbols `Driver_Motor.kicad_symdir` — https://gitlab.com/kicad/libraries/kicad-symbols/-/tree/master/Driver_Motor.kicad_symdir |
| E1 | Espressif ESP32-C6 Hardware Design Guidelines, PCB layout — https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c6/pcb-layout-design.html |

---

## 1. Seeed XIAO ESP32C6

### Coordinate frame used here

Top view, USB-C at the top, the way HARDWARE.md draws it. Origin is the
**centre of pin 1 (D0)**. +X runs right, toward the 5V/GND/3V3 row. +Y runs
down, away from USB. Bottom-side items are given as seen *through* the
board from the top, so they are not mirrored. All values are in mm and were
converted from Seeed's `.kicad_pcb` [S2], where the module footprint `U2`
has its +x axis pointing at the USB end.

### Pins, pitch, rows, pin 1

| Fact | Value | Source |
|---|---|---|
| Pin pitch | 2.54 | S2, S3 |
| Row spacing (centre–centre) | **15.24** (0.6", DIP-14 wide) | S2, S3 (pads at ±7.62) |
| Pins per row | 7 | S3 |
| Pin 1 | **D0, top-left** (left row, USB end) | S2 pad 1 net `GPIO0`, S4 |
| Numbering | 1–7 = D0…D6 down the left row; 8–14 = D7, D8, D9, D10, 3V3, GND, 5V(VBUS) **up** the right row (8 = D7 bottom-right, 14 = 5V top-right). Counter-clockwise from the top, like a DIP. | S2 pad nets, S4 |
| Pin centre ↔ long edge | 1.27 (board edge X = −1.27 and 16.51) | S2 |
| Pin 1 row ↔ USB-end board edge | 2.92 (edge at Y = −2.92) | S2 |
| Pin 7 row ↔ far-end board edge | 2.79 (edge at Y = 18.03) | S2 |

This matches the GPIO table in HARDWARE.md (D0 top-left … D6 bottom-left;
5V top-right … D7 bottom-right).

### Board outline

| Fact | Value | Source |
|---|---|---|
| PCB outline | **20.955 × 17.78** (wiki rounds it to "21 x 17.8mm") | S2 Edge.Cuts inside `U2`; S1 |
| Corner radius | 1.905 (all four corners) | S2 Edge.Cuts arcs |
| PCB thickness | 1.6 (4-layer: 0.1 prepreg / 1.24 core / 0.1 prepreg) | S2 stackup and `.gbrjob` — design-file value; **measure one** to confirm |
| Mounting holes | none | S2 (only NPTH are USB-C pegs) |
| Castellations | yes; half-holes on every header pad (THT 1.27 at the edge plus a 1.5 THT pad 1.27 inboard) | S2 |
| Two internal oblong slots | 1.27 × 0.51, about 0.6 inboard of pins D5 and D8 | S2 Edge.Cuts. Not relevant to the carrier. |

### USB-C position

| Fact | Value | Source |
|---|---|---|
| Location | Top edge (pin 1 end), centred across the width | S2 `USB1` |
| Body overhang beyond the PCB edge | **≈1.51–1.54** (edge cut from Y = −2.95 to −4.46) | S2 Edge.Cuts `fp_line` in `U2`; S3 silk |
| Body width | 9.0 (X 3.14 … 12.14, centre X = 7.63 = board centre) | S2 |
| Overall length incl. USB | ≈22.5 | S2 |
| Height above PCB | **UNVERIFIED.** Not in any Seeed file I could open. The 3D model is on GrabCAD (login needed, https://grabcad.com/library/seeed-studio-xiao-esp32-c6-1). | — |

The BOOT and RESET buttons (`K1`/`K2`, 2.6 × 1.6 × 0.53 parts) sit either
side of the USB-C, inboard of the edge [S2].

### On-board ceramic antenna and U.FL socket (both top side, far end)

| Item | Part | Centre (X, Y) | Extent | Source |
|---|---|---|---|---|
| Ceramic chip antenna `ANT1` | KH5220-A36, footprint `ANT2P-5.2x2x1.1MM` (5.2 × 2.0 × 1.1 body) | (10.74, 16.88) | X 8.14…13.34, Y 15.9…17.9; long axis along the bottom edge, between board centre and D7 | S2 |
| U.FL socket `ANT2` | Hirose `U.FL-R-SMT-1`, footprint `U-FL3P-SMD-2.6X2.6X1.0MM` | (3.32, 16.63) | ≈2.7 × 2.6 courtyard; bottom-left, about 1.4 from the far edge and 3.3 right of the D0–D6 row | S2 |

So the radio end of the XIAO is the **end opposite the USB-C**. The U.FL
sits on the D0–D6 side and the ceramic antenna on the D7–5V side. The **D7
pad (DIR) is the header pin nearest the ceramic antenna.**

The XIAO's own antenna clearance [S2]: Seeed's GND pours on **all four
layers** (F.Cu, In1, In2, B.Cu) are cut back from a rectangle at the far
end. That rectangle runs X 6.58 → 16.51 (from 1.0 left of the board
centre to the right edge) and Y 14.60 → 18.03 (the last ≈3.4 to the far
edge). It is roughly 9.9 × 3.4 at the antenna corner. The PCB also has an
F.Cu copper-pour keep-out under the U.FL socket. That follows Espressif's
"keep all layers clear under the IPEX antenna connector" [E1].

RF switch [S1]: GPIO3 low enables the switch. GPIO14 low (the default)
selects the ceramic antenna, and GPIO14 high selects the U.FL. Firmware
v2.4.1 drives GPIO14 high, per HARDWARE.md. **The ceramic antenna is still
the radio path before the app runs**, and in any firmware that doesn't
drive the switch, so the carrier should not kill it outright.

### Carrier keep-out recommendations for the XIAO

These are design rules derived from the facts above. They are not vendor
numbers.

1. **No copper (any layer) and no components on the carrier under the
   XIAO's far end.** Take at least Seeed's own void (X 6.6…16.5,
   Y 14.6…18.0) and preferably the full width (X −1.3…16.5) from Y ≈ 14
   to the far edge. That keeps the ceramic antenna's ground relief intact
   and keeps carrier copper away from the U.FL.
2. **Put the XIAO's far end at the carrier edge**, overhanging it if
   possible. Espressif's guidance for module antennas [E1] is to put the
   antenna at the base-board edge, ideally beyond it, and not to place
   the module centrally with a cut-out around it. The same guide asks for
   ≥15 mm clearance from the housing in all directions for a PCB antenna.
   That has no numeric equivalent for this chip antenna, but it is the
   right direction.
3. **Leave a clear path up for the U.FL plug and cable.** It mates
   vertically from above (HARDWARE.md, "press straight down"). Route the
   cable away from the TMC2209, the motor leads and 24 V, which also
   means don't place the TMC2209 at the XIAO's antenna end. The mated
   U.FL plug height is **UNVERIFIED** here: Hirose's site refused
   automated fetches.
4. **Keep STEP/DIR/motor traces out of the antenna-end zone.** D7 (DIR)
   lands right at the antenna corner, so take its trace inboard
   immediately.
5. **USB-C end:** the connector overhangs the XIAO outline by ~1.5. Seeed's
   footprint courtyard [S3] stops at the PCB edge (X −1.27…16.51 frame:
   courtyard Y −2.93…18.03) and does **not** include the USB overhang.
   Extend the courtyard yourself, and keep plug-approach space clear at
   the module's raised height.

### Bottom-side pads: do they matter when socketed?

Bottom (B.Cu) pads on the XIAO [S2, S3 SMD footprint pads 15–24, S4 pin
names]:

| Pad(s) | Net | Centre (X, Y), seen from top | Size |
|---|---|---|---|
| Battery + (`TP9`) | VBAT | (5.84, 13.15) | 2.03 × 1.02, long axis along Y |
| Battery − (`TP10`) | GND | (8.38, 13.15) | 2.03 × 1.02 |
| 2 × 4 test-pad grid (`TP1–TP8`) | 3V3, BOOT, GND, MTMS(GPIO4), MTDI(GPIO5), MTCK(GPIO6), MTDO(GPIO7), EN | columns X 6.35 / 8.89; rows Y ≈ −1.03, 1.51, 4.05, 6.59 (USB half of the board) | Ø1.14 round |

The battery pad positions agree with the wiki: "negative … near D8",
"positive … near D5" [S1].

**When socketed, they don't connect to anything.** The XIAO's underside
sits a full header height above the carrier. They are still **exposed live
copper** (VBAT, 3V3, EN, BOOT, the JTAG/strap pins), so:

- Don't put tall carrier parts under the XIAO that could touch them (bulk
  electrolytics, the 1N5822 standing up, etc.). Measure the actual gap.
  It is female-header height, plus the XIAO's own male-header spacer if
  that sits under the board. **Which header Lino uses is a bench fact.**
- Use the **DIP** footprint (header pads only), not Seeed's **SMD**
  footprint. The SMD one adds carrier pads under all ten bottom pads
  (it's meant for reflowing the XIAO flat onto a board).

### KiCad footprint / symbol for the XIAO

- **Footprint:** Seeed official `XIAO-ESP32-C6-DIP.kicad_mod` [S3], also
  in the OPL repo [S5]. It has the right geometry: 2 × 7 at 2.54, 15.24
  rows, pin 1 = D0. Caveats before using it for a *socket*:
  - Each pin has a THT pad (Ø1.524, **drill 0.889**) *plus* an F.Cu SMD
    castellation pad (2.432 × 1.524 at ±8.455). The footprint is
    `(attr smd)`.
  - Drill 0.889 suits soldering a male header straight in. Female-header
    tails want KiCad's standard **1.0 drill / 1.7 pad** [K2].
  - Its 3D model references point at other XIAO models: the visible one
    is SAMD21, the rest are hidden. They are not a C6 model.
  - **Recommendation:** use two KiCad-standard `PinSocket_1x07_P2.54mm_Vertical`
    [K2 family] at 15.24 spacing. Alternatively, copy Seeed's DIP
    footprint, delete the SMD pads, open the drills to 1.0, set the
    attribute to through-hole, and add Fab/courtyard for the USB overhang
    and the antenna-end keep-out (as a rule area).
- **Symbol:** Seeed's library [S4] has only `XIAO-ESP32-C6-SMD` for the
  C6. It has 24 pins (1–14 header, 15–24 bottom pads). Pins 15–24 would
  dangle against a 14-pad footprint. Either hide/delete them in a local
  copy or draw a 14-pin symbol. The generic `XIAO-Add-On` symbol is also
  in [S4].
- No XIAO footprint exists in the KiCad standard library (checked
  `Module.pretty` [K1's library]).

---

## 2. BIGTREETECH TMC2209 V1.3

### V1.3 material exists, in a different repo from V1.2

BTT's V1.3 files are in `bigtreetech/BIGTREETECH-Stepper-Motor-Driver`
under `TMC2209/V1.3/` [B1]: the SIZE drawing, schematic, pin card, manual
and a STEP model. HARDWARE.md cites only the V1.2 repo [B7].
**No KiCad footprint or symbol from BTT exists, for any version.**

### Coordinate frame used here

Top view (the heatsink/cooling-pad side), trimpot at the top, as in B4
and HARDWARE.md. Origin is the **centre of the EN pin** (top-left). +X runs
right, +Y runs down.

### Outline, pins, rows

| Fact | Value | Source |
|---|---|---|
| PCB outline | **15.24 × 20.32** | B2, B5 (dimensioned) |
| Pin pitch | 2.54 | B2 SCALED (78 px per pin at 30.7 px/mm); same as V1.2 [B7] and K1 |
| Row spacing (centre–centre) | **12.70** (0.5") | B2 SCALED (12.70); K1 Pololu-format footprint uses 12.7 |
| Pin centre ↔ edges | ≈1.27 on all four sides (edge X −1.27…13.97, Y −1.27…19.05) | B2 SCALED, consistent with 15.24 − 12.70 and 20.32 − 17.78 |
| Top-side copper cooling pad (heatsink area) | **8.40 × 12.88** (V1.3); V1.2 was 8.81 × 10.19 [B7], the docs image 8.49 × 10.20 [B8] | B2 dimensioned |
| Where the parts are | TMC2209 QFN, sense resistors, caps, TVS diodes all on the **bottom** (facing the carrier). Top has only the cooling pad and the trimpot. | B5 p.6 bottom photo, B2 |
| PCB thickness | **UNVERIFIED** (not dimensioned) | — |
| Heatsink size / height | **UNVERIFIED.** BTT publishes no heatsink dimension in any version's docs, and the STEP model [B6] has no heatsink. | — |
| Overall height above carrier | **UNVERIFIED**: female header + module header spacer + PCB + heatsink. Measure it. | — |

### 16-pin header (cross-check of HARDWARE.md)

Physical positions (B4 pin card, B2, B5 p.5) with the V1.3 silkscreen and
BTT's V1.3 schematic header numbers (`P1A`, B3):

| Pos (HARDWARE.md #) | (X, Y) | V1.3 silk / card | BTT `P1A` pin | Net (B3) |
|---|---|---|---|---|
| L1 (1) | (0, 0) | EN | 8 | ENN, **20 k pull-up to VIO** (R1) |
| L2 (2) | (0, 2.54) | MS1 | 7 | MS1_AD0 (no board resistor; TMC2209 internal pull-down) |
| L3 (3) | (0, 5.08) | MS2 | 6 | MS2_AD1 (same) |
| L4 (4) | (0, 7.62) | **RX** | 5 | **PDN_UART** (20 k pull-down R6) — factory UART pin |
| L5 (5) | (0, 10.16) | **TX** | 4 | `UART_TX`: **not connected** unless R10 (NC) is bridged to RX |
| L6 (6) | (0, 12.70) | CLK | 3 | CLK (20 k pull-down R4) |
| L7 (7) | (0, 15.24) | STEP | 2 | STEP |
| L8 (8) | (0, 17.78) | DIR | 1 | DIR |
| R8 (9) | (12.7, 17.78) | GND | 16 | GND (logic side) |
| R7 (10) | (12.7, 15.24) | **VIO** | 15 | VCC_IO |
| R6 (11) | (12.7, 12.70) | B2 | 14 | OB2 |
| R5 (12) | (12.7, 10.16) | B1 | 13 | OB1 |
| R4 (13) | (12.7, 7.62) | A1 | 12 | OA1 |
| R3 (14) | (12.7, 5.08) | A2 | 11 | OA2 |
| R2 (15) | (12.7, 2.54) | GND | 10 | GND (power side) |
| R1 (16) | (12.7, 0) | **VS** (card: VM) | 9 | VS |

**The positions in HARDWARE.md's table are all correct.** Every function
is where HARDWARE.md says it is. Three silkscreen labels differ on V1.3
(**Lino: compare with the physical module and the card in the box**):

- `PDN` / `PDN` (V1.2) → **`RX` / `TX`** on V1.3 [B2, B4, B5]. RX (4th
  from top) carries PDN_UART. TX is only a spare footprint: it is
  isolated until R10 is bridged (B5 p.7: "solder the two pads at R10
  together"). On V1.2 the two pads were selected by moving a resistor
  (T1 3-pad jumper, [B7] manual §8.1). Either way, leaving both
  unconnected is fine for STEP/DIR.
- `VDD` / `VCC_IO` (V1.2) → **`VIO`** on V1.3 [B2, B4, B3].
- `VM` → silkscreen **`VS`** on V1.3 (B4 card still prints "VM").

HARDWARE.md's "card labels it VDD" and "silkscreened PDN" read like V1.2.
That is a doc nit for HARDWARE.md (not edited here).

**BTT pin numbers vs HARDWARE.md pin numbers.** BTT's schematic numbers
the header DIR = 1 … EN = 8, VS = 9 … GND = 16 [B3]. HARDWARE.md and the
KiCad Pololu footprint [K1] number EN = 1 … DIR = 8, GND = 9 … VM = 16.
Same physical pins. Pick one numbering in the carrier symbol and say which
in the schematic.

### DIAG / INDEX / VREF (the 3 off-grid pads)

From B3, the extra connector `P1B` has 17 = INDEX, 18 = DIAG, 19 = VREF.
Physically [B2, B4]:

- Two pads in the **top row with EN**, at about **(2.54, 0)** and
  **(5.08, 0)** (B2 SCALED). The pin card labels the (5.08, 0) one
  **DIAG**. **Both have pins fitted** on retail V1.3 (B4 photo shows
  pins in both).
- One more pad at about **(3.8, 1.6)** (B2 SCALED, off-grid). It is an
  unpopulated plated hole in B4's photo.
- BTT does not label which of the other two is INDEX and which is VREF.
  **UNVERIFIED. Lino: buzz them.** VREF reads the trimpot wiper voltage
  against GND, roughly 0.2–2.2 V [B8].
- B5 p.6's bottom photo shows a 2-pin header body under the board at the
  two top-row pads. So the **DIAG pin (and its neighbour) protrude
  downward toward the carrier**, like the main rows. **The carrier must
  provide two holes or a 1×2 socket at (2.54, 0) and (5.08, 0)**,
  unconnected, or Lino clips those pins. Otherwise the module won't seat.
  Confirm on the physical module.
- HARDWARE.md calls them "DIAG/INDEX/VREF broken out separately near the
  top", which agrees. Its note that VREF is the trimpot test point is
  consistent with the schematic: VREF = wiper via R11 0 Ω [B3].

### V1.3 vs V1.2 electrical differences relevant to the carrier

From B3 vs B7 schematics and the manuals:

- **Protection added:** a TVS `D1` SMF28A on VS and `D2` SD05 on VIO, plus
  an R2 1.1 Ω + C9 1 µF snubber on VS. On-module VS decoupling is now
  2 × 10 µF + 2 × 0.1 µF (V1.2 had 1 µF + 2 × 100 nF). The carrier's
  220 µF bulk cap stays mandatory (HARDWARE.md).
- **Chopper mode:** set by resistor **R7** (NC = StealthChop, factory; 20 k
  to VIO = SpreadCycle) [B3 note "R7=20K SpreadCycle R7=NC StealthChop"].
  V1.2 used the `SPRE` 3-pad jumper [B7 §8.3]. HARDWARE.md's "SPRE pad"
  wording is V1.2. The behaviour (StealthChop by default) is the same.
- **VM range in the V1.3 manual: "DC 12V – DC 28V"**, peak "2A" [B5]. The
  V1.2 manual and BTT docs say 4.75–28 V and 2.8 A peak [B7, B8]. 24 V is
  inside both.
- MS1/MS2 = GND/GND → 1/8 step in both manuals [B7 §6, B8]. Note that
  B7's own *schematic* table labels the other MS combinations with the
  old TMC2208 values. It agrees on GND/GND = 1/8, which is what this
  project uses.
- EN has a 20 k pull-up to VIO on board (both versions), so the driver
  is disabled if EN floats.

### KiCad footprint / symbol for the TMC2209

- **Footprint:** none from BTT. The KiCad standard library has
  `Module:Pololu_Breakout-16_15.2x20.3mm` [K1] ("Pololu Breakout 16-pin
  15.2x20.3mm 0.6x0.8"): 2 × 8 at 2.54, rows 12.7 apart, pad 1 (square)
  at (0, 0) top-left, 8 at bottom-left, 9 bottom-right, 16 top-right.
  That maps exactly onto EN = 1 … VM = 16. Caveats:
  - Drill is **0.8**, too tight for female-header tails. Use 1.0 / 1.7
    pads [K2], or two `PinSocket_1x08_P2.54mm_Vertical` 12.7 apart.
  - It has **no DIAG/INDEX/VREF holes.** Add two (unconnected or NPTH)
    at (2.54, 0) and (5.08, 0) relative to pad 1, or a 1×2 socket.
  - Its outline is the plain 15.24 × 20.32 board. Add a courtyard and
    height note for the heatsink, and keep the top accessible for the
    trimpot (Vref adjustment) and the VREF probe.
- **Symbol:** none for this module. KiCad's `Driver_Motor` [K3] has
  `Pololu_Breakout_A4988` / `Pololu_Breakout_DRV8825` (wrong pin
  functions: SLEEP/RESET/MS3) and a bare-chip `TMC2209-LA`. **Draw a
  custom 16-pin symbol.** It is trivial.
- **3D:** BTT's `TMC2209 V1.3.step` [B6] has the PCB, bottom-side parts,
  and a 1×8 + 1×1-type pin header set. It has **no heatsink**, so model
  the heatsink as a block from measured size for enclosure checks.

---

## 3. Unverified — needs a physical module or calipers

1. **TMC2209 heatsink**: footprint and height. Also check it stays inside
   the 8.40 × 12.88 pad and clear of the trimpot. BTT warns the heatsink
   must not touch the pins [B7 §9].
2. **TMC2209 PCB thickness**, and the header spacer/pin lengths as shipped.
3. **TMC2209 DIAG-row pins**: do both point down? Which of (2.54, 0) and
   (3.8, 1.6) is INDEX and which is VREF?
4. **TMC2209 silkscreen labels** on Lino's units (RX/TX/VIO/VS expected
   for genuine V1.3), and whether the pin card in the box matches.
5. **XIAO USB-C height** and the tallest top-side part, for the enclosure.
6. **XIAO PCB thickness** (1.6 per Seeed's design file).
7. **Stack height under each module** with the female headers Lino will
   buy, to check what may sit underneath (vs the XIAO bottom pads and the
   TMC2209 bottom-side parts).
8. **Mated U.FL plug height** and cable bend clearance above the XIAO.
9. TMC2209 pin pitch, row spacing and DIAG-row offsets are **scaled from
   BTT's drawing** (agreeing with the Pololu standard to ±0.05 mm), not
   dimensioned by BTT.
