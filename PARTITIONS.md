# Flash Partition Layout

## Overview

`partitions.csv` divides the ESP32-C6's 4 MB flash into firmware slots, data
storage, and system regions. The layout is **dual-OTA**: two equal application
slots (`ota_0` / `ota_1`) plus an `otadata` selector, which is what enables Zigbee
over-the-air updates with bootloader rollback. Offsets are pinned explicitly (see
[Why offsets are explicit](#why-offsets-are-explicit)). The table is shared with the
sibling DFR-DoorSensor and DFR-MoistureTracker projects so the whole family uses the
same OTA scheme.

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
| `nvs` | data / nvs | 0x9000 | 24 KB | General NVS (`nvs_flash_init()`); also backs the app's own `blind` namespace (span, position, `motor_reversed`, `move_in_progress`) |
| `phy_init` | data / phy | 0xf000 | 4 KB | RF (PHY) calibration data |
| `otadata` | data / ota | 0x10000 | 8 KB | Records which app slot (`ota_0`/`ota_1`) is active + each slot's verify state |
| `ota_0` | app / ota_0 | 0x20000 | 1.5 MB | Application slot A |
| `ota_1` | app / ota_1 | 0x1a0000 | 1.5 MB | Application slot B (OTA download target) |
| `storage` | data / nvs | 0x320000 | 16 KB | Reserved NVS partition for future data |
| `zb_storage` | data / fat | 0x324000 | 16 KB | Zigbee stack NVRAM (network keys, bindings) |
| `zb_fct` | data / fat | 0x328000 | 1 KB | Zigbee factory/production data |

Monitor image growth with `pio run -t size`; exceeding the 1.5 MB slot size aborts an
OTA at the write stage.

## How OTA uses the slots

1. The device boots from the slot `otadata` marks active (initially `ota_0`).
2. During an update it writes the new image into the **inactive** slot.
3. On completion it sets the new slot as the boot target and reboots into it.
4. The new image stays `PENDING_VERIFY` until it boots cleanly to `app_main()`, at
   which point `ota_client_mark_valid()` commits it ("booted = good"). If it crashes
   before then, the bootloader rolls back to the previous slot on the next boot
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

Full OTA procedure (release, rollout, rollback) is in
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

Offsets are written out rather than left blank because PlatformIO's *upload* offset
parser otherwise diverges from ESP-IDF's app alignment and tries to flash the app at
`0x10000`, colliding with `otadata` ("Detected overlap at address: 0x10000"). The
build succeeds but `-t upload` fails. Pinning every offset keeps PlatformIO and the
IDF `flasher_args.json` in agreement.

## NVS / storage namespaces

| Namespace / partition | Partition | Keys / purpose |
|-----------------------|-----------|----------------|
| `blind` | `nvs` | `span_ok`/`span` (calibrated Open→Closed travel), `pos_ok`/`pos` (last known position), `rev` (`motor_reversed`), `moving` (`move_in_progress`) — see [CLAUDE.md](CLAUDE.md) |
| (FAT, not NVS) | `zb_storage` / `zb_fct` | Zigbee stack-managed network / factory data |
| (none) | `storage` | reserved for future use |

## Modifying partitions

Referenced from [platformio.ini](platformio.ini):

```ini
board_build.partitions = partitions.csv
```

To apply changes:

1. Edit `partitions.csv` (keep offsets explicit and 4 KB-aligned).
2. `pio run -t clean && pio run`
3. The first flash after a partition change needs a full erase + re-pair:
   `pio run -t erase -t upload` — this wipes the Zigbee network credentials, so the
   device must re-join zigbee2mqtt afterwards.

> **Warning**: changing the partition table erases stored data and forces a re-pair.

## Troubleshooting

- **`app partition is too small`** — the image exceeds 1.5 MB; trim the build
  (`-Os`, drop components) or enlarge both slots symmetrically.
- **`Detected overlap at address: 0x10000` on upload** — offsets went blank; restore
  the explicit offsets above.
- **`Partitions overlap` / unaligned** — keep offsets 4 KB-aligned and within 4 MB.
- **NVS init fails after a layout change** — `pio run -t erase`, then re-flash.

## References

- [ESP-IDF Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/partition-tables.html)
- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/ota.html)
- [NVS Flash](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/storage/nvs_flash.html)
