# What PCBWay assembly needs from a KiCad project

Research for issue #5. Researched 2026-11-06 against primary sources only:
pages on pcbway.com, PCBWay's own GitHub repos (source code read directly),
PCBWay's published KiCad DRC file (downloaded and read), and the KiCad 9
`kicad-cli` reference. Where a page shows a date it is given; most PCBWay
pages carry only a "(c) 2024" footer, so treat undated figures as current
but not guaranteed. Anything not confirmed is listed under
[Open questions](#open-questions).

The board in question: a 2-layer (possibly 4-layer) carrier with 24 V
input, an on-board buck, SMD passives, JST-XH and screw-terminal
through-hole connectors, and female headers for two plug-in modules (XIAO
ESP32C6, TMC2209 stick) that PCBWay will **not** supply or fit. Quantity
5-10.

---

## 1. Design-rule minimums (standard process)

Two primary sources, which disagree slightly. The **DRC file is the
stricter one, so design to it.**

| Rule | Capabilities page (standard PCB) | PCBWay KiCad DRC file (2-layer, 1 oz) | Use |
|---|---|---|---|
| Trace width, outer | 0.1 mm / 4 mil | 0.127 mm | >= 0.127 mm (in practice 0.2 mm or more for signal) |
| Spacing, outer | 0.1 mm / 4 mil | 0.127 mm | >= 0.127 mm |
| Trace/space, inner (4-layer) | - | 0.1 / 0.1 mm (4-layer 1 oz file option: outer 0.09/0.09, inner 0.1/0.09) | - |
| 2 oz copper trace/space | - | 0.1524 / 0.1778 mm (commented-out option) | - |
| Min mechanical drill | 0.15 mm (max 6.0 mm) | 0.15 mm (max 6.3 mm) | - |
| Via (2-layer standard) | not listed separately | hole 0.3 mm, diameter 0.5 mm | **0.3 / 0.5 mm or bigger** |
| Via (4-layer standard) | - | hole 0.3 mm, diameter 0.45 mm | - |
| Annular ring | 0.15 mm (6 mil) | 0.25 mm for plated pads (rule "Pad Size") | 0.25 mm on THT pads |
| PTH hole (non-via) | tolerance +/-0.08 mm (NPTH +/-0.05) | 0.2-6.35 mm | - |
| NPTH hole | - | min 0.5 mm | - |
| Plated / non-plated slot width | - | 0.5 mm / 0.8 mm | - |
| Hole to hole, different nets | - | 0.5 mm | - |
| PTH to track | - | 0.33 mm | - |
| Via to track | - | 0.254 mm | - |
| Copper to board edge (routed) | Advanced table: "line to board edge" 0.25 mm normal | 0.3 mm (V-score: 0.4 mm) | **0.3 mm** (0.5 mm is more comfortable) |
| Solder mask bridge | Advanced table: 4 mil (<2 oz), 5 mil (>=2 oz) | - | - |
| Silkscreen line width | 0.15 mm | 0.15 mm | 0.15 mm |
| Silkscreen text height | 0.8 mm (width:height 1:5) | 0.8 mm | 0.8 mm (1.0 mm reads better) |
| Pad to silkscreen | - | 0.15 mm | - |
| Board size (PCB only) | min 3 x 3 mm | - | - |
| Layers | 1-14 | - | - |

Sources:
- Capabilities page: <https://www.pcbway.com/capabilities.html> (undated; footer "(c) 2024").
- DRC file: <https://pcbwayfile.s3.us-west-2.amazonaws.com/web/230915/KiCAD_Custom_DRC_Rules_for_PCBWay.zip>,
  linked from <https://www.pcbway.com/helpcenter/design_instruction/PCBWay_Custom_Design_Rules.html>
  ("Last updated on 29/05/2024"). The file header reads
  `#PCBWay Custom DRC for Kicad 7`. The page caveats: "All statemets without
  guarantee!" and "A perfect setting of design rules can understandably only
  be performed by the user."

For this board none of the minimums matter much. A 24 V / ~2 A stepper
carrier wants wide power traces and 2-layer standard vias anyway. Even
0.2 mm / 0.2 mm signal rules with 0.3/0.6 mm vias stay far from the limits.
Whether a finer track/space setting raises the price is a separate question
(see open questions).

### Using PCBWay's DRC file

The help page says to copy the `.kicad_dru` file into the project folder
and rename it to `<project>.kicad_dru`. The zip contains a complete dummy
KiCad project; only the `.kicad_dru` is needed. Several rules are "pick
one" blocks: the 2-layer 1 oz trace rule and the "2-layer standard" via rule
are active by default. A 4-layer board has to swap to the commented-out
4-layer blocks. The file was written for KiCad 7. It uses `(version 1)`
custom-rule syntax, which KiCad 8 and 9 still read, but I have not
test-loaded it in KiCad 9 (open question). Board Setup -> Constraints
should also be set to the same numbers so that the router and the
interactive tools respect them, not only DRC.

---

## 2. Gerber and drill export

### What PCBWay asks for

- Gerbers in RS-274X, at minimum "Silkscreen, Copper (Track) and Solder
  Paste" for assembly, and "the same Gerber files ... that you send to us for
  your PCB fabrication."
  <https://www.pcbway.com/assembly-file-requirements.html> (undated)
- The KiCad 9 guide
  <https://www.pcbway.com/helpcenter/generate_gerber/How_to_Export_Gerber__BOM__and_Pick_and_Place_Files_in_KiCad_9_0.html>
  ("Last updated on 11/09/2025") says: run DRC first, File -> Plot, refill
  zones when asked, then "Generate Drill Files...". Its actual checkbox
  settings exist only as screenshots, so I could not read them as text.
- An old KiCad 5.1.6 guide warns "Do NOT check 'Use extended X2 format',
  otherwise, the format will not be accepted."
  <https://www.pcbway.com/blog/help_center/Generate_Gerber_file_from_Kicad_5_1_6.html>.
  PCBWay's own current plugin (below) **enables** Gerber X2 attributes, so
  that advice looks obsolete. Use `--no-x2` if you want to be safe.

### What PCBWay's own plugin does (read from source)

PCBWay's plugin is the only machine-readable statement of "the settings
PCBWay wants". It is `plugins/process.py` in
<https://github.com/pcbway/PCBWay-Plug-in-for-Kicad> (v1.0.5, released
2026-03-31). It:

