# Flash Partition Layout

## Overview

`partitions.csv` divides the 4 MB flash of the ESP32-C6 into firmware
slots, data storage and system regions. The layout is **dual-OTA**. It has
two application slots of equal size (`ota_0` and `ota_1`) and an `otadata`
selector. This layout permits Zigbee over-the-air updates with bootloader
rollback. Each offset has an explicit value (refer to
[Why offsets are explicit](#why-offsets-are-explicit)). The sibling
projects DFR-DoorSensor and DFR-MoistureTracker use the same table. Thus
all the devices in the family use the same OTA scheme.

## Partition Table

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,   0x9000,   0x6000,
phy_init,   data, phy,   0xf000,   0x1000,
otadata,    data, ota,   0x10000,  0x2000,
ota_0,      app,  ota_0, 0x20000,  0x180000,
ota_1,      app,  ota_1, 0x1a0000, 0x180000,
storage,    data, nvs,   0x320000, 0x4000,
zb_storage, data, fat,   0x324000, 0x4000,
zb_fct,     data, fat,   0x328000, 0x400,
```

## Partition Breakdown

| Partition | Type / SubType | Offset | Size | Purpose |
|-----------|----------------|--------|------|---------|
| `nvs` | data / nvs | 0x9000 | 24 KB | General NVS (`nvs_flash_init()`). It also holds the `blind` namespace of the application (span, position, `motor_reversed`, travel time, `move_in_progress`). |
| `phy_init` | data / phy | 0xf000 | 4 KB | RF (PHY) calibration data |
| `otadata` | data / ota | 0x10000 | 8 KB | Records which application slot (`ota_0` or `ota_1`) is active, and the verify state of each slot |
| `ota_0` | app / ota_0 | 0x20000 | 1.5 MB | Application slot A |
| `ota_1` | app / ota_1 | 0x1a0000 | 1.5 MB | Application slot B (OTA download target) |
| `storage` | data / nvs | 0x320000 | 16 KB | NVS partition kept for data in the future |
| `zb_storage` | data / fat | 0x324000 | 16 KB | Zigbee stack NVRAM (network keys, bindings) |
| `zb_fct` | data / fat | 0x328000 | 1 KB | Zigbee factory and production data |

To monitor the image size, use `pio run -t size`. If the image is larger
than the 1.5 MB slot, the OTA stops at the write stage.

## How OTA uses the slots

1. The device starts from the slot that `otadata` marks as active. At
   first, this is `ota_0`.
2. During an update, the device writes the new image into the
   **inactive** slot.
3. When the download is complete, the device sets the new slot as the
   boot target. Then it restarts from the new slot.
4. The new image stays `PENDING_VERIFY` until it starts correctly and
   gets to `app_main()`. Then `ota_client_mark_valid()` commits the image
   ("booted = good"). If the image crashes before this point, the
   bootloader goes back to the previous slot at the next start
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

For the full OTA procedure (release, rollout and rollback), refer to
[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

## Flash Memory Layout

```
┌──────────────────────┐ 0x000000
│ Bootloader           │
├──────────────────────┤ 0x008000
│ Partition Table      │
├──────────────────────┤ 0x009000
│ nvs (24 KB)          │ general NVS + app `blind` namespace
├──────────────────────┤ 0x00F000
│ phy_init (4 KB)      │ RF calibration
├──────────────────────┤ 0x010000
│ otadata (8 KB)       │ active-slot selector
├──────────────────────┤ 0x020000
│ ota_0 (1.5 MB)       │ app slot A
├──────────────────────┤ 0x1A0000
│ ota_1 (1.5 MB)       │ app slot B (OTA target)
├──────────────────────┤ 0x320000
│ storage (16 KB)      │ reserved NVS
├──────────────────────┤ 0x324000
│ zb_storage (16 KB)   │ Zigbee NVRAM
├──────────────────────┤ 0x328000
│ zb_fct (1 KB)        │ Zigbee factory data
├──────────────────────┤ 0x328400
│ Free                 │ ~0.7 MB of the 4 MB flash
└──────────────────────┘ 0x400000
```

## Why offsets are explicit

Each offset has a written value. If an offset is blank, the *upload*
offset parser of PlatformIO does not agree with the application alignment
of ESP-IDF. PlatformIO then tries to write the application at `0x10000`,
which is in `otadata` ("Detected overlap at address: 0x10000"). The build
is successful, but `-t upload` fails. When all offsets have a value,
PlatformIO and the IDF `flasher_args.json` agree.

## NVS / storage namespaces

| Namespace / partition | Partition | Keys / purpose |
|-----------------------|-----------|----------------|
| `blind` | `nvs` | `span_ok`/`span` (calibrated Open→Closed travel), `pos_ok`/`pos` (last known position), `rev` (`motor_reversed`), `spd` (full-travel time), `moving` (`move_in_progress`). Refer to [CLAUDE.md](CLAUDE.md). |
| (FAT, not NVS) | `zb_storage` / `zb_fct` | Network data and factory data. The Zigbee stack manages them. |
| (none) | `storage` | Kept for use in the future |

## Modifying partitions

[platformio.ini](platformio.ini) refers to the table:

```ini
board_build.partitions = partitions.csv
```

To apply a change:

1. Edit `partitions.csv`. Keep each offset explicit and aligned to 4 KB.
2. Run `pio run -t clean && pio run`.
3. After a partition change, the first flash needs a full erase and a new
   pairing. Run `pio run -t erase -t upload`. This erases the Zigbee
   network credentials. Then join the device to zigbee2mqtt again.

> **Warning**: a change to the partition table erases the stored data.
> You must then pair the device again.

## Troubleshooting

- **`app partition is too small`**: the image is larger than 1.5 MB. Make
  the build smaller (`-Os`, remove components), or make the two slots
  larger by the same amount.
- **`Detected overlap at address: 0x10000` on upload**: one or more
  offsets are blank. Put back the explicit offsets above.
- **`Partitions overlap` or not aligned**: keep each offset aligned to
  4 KB and in the 4 MB flash.
- **NVS init fails after a layout change**: run `pio run -t erase`, then
  flash again.

## References

- [ESP-IDF Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/partition-tables.html)
- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/ota.html)
- [NVS Flash](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/storage/nvs_flash.html)
