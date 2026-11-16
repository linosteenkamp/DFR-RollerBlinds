#!/usr/bin/env python3
"""Build the Bambu Lab X2D print plates from the per-part STLs in ../stl/.

Pure standard library. Run from anywhere:  python3 make_plates.py

Every part keeps the orientation it has in ../stl/ (its flat print face is
already its -Z face, see README "Printing"), so parts are only translated:
no rotation, no mirroring, and each hand is preserved. Parts sit 10 mm apart,
rows are stacked in Y, and the plate is centred on the 256 x 256 mm bed.
"""
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "stl"
GAP = 10.0                  # mm between parts
BED = (256.0, 256.0)        # X2D plate
SAFE = (235.0, 250.0)       # inside the dual-nozzle area (235.5 mm wide)

PLATES = {
    "x2d-gears.stl": [["driven-gear-60t", "idler-gear-36t-12t",
                       "shaft-attachment", "driver-gear-12t"]],
    "x2d-housing-left.stl": [["gear-housing-left"], ["gear-housing-lid-left"]],
    "x2d-housing-right.stl": [["gear-housing-right"], ["gear-housing-lid-right"]],
}


def load(path):
    data = path.read_bytes()
    (count,) = struct.unpack("<I", data[80:84])
    tris = []
    for i in range(count):
        v = struct.unpack("<12f", data[84 + i * 50:84 + i * 50 + 48])
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
         u[0] * w[1] - u[1] * w[0]]
    length = math.sqrt(sum(x * x for x in n)) or 1.0
    return [x / length for x in n]


def save(path, tris):
    out = bytearray(f"DFR-RollerBlinds gearbox plate {path.name}".encode()
                    .ljust(80, b" ")[:80])
    out += struct.pack("<I", len(tris))
    for a, b, c in tris:
        out += struct.pack("<12fH", *normal(a, b, c), *a, *b, *c, 0)
    path.write_bytes(out)


def bbox(tris):
    pts = [p for t in tris for p in t]
    return ([min(p[i] for p in pts) for i in range(3)],
            [max(p[i] for p in pts) for i in range(3)])


def shift(tris, dx, dy, dz):
    return [tuple((p[0] + dx, p[1] + dy, p[2] + dz) for p in t) for t in tris]


def build(name, rows):
    plate, y = [], 0.0
    for row in rows:
        x, row_depth = 0.0, 0.0
        for part in row:
            tris = load(SRC / f"{part}.stl")
            lo, hi = bbox(tris)
            plate += shift(tris, x - lo[0], y - lo[1], -lo[2])
            x += hi[0] - lo[0] + GAP
            row_depth = max(row_depth, hi[1] - lo[1])
        y += row_depth + GAP
    lo, hi = bbox(plate)
    w, d, h = (hi[i] - lo[i] for i in range(3))
    if w > SAFE[0] or d > SAFE[1]:
        raise SystemExit(f"{name}: {w:.1f} x {d:.1f} mm exceeds {SAFE}")
    plate = shift(plate, BED[0] / 2 - (lo[0] + w / 2),
                  BED[1] / 2 - (lo[1] + d / 2), 0.0)
    save(HERE / name, plate)
    print(f"{name}: {len(plate)} tris, {w:.1f} x {d:.1f} mm, {h:.1f} mm tall")


if __name__ == "__main__":
    for plate_name, plate_rows in PLATES.items():
        build(plate_name, plate_rows)