- plots F.Cu, B.Cu, any enabled In*.Cu, F/B.SilkS, F/B.Mask, F/B.Paste,
  Edge.Cuts and User.Comments
- `SetUseGerberAttributes(True)` (X2 on), `SetUseGerberProtelExtensions(False)`
  (`.gbr`), `SetSubtractMaskFromSilk(True)`, `SetUseAuxOrigin(True)` (the
  drill/place origin), no drill marks, scale 1, no mirroring
- sets solder-mask margin and min width to 0 before plotting
- writes Excellon drill with minimal header, offset to the aux origin, and
  PTH and NPTH **not** merged (separate files), no map. `SetFormat(False)`
  appears to select inch units. That reading comes from the KiCad API
  argument order and I have not checked it; PCBWay accepts both units in
  any case.
- writes an IPC-D-356 netlist (`PCBWay_netlist.ipc`)

### Equivalent `kicad-cli` recipe (KiCad 9)

The option names below are verified against
<https://docs.kicad.org/9.0/en/cli/cli.html>. First set the drill/place
origin (Place -> Drill/Place File Origin) at the bottom-left corner of the
board outline. PCBWay's position-file guide asks for this
(<https://www.pcbway.com/helpcenter/design_instruction/Generate_Position_File_in_Kicad.html>,
"Last updated 16/07/2024").

