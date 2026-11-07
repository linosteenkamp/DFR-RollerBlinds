#!/usr/bin/env bash
# Rebuild the PCBWay upload package from the KiCad sources.
# Run from anywhere:  hardware/carrier-board/fab/export.sh
# Needs kicad-cli (KiCad 10) on PATH; stops on the first failed check.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(dirname "$HERE")"
PCB="$PROJ/carrier-board.kicad_pcb"
SCH="$PROJ/carrier-board.kicad_sch"
REV=revA
NAME="carrier-board-$REV"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$HERE"
rm -f "$NAME"-*

# 0. ERC and DRC must be clean at every severity, with schematic parity
kicad-cli sch erc --severity-all --exit-code-violations -o "$TMP/erc.rpt" "$SCH" >/dev/null
kicad-cli pcb drc --severity-all --schematic-parity --exit-code-violations \
  -o "$TMP/drc.rpt" "$PCB" >/dev/null

# 1. Gerbers (RS-274X with X2 attributes, as PCBWay's own plugin plots them),
#    relative to the drill/place origin at the board's lower-left corner
mkdir -p "$TMP/gerber"
kicad-cli pcb export gerbers -o "$TMP/gerber" \
  -l F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
  --subtract-soldermask --use-drill-file-origin --no-protel-ext --check-zones \
  "$PCB" >/dev/null

# 2. Drill: Excellon in mm, PTH and NPTH in separate files, same origin
kicad-cli pcb export drill -o "$TMP/gerber/" \
  --format excellon --excellon-units mm --excellon-separate-th \
  --drill-origin plot --generate-map --map-format gerberx2 \
  "$PCB" >/dev/null

# 3. IPC-D-356 netlist, so PCBWay's electrical test checks against our nets
kicad-cli pcb export ipcd356 -o "$TMP/gerber/$NAME.ipc" "$PCB" >/dev/null

(cd "$TMP/gerber" && zip -q -X "$HERE/$NAME-gerbers.zip" ./*)

# 4. BOM: one row per part, every designator spelled out, DNP rows kept and marked
kicad-cli sch export bom -o "$HERE/$NAME-bom.csv" \
  --fields 'Reference,${QUANTITY},Value,Package,Type,Manufacturer,MPN,Note,${DNP}' \
  --labels 'Designator,Quantity,Value,Package,Type,Manufacturer,Manufacturer Part Number,Note,DNP' \
  --group-by 'Value,Footprint,MPN,${DNP}' \
  --ref-range-delimiter '' --string-delimiter '"' \
  "$SCH" >/dev/null

# 5. Centroid: top side only (nothing is placed on the bottom), SMD parts, mm
kicad-cli pcb export pos -o "$HERE/$NAME-centroid.csv" \
  --side front --format csv --units mm \
  --use-drill-file-origin --smd-only --exclude-dnp \
  "$PCB" >/dev/null

# 6. Assembly drawing: designators live on the Fab layer, not the silkscreen,
#    so PCBWay gets them on paper (pads sketched, DNP parts crossed out)
kicad-cli pcb export pdf -o "$HERE/$NAME-assembly.pdf" \
  -l F.Fab,Edge.Cuts --mode-single --ibt --sp --cdnp --ev --scale 0 \
  "$PCB" >/dev/null

ls -1 "$HERE"/"$NAME"-*
