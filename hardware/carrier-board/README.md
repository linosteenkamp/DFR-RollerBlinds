# Carrier board (rev A)

This is the KiCad 10 project for the carrier board of the roller-blind
controller. The XIAO ESP32C6 and the BTT TMC2209 V1.3 plug into sockets.
The 24 V DC input connects to a screw terminal. An AP64200 on the board
makes 5 V. The board uses the v2.4.x pin map without changes. Thus the
board does not need a new firmware release.

The wayfinder map
[PCBWay carrier board for the roller-blind controller](https://github.com/linosteenkamp/DFR-RollerBlinds/issues/1)
has the plan and the status. The reasons for the part selections are in
`docs/research/` (`buck-regulator.md`, `input-protection.md`,
`module-footprints.md`, `pcbway-kicad.md`).

| File | What it is |
|---|---|
| `carrier-board.kicad_sch` | The schematic. It is the source of truth. Edit it in the KiCad GUI. |
| `carrier-board.kicad_pcb` | The layout: 80 × 50 mm, 2 layers, routed. It is the source of truth for the board. |
| `carrier-board.kicad_dru` | The KiCad DRC rules that PCBWay publishes (2-layer, 1 oz set), not changed |
| `carrier-board.kicad_pro` | The project settings, with the net classes (refer to the table below) |
| `carrier.kicad_sym` (+ `sym-lib-table`) | The project symbols: the module sockets, the AP64200, a P-FET with DPAK pin numbers, and the DNP module placeholders |
| `carrier.pretty/` (+ `fp-lib-table`) | The project footprints: sockets for the TMC2209 with a tight courtyard (its DIAG row is 2.54 mm from EN), and copies of the test pad, L1 and J6 without silkscreen |
| `carrier-board-schematic.pdf` | A plot of the schematic for review |
| `fab/` | The PCBWay upload package, and `export.sh`, which makes the package again (refer to the section below) |
| `renders/` | `top.png` and `bottom.png` (3D), and `copper-t.png` and `copper-b.png` (tracks without the pours), for review |

## Checks

```bash
kicad-cli sch erc --severity-all --exit-code-violations carrier-board.kicad_sch
kicad-cli pcb drc --severity-all --schematic-parity --exit-code-violations carrier-board.kicad_pcb
```

Rev A passes the two checks at all severities. DRC finds 0 violations,
0 unconnected items and 0 schematic-parity issues. The netlist of the
schematic was also compared, net by net, with the intended connections.
It has 37 named nets and 3 intentional no-connects (TMC2209 TX, CLK and
VREF).

## How the schematic is drawn

Each connection goes through a **net label** on a short wire stub at the
pin. There are no long wires. To find all the items on a net, search for
its label.

The modules are not parts of this board. `U1` (XIAO) and `U2` (TMC2209)
are DNP placeholders without pins. The sockets for the modules are real
parts that PCBWay assembles: `J2` and `J3` (XIAO), and `J4`, `J5` and
`J6` (TMC2209). The PCBWay assembly guidance tells you to use this
method (refer to `docs/research/pcbway-kicad.md`).

## How the layout was made

The placement agrees with the placement decision on the map:

- The 24 V terminal and the motor connector are at the left end.
- The TMC2209 is next to them. It is turned 180°, so its power column
  is toward them.
- The buck regulator is in the middle.
- The XIAO is at the right end. Its USB-C is at the top edge. Its
  antenna end is toward the center of the board, above a keep-out area
  that has no copper.
- The keypad connector is at the right edge.
- The four corners have M3 holes.

The reference designators are on the Fab (assembly) layer. Thus the
silkscreen shows only function labels.

Freerouting 2.5.0 routed the board. Freerouting needs **Java 25**. Run
`brew install openjdk@25`, then `/opt/homebrew/opt/openjdk@25/bin/java -jar …`.
The Java 21 of the KiCad plugin cannot run it.

**GND has real traces**. It does not use only the pours. The two GND
pours were added after the routing. Then vias connect the two pours: a
grid of vias, one via next to each SMD GND pad, and six vias under the
exposed pad of the AP64200.

The sequence is important. If you export a board that has its pours,
GND becomes a "plane". Freerouting does not route a plane, and the top
pour then breaks into islands under the buck regulator. To route the
board again from the start, use the same sequence:

1. Remove the pours.
2. Export the DSN.
3. Route the board.
4. Import the SES.
5. Add the pours and the stitching vias again.

These are the net classes and their track widths:

| Class | Nets | Width |
|---|---|---|
| Power | 24V_IN, 24V_F, +24V, VM | 1.0 mm |
| Motor | MOT_A1/A2/B1/B2 | 0.8 mm |
| Supply | +5V_BUCK, 5V_PRE, +5V, SW, +3V3 | 0.5 mm |
| Ground | GND | 0.5 mm, plus the pours |
| Default | signals | 0.25 mm |

Freerouting makes some segments narrower where they connect to
fine-pitch pads. 7 Power segments are 0.6 mm, and some signal segments
are 0.19 mm. All of them are wider than the PCBWay minimum of 0.127 mm.

## The PCBWay package

`fab/export.sh` makes all the files in `fab/` again from the KiCad
files. The script stops with an error if ERC or DRC (with parity) finds
an issue at any severity.

| File | Upload as |
|---|---|
| `carrier-board-revA-gerbers.zip` | The PCB file: Gerbers (X2, origin at the lower-left corner), Excellon drill files in mm with PTH and NPTH in different files, and an IPC-D-356 netlist |
| `carrier-board-revA-bom.csv` | The BOM: one row for each part, with each designator written fully. The modules and J9 are DNP rows. |
| `carrier-board-revA-centroid.csv` | Pick and place: top side only, SMD only, in mm, from the same origin |
| `carrier-board-revA-assembly.pdf` | The assembly drawing. The designators are on the Fab layer, not on the silkscreen. PCBWay finds them on this drawing. |

Order settings: 2 layers, FR-4 (TG150), 1.6 mm, 1 oz copper, 6/6 mil,
0.3 mm minimum hole, lead-free HASL, green mask, white silkscreen,
10 boards. PCBWay assembles all 10 boards turnkey. They also install the
through-hole parts (C1, J1-J8; they do not install J9). Permit
substitutes. The Note column of the BOM tells which parts are generic
and which parts must not change:

- L1 must have Isat ≥ 4.5 A.
- C4 and C5 must be 50 V X7R.
- C7 and C8 must be ≥ 16 V X7R.

Stock on 2026-11-07 (Digi-Key, unless a different supplier is given):

- All parts are in stock, except the Phoenix 1715721. Digi-Key expects
  it at approximately 30 Nov. RS, element14 and TME have it in stock.
- D2 is an MMSZ5242BT1G, because the BZT52C12 had a lead time of
  32 weeks.
- L1 is a Bourns SRP7028A-100M (Isat 6 A). It fits the footprint of the
  current layout.

## Notes

- **Keep the PSUs at the nominal 24 V.** The SMBJ24A TVS clamps above
  that voltage. The bottom silkscreen next to the input shows this text:
  "24 V NOMINAL - DO NOT TRIM PSU UP".