```bash
PCB=carrier.kicad_pcb
SCH=carrier.kicad_sch
OUT=fab

# 0. DRC must be clean (exit 5 on any violation)
kicad-cli pcb drc --severity-error --schematic-parity --exit-code-violations \
  -o $OUT/drc.rpt $PCB

# 1. Gerbers (2-layer; for 4-layer append In1.Cu,In2.Cu)
kicad-cli pcb export gerbers -o $OUT/gerber \
  -l F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
  --subtract-soldermask --use-drill-file-origin --no-protel-ext \
  $PCB

# 2. Drill: Excellon, mm, separate PTH/NPTH, same origin as the Gerbers
kicad-cli pcb export drill -o $OUT/gerber/ \
  --format excellon --excellon-units mm --excellon-separate-th \
  --drill-origin plot --generate-map --map-format gerberx2 \
  $PCB

# 3. Zip $OUT/gerber/ and upload it as the PCB file.
```

Notes:
- `--board-plot-params` would instead use whatever is saved in the board's
  Plot dialog. That is a reasonable alternative once the dialog is set up
  once and committed.
- The defaults that are already right: `--format excellon`,
  `--excellon-units mm`, `--excellon-zeros-format decimal`, Gerber precision 6.
- Gerber and drill must share one origin. Both commands above use the
  drill/place origin.
- KiCad 9 can also save all of the above as a `.kicad_jobset` and run it
  with `kicad-cli jobset run -f fab.kicad_jobset carrier.kicad_pro`.

---

## 3. BOM format

### PCBWay's requirements

- Formats: `.xls`, `.xlsx` or `.csv`. PDF BOMs are not accepted.
  <https://www.pcbway.com/assembly-file-requirements.html>,
  <https://www.pcbway.com/smt_ordering_guide.html>
- Consigned/kitted BOM fields: Line #, Quantity per part number, Reference
  designator, Part number, Description, Package, and **Type (surface mount,
  thru-hole, hybrid)**. Turnkey or partial turnkey orders add Manufacturer
  name, **Manufacturer part number**, and Distributor part number.
  (assembly-file-requirements page)
- "Please fill in designators(references), quantity, package and part
  numbers." For passives with no part number, give value and package. Mark
  each part PTH or SMD and use PCBWay's template.
  (smt_ordering_guide; the only date shown is a 2018-12-30 material spec)
- Plugin README / PCM metadata: "designator, quantity, MPN/part number,
  package/footprint are necessary". Manufacturer and description are
  optional. <https://github.com/pcbway/PCBWay-Plug-in-for-Kicad>
- Common rejections, from
  <https://www.pcbway.com/helpcenter/Assembly_files_problems/Top_13_suggestions_for_solving_PCB_Assembly_files_problems.html>
  ("Last updated 14/05/2020"): BOM and centroid designator counts differ;
  BOM quantity doesn't match the designator count; value disagrees with the
  part number; designators repeated; designators not on the silkscreen; LED
  colour not specified; top and bottom placements mixed in one file.
- PCBWay's own template is the authority for header names. It is a linked
  download that I did not open (open question).

### Mapping KiCad fields to it

PCBWay's plugin looks up the MPN in the first footprint field matching
(`plugins/utils.py`): `mpn`, `MPN`, `Mpn`, `PCBWay_MPN`, `part number`,
`Part Number`, `Part No.`, `Mfr. Part No.`, `Mfg Part`,
`Manufacturer_Part_Number`. It looks up the package in `pack`/`package`/`case`
(any case). **So name the symbol field `MPN`**, add `Manufacturer` and
`Package`, and every tool, PCBWay's included, picks them up. The plugin
also copies every other non-empty field into extra columns.

The plugin's BOM columns are `Designator, Quantity, Value, Footprint,
Package, MPN` plus `DNP` and `Mount_Type` (`smt`/`tht`, taken from the
footprint's SMD/through-hole attribute). It then adds any custom fields.
Rows are grouped by value+footprint+package+MPN, and DNP parts get their own
rows.

The equivalent from the schematic (option names verified against the KiCad
9 CLI doc). This assumes the fields `MPN`, `Manufacturer`, `Package` and a
text field `Type` (SMD/THT):

```bash
kicad-cli sch export bom -o fab/bom.csv \
  --fields 'Reference,${QUANTITY},Value,Manufacturer,MPN,Package,Footprint,Type,${DNP}' \
  --labels 'Designator,Quantity,Value,Manufacturer,Manufacturer Part Number,Package,Footprint,Type,DNP' \
  --group-by 'Value,Footprint,MPN,${DNP}' \
  --ref-range-delimiter '' \
  --string-delimiter '"' \
  $SCH
