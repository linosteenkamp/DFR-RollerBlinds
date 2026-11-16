# Gearbox (1:15)

The 3D-printed spur-gear reduction between the stepper and the roller tube.
It replaces the old `Blinds 2.step` reference. The source of truth is the
Fusion design **Blinds gearbox assembly** in the Fusion project
`BlindsController2`. Everything here was exported on 2026-11-16 from version 15 of that design,
which references `gear housing` v13, `Gear Housing Lid` v6,
`Driven Gear 60 teeth` v6, `Idler Gear` v3, `Driver Gear` v1 and
`Blinds Shaft Attachment` v7.

![Assembled gearbox](renders/assembly-top.png)
![Gear train, housing and lid hidden](renders/geartrain-top.png)

## Gear train

Module 1 spur gears, two stages:

| Stage | Driving | Driven | Ratio | Centre distance |
|---|---|---|---|---|
| 1 | Driver gear, 12 T (motor shaft) | Idler gear, large wheel, 36 T | 3 : 1 | 24.0 mm |
| 2 | Idler gear, pinion, 12 T | Driven gear, 60 T | 5 : 1 | 36.0 mm |

The overall ratio is 3 × 5 = **15 : 1**, which is the 1:15 that the firmware's
step math assumes (24 000 microsteps per output revolution at 1/8 microstep).
Despite its name, the "idler" is a compound gear (36 T and 12 T on one body),
not a true idler. Tooth counts and centre distances were measured from the
model's geometry, not taken from a parameter table, because the design has no
user parameters.

## Motion

The Fusion assembly is set up to move. The left housing is grounded. Each
gear has an as-built revolute joint about its own axis: `Rev Driver Gear`,
`Rev Idler Gear` and `Rev Driven Gear 60 teeth`. The shaft attachment is
rigidly joined to the driven gear. The lid and the four bearings are
rigidly joined to the housing. Two motion links set the ratios:

| Motion link | Joint 1 | Joint 2 | Values |
|---|---|---|---|
| `Link driver-idler 3:1` | Rev Driver Gear | Rev Idler Gear | 360° : 120°, reversed |
| `Link idler-driven 5:1` | Rev Idler Gear | Rev Driven Gear 60 teeth | 360° : 72°, reversed |

To animate it, use **Assemble → Drive Joints** on `Rev Driver Gear` (or drag
the driver gear), or set up a **Motion Study**. Driving the driver 30° turns
the idler 10° the opposite way, and turns the driven gear and the shaft
attachment 2° the same way as the driver: 15:1.

The gears were already meshed at rest. An interference check on the three
gears finds only tooth contact (0.01–0.05 mm³) both at rest and with the
driver turned 30°. The same check flags 3.5 mm³ of overlap between the
shaft attachment and the driven gear's hub at their current positions.



