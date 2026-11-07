# Carrier board (rev A)

KiCad 10 project for the roller-blind controller's carrier board: the XIAO
ESP32C6 and BTT TMC2209 V1.3 plug into sockets, 24 V DC comes in on a screw
terminal, and an on-board AP64200 makes 5 V. The v2.4.x pin map is unchanged,
so the board needs no firmware release.

Planned and tracked on the wayfinder map
[PCBWay carrier board for the roller-blind controller](https://github.com/linosteenkamp/DFR-RollerBlinds/issues/1).
The part choices are justified in `docs/research/` (`buck-regulator.md`,
`input-protection.md`, `module-footprints.md`, `pcbway-kicad.md`).

| File | What it is |
|---|---|
| `carrier-board.kicad_sch` | The schematic: the source of truth. Edit it in the KiCad GUI. |
| `carrier-board.kicad_pcb` | The layout: 80 × 50 mm, 2 layers, routed. The source of truth for the board. |
| `carrier-board.kicad_dru` | PCBWay's published KiCad DRC rules (2-layer, 1 oz set), unchanged |
| `carrier-board.kicad_pro` | Project settings, including the net classes (see below) |
| `carrier.kicad_sym` (+ `sym-lib-table`) | Project symbols: the module sockets, AP64200, a DPAK-numbered P-FET, and the DNP module placeholders |
| `carrier.pretty/` (+ `fp-lib-table`) | Project footprints: tight-courtyard sockets for the TMC2209 (its DIAG row sits 2.54 mm from EN), plus no-silk copies of the test pad, L1 and J6 |
| `carrier-board-schematic.pdf` | Schematic plot for review |
| `fab/` | The PCBWay upload package and `export.sh`, which rebuilds it (see below) |
| `renders/` | `top.png`/`bottom.png` (3D) and `copper-t.png`/`copper-b.png` (tracks, without the pours), for review |

## Checks

```bash
kicad-cli sch erc --severity-all --exit-code-violations carrier-board.kicad_sch
kicad-cli pcb drc --severity-all --schematic-parity --exit-code-violations carrier-board.kicad_pcb
```

Rev A passes both at every severity. DRC finds 0 violations, 0 unconnected
items and 0 schematic-parity issues. The schematic's netlist was also checked
net by net against the intended connections: 37 named nets, plus 3
intentional no-connects (TMC2209 TX, CLK and VREF).

## How the schematic is drawn

Every connection goes through a **net label** on a short wire stub at the
pin. There are no long wires. So to find everything on a net, search for its
label.

The modules are not parts on this board. `U1` (XIAO) and `U2` (TMC2209) are
pinless DNP placeholders. The sockets they plug into, `J2`/`J3` (XIAO) and
`J4`/`J5`/`J6` (TMC2209), are real parts that get assembled. This is the
approach PCBWay's assembly guidance calls for (see `docs/research/pcbway-kicad.md`).

## How the layout was made

Placement follows the map's placement decision. The 24 V terminal and the
motor connector sit at the left end, with the TMC2209 beside them and turned
180° so its power column faces them. The buck is in the middle. The XIAO sits
at the right end with its USB-C at the top edge and its antenna end inboard,
over a copper-free keep-out. The keypad connector is at the right edge, and
the four corners have M3 holes. Reference designators are on the Fab
(assembly) layer, so the silkscreen carries only function labels.

It was routed by Freerouting 2.5.0, which needs **Java 25**:
`brew install openjdk@25`, then `/opt/homebrew/opt/openjdk@25/bin/java -jar …`.
The KiCad plugin's Java 21 can't run it.

**GND is routed as real traces**, not only through the pours. The two GND
pours were added after routing, then stitched together with vias: a grid of
them, one beside every SMD GND pad, and six under the AP64200's exposed pad.
The order matters. A board exported with its pours turns GND into a "plane",
which Freerouting doesn't route, and the top pour then breaks into islands
under the buck. Re-routing from scratch has to follow the same order: remove
the pours, export the DSN, route, import the SES, then re-add the pours and
the stitching.

Net classes and their track widths:

| Class | Nets | Width |
|---|---|---|
| Power | 24V_IN, 24V_F, +24V, VM | 1.0 mm |
| Motor | MOT_A1/A2/B1/B2 | 0.8 mm |
| Supply | +5V_BUCK, 5V_PRE, +5V, SW, +3V3 | 0.5 mm |
| Ground | GND | 0.5 mm, plus the pours |
| Default | signals | 0.25 mm |

Freerouting narrows a few segments where they meet fine-pitch pads: 7 Power
segments down to 0.6 mm, and some signal segments down to 0.19 mm. All are
above PCBWay's 0.127 mm minimum.

## The PCBWay package

`fab/export.sh` rebuilds everything in `fab/` from the KiCad files. It fails
unless ERC and DRC (with parity) are clean at every severity.

| File | Upload as |
|---|---|
| `carrier-board-revA-gerbers.zip` | The PCB file: Gerbers (X2, origin at the lower-left corner), Excellon drill in mm with PTH and NPTH split, and an IPC-D-356 netlist |
| `carrier-board-revA-bom.csv` | The BOM: one row per part, every designator spelled out, with the modules and J9 as DNP rows |
| `carrier-board-revA-centroid.csv` | Pick and place: top side only, SMD only, mm, from the same origin |
| `carrier-board-revA-assembly.pdf` | The assembly drawing. Designators are on the Fab layer, not the silkscreen, so this is where PCBWay finds them |

Order settings: 2 layers, FR-4 (TG150), 1.6 mm, 1 oz copper, 6/6 mil, 0.3 mm min hole, HASL lead-free, green
mask, white silk, 10 boards, all 10 assembled turnkey. Through-hole parts are
fitted too (C1, J1-J8; J9 is not fitted). Allow substitutes. The BOM's Note
column says which parts are generic and which must not change: L1 needs
Isat ≥ 4.5 A, C4/C5 must be 50 V X7R, and C7/C8 must be ≥ 16 V X7R.

Stock as of 2026-11-07 (Digi-Key unless stated): everything is in stock
except Phoenix 1715721, which Digi-Key expects around 30 Nov but RS,
element14 and TME have. D2 is an MMSZ5242BT1G, because BZT52C12 was on a
32-week lead time. L1 is a Bourns SRP7028A-100M (Isat 6 A), which fits the
footprint as laid out.

## Notes

- **PSUs stay at nominal 24 V** (the SMBJ24A TVS clamps above that). This is
  printed on the bottom silkscreen beside the input: "24 V NOMINAL - DO NOT TRIM PSU UP".
