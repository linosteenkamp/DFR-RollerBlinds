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
| `carrier.kicad_sym` (+ `sym-lib-table`) | Project symbols: the module sockets, AP64200, a DPAK-numbered P-FET, and the DNP module placeholders |
| `carrier-board-schematic.pdf` | Plot for review; regenerate after edits with `kicad-cli sch export pdf -o carrier-board-schematic.pdf carrier-board.kicad_sch` |

## Checks

```bash
kicad-cli sch erc --severity-all --exit-code-violations carrier-board.kicad_sch
```

Rev A was ERC-clean at every severity. Its netlist was also checked net by net
against the intended connections: 37 named nets, plus 3 intentional
no-connects (TMC2209 TX and CLK, and the DIAG row's second pin).

## How the schematic is drawn

Every connection goes through a **net label** on a short wire stub at the
pin. There are no long wires. So to find everything on a net, search for its
label.

The modules are not parts on this board. `U1` (XIAO) and `U2` (TMC2209) are
pinless DNP placeholders. The sockets they plug into, `J2`/`J3` (XIAO) and
`J4`/`J5`/`J6` (TMC2209), are real parts that get assembled. This is the
approach PCBWay's assembly guidance calls for (see `docs/research/pcbway-kicad.md`).

## Open items

- **J6 DIAG-row pin order** and whether both pins exist: waiting on
  [Measure the physical XIAO and TMC2209 modules](https://github.com/linosteenkamp/DFR-RollerBlinds/issues/13).
- **L1 part number:** to be chosen. It must be 10 µH, shielded, with Isat
  ≥ 4.5 A (the AP64200 high-side limit). The footprint is a placeholder
  (Bourns SRP7028A size) until the part is picked.
- **Generic passives:** they have values and packages but no part numbers yet.
  Those get assigned at the PCBWay export, along with a stock check for every
  part (the research couldn't check PCBWay or LCSC stock).
- **PSUs stay at nominal 24 V** (the SMBJ24A TVS clamps above that). The
  "do not trim up" silkscreen note gets added at layout.