```

`--ref-range-delimiter ''` turns off "R1-R4" ranges, so every designator is
listed explicitly, which is what the designator-count checks want. Add
`--exclude-dnp` to drop DNP rows altogether (see section 5 for which way to
go). Converting to `.xlsx` and pasting into PCBWay's template is optional;
CSV is accepted.

---

## 4. Centroid / pick-and-place format

### PCBWay's requirements

- Contents: reference designator, X, Y, rotation, side (Top/Bottom). "Only
  surface mounting parts are listed in the Centroid."
  (<https://www.pcbway.com/assembly-file-requirements.html>)
- Format Excel, `.txt` or `.pos`; "please set the unit to be mm"; if a board
  has only THT parts no centroid is needed.
  (<https://www.pcbway.com/helpcenter/pcb_assembly_ordering/What_files_are_requested_for_assembly_production_.html>,
  "Last updated on 12/05/2020")
- THT designators may be left out of the centroid. Designators must match
  the silkscreen or assembly drawing. (smt_ordering_guide)
- Top and bottom go in separate files. (Top-13 page, item 5)
- Origin and rotation: "The board's origin is defined as the (0, 0) value and
  is located at the lower left corner of the board"; "Rotation is ...
  referenced from a top point of view", 0-360.
  (<https://www.pcbway.com/helpcenter/design_instruction/How_to_create_a_Centroid_File_from_Eagle__Altium_Sprint_Layout_and_ORCAD_.html>,
  "Last updated on 26/02/2024")
- KiCad-specific: set Place -> Drill and Place Offset at the bottom-left
  corner of the board frame, then export File -> Fabrication Outputs ->
  Footprint Position (.pos) "keeping other defaults". (Generate_Position_File_in_Kicad, 16/07/2024)

PCBWay publishes **no rotation-offset table** for KiCad. Its plugin writes
KiCad's native footprint orientation unchanged: `rotation =
f.GetOrientation().AsDegrees()`, with no per-package correction. In other
words PCBWay accepts KiCad-native rotations, and its engineers reconcile
them against the footprints and the Gerbers. Assembly is preceded by file
review (and sample photos on prototype orders, see section 7), which is
where orientation gets checked. **Mark pin 1 and polarity clearly on the
silkscreen**; the Top-13 page calls out tantalum polarity specifically.

### What PCBWay's plugin writes

`PCBWay_positions.csv` has the columns `pos_x, pos_y, rotation, side,
designator, mpn, pack, footprint, value, mount_type`. Units are mm. X/Y are
relative to the aux (drill/place) origin, with **Y negated** so that +Y is
up (KiCad's internal Y points down). Side is `top`/`bottom`. Bottom X is
**not** negated. THT parts are **included** unless the footprint has
"Exclude from position files" set. That differs from the help-center advice
but is evidently acceptable to PCBWay, since it is their tool.

### `kicad-cli` equivalent

```bash
kicad-cli pcb export pos -o fab/pos-top.csv \
  --side front --format csv --units mm \
  --use-drill-file-origin --smd-only --exclude-dnp \
  $PCB
