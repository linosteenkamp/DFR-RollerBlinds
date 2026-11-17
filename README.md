# DFR-RollerBlinds

**ESP32-C6 stepper-driven Zigbee-router roller blind controller**

> *Photo placeholder: the installed enclosure, motor and roller tube. Add
> the photo after bench verification.*

A Seeed XIAO ESP32C6 drives a bipolar stepper through a BIGTREETECH TMC2209
and a 3D-printed 1:15 reduction. The stepper raises and lowers a roller
blind. The StealthChop2 chopping of the driver makes the motor almost
silent. The device shows a standard **Window Covering** device
(cluster 0x0102) to zigbee2mqtt and Home Assistant.

The device joins the mesh as a **Zigbee Router**. It uses mains power, and
its radio is always on. It relays traffic and extends the range of the
network. A membrane keypad with three keys (Up, Down and Fn) gives full
local control and a guided calibration procedure. zigbee2mqtt sends
firmware updates over the air.

## Features

- **Window Covering cluster (0x0102)**: full open, full close, stop, and
  go to a percentage. The device reports the lift position during each
  move.
- **Local keypad control**: tap a key for full travel. Hold a key to jog.
  Jog operates before calibration and when Zigbee is not available.
- **Guided calibration**: hold Fn for approximately 3 s, then jog to the
  two limits. The LED shows the status at each step.
- **Position Unknown recovery**: the device finds a power failure that
  occurred during a move. A short Re-home with one mark restores the
  position. A full calibration is not necessary.
- **Motor Reversed**: an installer setting for each unit, set from z2m.
  It corrects the direction for front-roll and back-roll installations.
- **Zigbee Router**: the radio is always on. The device relays traffic
  and extends the mesh. The shared `esp-zb-common` library supplies the
  Zigbee code.
- **Zigbee OTA**: CI builds the images, and zigbee2mqtt sends them. The
  bootloader has two slots and can roll back to the old image.
- **Core logic tested on the host**: the position calculations, the ramp
  planning and the keypad gesture classification are pure-C modules. A
  `native` unit-test suite tests them.

## Getting started

```bash
# Build & flash
pio run -e seeed_xiao_esp32c6_zigbee -t upload -t monitor

# Host unit tests
pio test -e native
```

For pairing, calibration and OTA setup, refer to
[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

> **Do you install a unit on a blind?** Use the
> [per-blind install checklist](HARDWARE.md#installing-a-unit-per-blind-checklist).
> The fleet uses **two different motors**. Each motor needs its own Vref
> *and* its own motor cable. You cannot interchange the cables. If the
> Vref or the cable is incorrect, the motor stalls. This stall looks like
> a firmware problem or a speed problem.

## Documentation

- **[HARDWARE.md](HARDWARE.md)**: the BOM, the power chain, the TMC2209
  wiring, the Vref procedure, the keypad and LED wiring, the direction
  check, and the
  [per-blind install checklist](HARDWARE.md#installing-a-unit-per-blind-checklist)
- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)**: pairing, the calibration
  procedure, the bench checklist and the OTA release procedure
- **[CLAUDE.md](CLAUDE.md)**: the architecture, the module table and the
  concurrency rule
- **[PARTITIONS.md](PARTITIONS.md)**: the flash partition layout (dual OTA)
- **[docs/superpowers/specs/2026-07-18-roller-blinds-design.md](docs/superpowers/specs/2026-07-18-roller-blinds-design.md)**:
  the full design specification

## License

The firmware and the code are under the PolyForm Noncommercial License
1.0.0. `hardware/` and `docs/` are under CC BY-NC-SA 4.0. You can use, change
and share this work for noncommercial purposes. Commercial use is not
permitted. Refer to [LICENSE.md](LICENSE.md).