| Path | What it is |
|---|---|
| `blinds-gearbox-assembly.step` | Whole assembly, bearings included, with parts in their assembled positions. Use it for viewing, for fit checks, or for opening in other CAD tools. |
| `stl/*.stl` | One binary STL per printed part, exported at high mesh refinement in millimetres. Each STL is in its part's own coordinates, so orient it in the slicer. |
| `fusion/<part>.f3d` | Each printed part's own Fusion design, with its timeline, for editing offline. |
| `fusion/blinds-gearbox-assembly.f3d` | The assembly design only. Its parts are **cross-references** into the Fusion cloud project, so the archive is not self-contained. Edit in the cloud project, or rebuild an assembly from the per-part `.f3d` files. |
| `print/x2d-*.stl` | Print plates for the Bambu Lab X2D, laid out and oriented (see [Printing](#printing-bambu-lab-x2d)). |
| `renders/` | Viewport captures: the closed assembly, and the gear train with housing and lid hidden, each from the top and bottom. |

## Parts

### Printed

| Part | STL | Notes |
|---|---|---|
| Gear housing, left | `gear-housing-left.stl` | 108.9 × 73.4 × 23.5 mm. The housing shown in the assembly. |
| Gear housing lid, left | `gear-housing-lid-left.stl` | Fits the left housing. |
| Gear housing, right | `gear-housing-right.stl` | Exact mirror of the left housing. |
| Gear housing lid, right | `gear-housing-lid-right.stl` | Exact mirror of the left lid. Fits the right housing. |
| Driver gear, 12 T | `driver-gear-12t.stl` | Fits on the motor shaft. |
| Idler gear, 36 T + 12 T | `idler-gear-36t-12t.stl` | The compound middle stage. |
| Driven gear, 60 T | `driven-gear-60t.stl` | The output stage. |
| Shaft attachment | `shaft-attachment.stl` | Ø 31.5 × 22.6 mm splined spigot that drives the roller tube. It sits on the driven gear's axis, below the output bearing. |

### Bearings (bought)

| Qty | Part | Size (bore × OD × width) | Where |
|---|---|---|---|
| 1 | 61804-2RS1 | 20 × 32 × 7 mm | Output: driven gear hub |
| 3 | 628/7-2Z | 8 × 14 × 5 mm | One each on the driver gear, the idler gear and the top of the driven gear |

## Printing (Bambu Lab X2D)

Three plate files are in `print/`. Each one is a single STL with every part
already oriented and spaced 10 mm apart, centred on the X2D's 256 × 256 mm
plate. Each fits inside 235 × 250 mm, so it also clears the narrower
dual-nozzle print area (235.5 mm wide).

| File | Parts | Footprint × height |
|---|---|---|
| `x2d-gears.stl` | Driven gear 60 T, idler 36/12 T, shaft attachment, driver gear 12 T: one gearbox's worth | 175.5 × 62.0 × 22.6 mm |
| `x2d-housing-left.stl` | Left housing + left lid | 108.9 × 156.7 × 23.5 mm |
| `x2d-housing-right.stl` | Right housing + right lid | 108.9 × 156.7 × 23.5 mm |

Bambu Studio imports each file as one object. Use **Split → To objects**
if you want per-part settings, or want to move or duplicate a part.

Orientation was chosen by scoring all six axis-aligned orientations of each
part for bed-contact area and for overhang area (down-facing area steeper
than 45° that isn't on the bed):

- **Lid:** printed with its flat outer face down. It needs no supports.
- **Driver gear, driven gear:** printed with their large flat end down.
  They need essentially no supports.
- **Shaft attachment:** printed upside down from its STL, with the flat end
  that faces the gear down (chosen for the best use of support material).
  The bed holds about 160 mm² of it. Ledges of about 205 mm² at 0.5 mm,
  86 mm² at 1.0 mm and 240 mm² at 3.1 mm up need the slicer to support
  them.
- **Idler gear:** printed with its bearing hub down. The 36 T wheel's
  underside is 1.5 mm off the bed (about 780 mm² of overhang), so it needs
  support. *Support on build plate only* is enough. Printing it with the
  pinion down instead puts the wheel 6.5 mm up on a 108 mm² base, which is
  worse.
- **Housing:** printed floor-down, as designed. The motor-mount boss is
  the lowest point, and the main floor sits 8 mm above the bed (about
  5100 mm²), so it needs *support on build plate only* under the floor.
  Every other orientation was worse. Open side down bridges the whole floor,
  and printing it on its side puts large ceilings 60–70 mm up.

`print/make_plates.py` builds the plates from the per-part STLs. It only
translates parts, except those in its `FLIP` set (the shaft attachment),
which it turns 180° about X. That's a rotation, not a mirror, so each hand
is preserved.
Rerun it (`python3 hardware/gearbox/print/make_plates.py`) whenever the
part STLs change.

## Left and right hands

The housing and the lid each come in a left and a right hand, and each pair
is an exact mirror image. Print a matched pair, both left or both right; the
gears, bearings and shaft attachment are the same for either hand.

In Fusion, both designs use the same structure: each hand is its own
component, `Left` and `Right`. The `gear housing` design has them under
`Component1`; the lid has them at the root. The assembly shows the left hand
and hides `Right` in both.

The housing mirror was checked on the B-rep: identical volume, area and face
count, and 300 of 300 random sample points agree under the mirror.

## Re-exporting

If the Fusion design changes, export again from **Blinds gearbox assembly**.
STEP comes from the root component, so hidden `Right` bodies are left out.
Each STL comes from that part's single body, exported binary at high refinement. Each per-part `.f3d` comes from
`exportManager.createFusionArchiveExportOptions(path, occurrence.component)`.
Bump the version number and date at the top of this file.