```

- `--units mm` is required. **The CLI default is `in`.**
- `--use-drill-file-origin` gives the bottom-left origin PCBWay asks for,
  once the origin is placed there.
- `--smd-only` matches "only surface mounting parts are listed". Leave it
  off if you would rather PCBWay see THT positions too, as their plugin does.
- If anything ever goes on the bottom side, run a second export with
  `--side back` into `pos-bottom.csv`. PCBWay wants the sides in separate
  files. Leave `--bottom-negate-x` off: PCBWay's plugin doesn't negate, and
  their origin definition is a top view.
- KiCad's CSV header is `Ref,Val,Package,PosX,PosY,Rot,Side`, which is
  self-describing enough for PCBWay's engineers. That header comes from
  KiCad's output, not from the CLI doc, which doesn't list columns; check it
  on the first real export.

Every SMD designator in the BOM must appear in the SMD centroid, and the
reverse. Running `--exclude-dnp` on the pos export while leaving DNP rows in
the BOM is fine, provided those rows say DNP.

---

## 5. Part sourcing, consigned parts, and DNP for the two modules

### Sourcing modes

<https://www.pcbway.com/assembly-capabilities.html> and the quote form
<https://www.pcbway.com/quotesmt.aspx> offer:

- **Turnkey**: PCBWay buys everything. It says it adds no markup, but asks
  for approval on substitutions. A quote option "Alternatives/substitutes
  made in China: No/Yes" controls whether it may substitute.
- **Kitted / consigned**: you ship all parts, with a BOM and packing list.
- **Combo (partial turnkey)**: you ship some parts and PCBWay sources the
  rest.

For 5-10 boards, turnkey with MPNs specified is the obvious choice.
Imported (non-China-stock) parts can add "at least 5-7 working days"
(older help-center article, see section 7).

### The two plug-in modules

PCBWay's documented convention for parts that should be neither bought nor
soldered is to **mark the line DNP (or DNS) in the BOM**:

- "Components should not be purchased or soldered so you can mark them as
  DNP or DNS."
  (<https://www.pcbway.com/helpcenter/pcb_assembly_ordering/Common_factors_that_affect_the_efficiency_of_Assembly_service_.html>)
- When changing a BOM: "Write 'DNP' for those parts that do not need us to
  solder, and it is better to add a strikethrough on that line."
  (<https://www.pcbway.com/helpcenter/SMT_Engineering_Questions/What_should_I_do_to_change_gerber_file__parts_and_BOM_during_soldering_.html>)

These are older pages (search metadata put them at roughly 4-6 years old).
I found no dedicated current DNP article.

**Recommended approach for the XIAO and TMC2209:**

1. Keep the **female headers as separate, fitted parts** with their own
   symbols or footprints and real MPNs (e.g. 1x7 2.54 mm female headers for
   the XIAO, 1x8 for the TMC2209 stick). Their BOM `Type` is THT.
   PCBWay solders these; they are what the board actually needs fitted.
2. Give each module its own symbol (e.g. `U1 XIAO_ESP32C6`,
   `U2 TMC2209_module`), either with no footprint or with a
   fab/courtyard-only outline footprint. Set the KiCad **DNP** attribute on
   both. If you would rather they never reach the BOM at all, also set
   "Exclude from BOM" and "Exclude from position files".
3. Export the BOM **with** DNP rows and the `${DNP}` column, so PCBWay sees
   "U1 ... DNP" in writing, the way their help pages ask. Export the
   centroid with `--exclude-dnp`. PCBWay's own plugin does the same: DNP
   rows stay in the BOM flagged `DNP=Yes`. The plugin detects DNP from
   KiCad's DNP attribute (`IsDNP()`), from any field named `DNP`, or from a
   Value of `DNP`.
4. Say it again in the order's special-instructions note: "U1/U2 are
   customer plug-in modules, DNP, not supplied; fit the female headers
   J_x/J_y only."
5. Optionally pass `--cdnp`/`--sdnp` to `pcb export gerbers` for any Fab-layer
   assembly drawing, so the DNP outlines are visibly crossed out.

A single combined "module footprint" whose pads *are* the headers is the
pattern to avoid. Marking it DNP would also drop the headers from assembly.

---

## 6. Mixed SMD + THT assembly: support and pricing

- **Supported**: "We offer Surface Mount (SMT), Thru-Hole (THT) and hybrid
  of both" and "single or double-sided placement".
  (<https://www.pcbway.com/assembly-capabilities.html>,
  <https://www.pcbway.com/pcb_prototype/PCB_assembly_Capabilities.html>)
- **Method**: PCBWay lists wave soldering among its processes. Its
  mixed-assembly blog says THT is inserted after SMT reflow and then
  wave- or selective-soldered.
  (<https://www.pcbway.com/pcb-assembly.html>,
  <https://www.pcbway.com/blog/Engineering_Technical/Mixed_Technology_PCB_Assembly.html>).
  Whether a given 5-10 piece job is hand-soldered or wave-soldered is not
  stated anywhere I found.
- **Pricing**: no per-joint or per-THT-part rate is published. The quote
  form takes counts of "unique parts, SMD parts, BGA/QFP parts, through-hole
  parts" (optional if a correct BOM is uploaded) and an assembly side (Top /
  Bottom / Both). Its price breakdown has separate lines for PCB, stencil,
  assembly service, DTP fee, discounts and shipping. Components, duties and
  VAT are excluded, and "exact quotation will be updated after all the files
  you uploaded pass review" (<https://www.pcbway.com/quotesmt.aspx>).
  The only way to get a THT cost is to run the quote calculator with the
  real part counts.
- **Implications for this board**: keep **all SMD on the top side**, which
  means one stencil and one reflow pass. Put the THT parts (JST-XH, screw
  terminals, female headers, maybe a bulk electrolytic) on the top as well,
  soldered from the bottom. Each unique THT part adds to the count the quote
  is priced on. Hand-soldering the THT parts yourself is a fallback if
  quoted THT cost is unreasonable: list them as DNP, or order assembly
  "Top, SMD only".

---

## 7. MOQ, board size, lead times

- **Assembly MOQ**: "Our minimum is as low as 5 pieces."
  (assembly-capabilities and PCB_assembly_Capabilities pages). The PCB
  quantity dropdown also starts at 5 (quotesmt).
- **Assembly board size**: the two PCBWay pages **disagree**.
  assembly-capabilities says min 10 x 10 mm, PCB_assembly_Capabilities says
  min 50 x 100 mm; both give max 250 x 500 mm, and smaller boards must be
  panelized. The quote form advises panelizing "if the quantity is above 20
  or any side is under 50mm". A carrier board with a side under 50 mm may
  draw a panelization request (open question).
- **Lead time**: "Our assembly time ranges from 24 Hours to a few weeks",
  and the clock starts only once parts and all files are complete
  (assembly-capabilities). The PCBWay homepage gives "2 days - 3 weeks".
  Older help-center articles (about 6 years old) say prototype orders are
  usually done in about 3 working days after sample-photo confirmation, and
  small/medium quantities in about 5. Imported parts add at least 5-7
  working days, plus customs. An assembly quote takes 1-2 days because the
  BOM is reviewed by hand.
  (<https://www.pcbway.com/helpcenter/pcb_assembly_ordering/When_can_you_ship_out_the_assembled_pcbs_after_sample_photos_confirmation_.html>,
  <https://www.pcbway.com/helpcenter/pcb_assembly_ordering/Why_the_lead_time_of_assembly_order_is_long_.html>,
  <https://www.pcbway.com/helpcenter/pcb_assembly_ordering/How_long_can_I_get_quotation_after_BOM_is_uploaded_.html>)
  A realistic end-to-end estimate for turnkey 5-10 boards is 2-4 weeks plus
  shipping. That figure is an inference, not a PCBWay number.

---

## 8. PCBWay's KiCad plugins

PCBWay ships **two** near-identical official plugins, both MIT-licensed,
both in the KiCad Plugin and Content Manager, and both at v1.0.5 released
2026-03-31. The PCM metadata gives `kicad_version` "6.00" as the minimum,
and the README says "We have updated plugin for Kicad10".

| | PCBWay Plug-in for KiCad | PCBWay Fabrication Toolkit for KiCad |
|---|---|---|
| Repo | <https://github.com/pcbway/PCBWay-Plug-in-for-Kicad> | <https://github.com/pcbway/PCBWay-Fabrication-Toolkit-for-KiCad> |
| Behaviour | Builds the files and **uploads** them to `pcbway.com/Common/KiCadUpFile/` with board width, height and layer count, then opens the order page in a browser | Builds the same files into a timestamped folder in the project and opens it. **Nothing is uploaded.** |
| Outputs | Gerber zip (with drill files), `PCBWay_netlist.ipc` (IPC-D-356), `PCBWay_bom.csv`, `PCBWay_positions.csv` | the same, prefixed with the board name |

The Toolkit was announced 2023-10-11
(<https://www.pcbway.com/blog/News/PCBWay_Fabrication_Toolkit_for_KiCad_23c41e77.html>).
Its exact settings are in section 2 (Gerber/drill), section 3 (BOM fields)
and section 4 (positions). Both plugins **modify the board's design
settings in memory** (solder-mask margin and min width set to 0) before
plotting. That is harmless if you don't save afterwards, but worth knowing.

For a repo-driven, reproducible flow, the `kicad-cli` recipe above (or a
`.kicad_jobset`) is better than either plugin, because the plugins run only
in the GUI. The Toolkit is still a useful **cross-check**: run it once and
diff its BOM and positions against the CLI output.

---

## 9. Checklist for this carrier board

1. Copy PCBWay's `.kicad_dru` in as `<project>.kicad_dru`, choosing the
   2-layer or 4-layer blocks. Mirror its numbers in Board Setup.
2. Use 0.3 / 0.5 mm vias or bigger, a 0.25 mm annular ring on THT pads,
   0.3-0.5 mm copper-to-edge, and silk at least 0.15 mm wide and 0.8 mm tall.
3. Place the drill/place origin at the bottom-left of Edge.Cuts.
4. Put the symbol fields `MPN`, `Manufacturer`, `Package` and `Type`
   (SMD/THT) on every fitted part.
5. Give the female headers their own fitted parts. Give the XIAO and the
   TMC2209 their own symbols, marked DNP.
6. Keep all SMD on the top side.
7. Export with the `kicad-cli` commands above: Gerbers and drill zipped, a
   BOM CSV including DNP rows, and `pos-top.csv` in mm with SMD only,
   excluding DNP.
8. Check that the SMD designator sets in the BOM and the centroid are
   identical.
9. Upload to the quote form as Turnkey, Top side, quantity 5-10, with the
   THT part count filled in. Add an instructions note about U1/U2.

---

## Open questions

- **Price vs track/space**: the quote form has a "min track/spacing"
  field. I could not confirm from text at which threshold (e.g. 6/6 mil vs
  4/4 mil) the price changes. Check it in the calculator.
- **Annular ring**: the capabilities page says 0.15 mm, the DRC file says
  0.25 mm for plated pads. Using 0.25 mm sidesteps the question.
- **Edge clearance**: 0.25 mm on the advanced table, 0.3 mm in the DRC file;
  the standard table lists none. Using 0.3 mm or more sidesteps it.
- **Assembly minimum board size**: 10 x 10 mm and 50 x 100 mm on two PCBWay
  pages. Ask whether a small single carrier board needs panel rails for
  5-10 pieces.
- **THT cost and method**: no per-joint price, and no statement of whether
  small runs are hand- or wave-soldered. Get a real quote.
- **BOM template headers**: PCBWay's downloadable template wasn't opened, so
  its exact column names are unconfirmed. Their plugin's headers
  (`Designator, Quantity, Value, Footprint, Package, MPN, DNP, Mount_Type`)
  are evidently accepted.
- **X2 Gerbers**: the old KiCad 5 guide says not to use X2; the current
  plugin uses it. If in doubt, pass `--no-x2`.
- **KiCad 9 Gerber/drill checkbox values** on PCBWay's KiCad 9 guide are
  screenshot-only and were not read. The plugin's source settings are used
  instead.
- **DRC file on KiCad 9**: it was written for KiCad 7 and has not been
  test-loaded in 9.
- **Plugin drill units**: `SetFormat(False)` is read as inch Excellon from
  the KiCad API argument order, not verified.
- **DNP guidance age**: the DNP help pages are several years old. Confirm
  via the order's instructions note.
- **KiCad pos CSV column names** are from KiCad's known output, not the CLI
  documentation.
