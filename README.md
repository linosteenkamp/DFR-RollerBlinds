# DFR-RollerBlinds

**ESP32-C6 stepper-driven Zigbee-router roller blind controller**

> *Photo placeholder — installed enclosure + motor + roller tube, to be added
> after bench verification.*

A Seeed XIAO ESP32C6 that drives a bipolar stepper (through a BIGTREETECH
TMC2209 and a 3D-printed 1:15 reduction) to raise and lower a roller blind,
in near silence thanks to the driver's StealthChop2 chopping, and presents a
standard **Window Covering** device (cluster 0x0102) to zigbee2mqtt / Home
Assistant. It joins the mesh as a **Zigbee Router** — mains-powered,
always-on, relays traffic and extends network range. A 3-key membrane keypad
(Up / Down / Fn) gives full local control and hosts a guided calibration
flow; firmware updates are delivered over the air through zigbee2mqtt.

## Features

- **Window Covering cluster (0x0102)** — full open/close, stop, and
  go-to-percentage, with live lift-position reporting during moves
- **Local keypad control** — tap for full travel, hold to jog, works even
  before calibration or with Zigbee down
- **Guided calibration** — hold Fn ~3 s, jog to the two limits, done; LED
  feedback at every step
- **Position Unknown recovery** — a power cut mid-move is detected and
  recovered with a lightweight one-mark Re-home instead of full recalibration
- **Motor Reversed** — per-unit installer setting (keypad chord or z2m),
  correcting for front-roll vs back-roll installs
- **Zigbee Router** — always-on radio, relays traffic and extends the mesh;
  built on the shared `esp-zb-common` library
- **Zigbee OTA** — CI-built images delivered through zigbee2mqtt with
  dual-slot bootloader rollback
- **Host-testable core** — position math, ramp planning, and keypad gesture
  classification are pure-C modules with a `native` unit-test suite

## Getting started

```bash
# Build & flash
pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor

# Host unit tests
pio test -e native
```

Pairing, calibration, and OTA setup are in
[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

> **Fitting a unit to a blind?** Follow the
> [per-blind install checklist](HARDWARE.md#installing-a-unit-per-blind-checklist).
> The fleet uses **two different motors**, and each needs its own Vref *and* its
> own motor cable — the cables are not interchangeable, and mismatching either
> one produces a stall that looks like a firmware or speed problem.

## Documentation

- **[HARDWARE.md](HARDWARE.md)** — BOM, power chain, TMC2209 wiring, Vref
  procedure, keypad/LED wiring, direction check, and the
  [per-blind install checklist](HARDWARE.md#installing-a-unit-per-blind-checklist)
- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** — pairing, calibration
  walkthrough, bench checklist, OTA release flow
- **[CLAUDE.md](CLAUDE.md)** — architecture, module table, concurrency rule
- **[PARTITIONS.md](PARTITIONS.md)** — flash partition layout (dual-OTA)
- **[docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md)** — full design spec

## License

Provided as-is for educational and development purposes.
