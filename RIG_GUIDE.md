# Gearbox test rig guide

This guide tells you how to build, flash and use the test rig firmware.
The test rig measures the top speed and the wear of a gearbox unit. The
design is in the spec
[docs/superpowers/specs/2026-11-21-gearbox-test-rig-design.md](docs/superpowers/specs/2026-11-21-gearbox-test-rig-design.md)
(the master copy is issue #27).

The rig firmware is a separate PlatformIO env in this repository. It has
no Zigbee and no keypad. It uses the same `motion`, `ramp` and
`rf_switch` modules as the blind firmware.

## Status

This is the first version of the rig firmware (ticket #31). It has only
the serial console and the `move`, `stop` and `status` commands. The
driver UART, the encoder, the limit switch, MQTT and the test profiles
come in later tickets.

The driver is disabled after each move. Hold at idle stays off until the
firmware sets `IHOLD` through UART (ticket #33).

**WARNING:** Do not hang a load on the rig with this version. The driver
is disabled between the moves, and the load can turn the gearbox
backwards.

## Build and flash

1. Connect the XIAO ESP32C6 of the rig board to the Mac with a USB-C cable.
2. Build the rig firmware, flash it, and open the serial monitor:

   ```bash
   pio run -e seeed_xiao_esp32c6_rig -t upload -t monitor
   ```

3. Make sure that the monitor shows the line `test rig ready`.

The env `seeed_xiao_esp32c6_rig` sets two build values:

- `FW_APP=rig` selects the rig source files in `src/CMakeLists.txt`.
- `RAMP_MIN_CRUISE_US=40u` lowers the fastest step interval to 40 µs
  for the speed sweep. The blind keeps 60 µs. A build of the rig that
  does not get this flag stops with an error.

To put the blind firmware back on the board, flash the env
`seeed_xiao_esp32c6_zigbee`. Refer to [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

## Serial commands

Type a command in the serial monitor, then push Enter. The monitor
shows what you type (`monitor_echo = yes`).

| Command | Effect |
|---|---|
| `move <steps> <interval_us>` | Turn the motor by `<steps>` microsteps. A positive value turns in the + direction. A negative value turns in the − direction. `<interval_us>` is the step interval at cruise, from 40 to 1000 µs. |
| `stop` | Decelerate and stop the move. |
| `status` | Show the state (`idle` or `moving`) and the position in microsteps. |

The ramp is the same as on the blind: the first step is at 500 µs, and
the acceleration has 800 steps. At 1/8 microstep through the 1:15
reduction, one output revolution is 24 000 microsteps. The rig rejects a
move of more than 400 000 microsteps.

The words are case-sensitive. The rig rejects a command that is not
correct and tells you why.

## Output

The rig writes each record as one line: the topic, a space, and a JSON
object. Later, the MQTT adapter (ticket #36) sends the same records to
the broker.

| Topic | When | Fields |
|---|---|---|
| `rig/status` | At each change of state, and for `status` | `state`, `position` |
| `rig/move` | At the end of each move | `from`, `target`, `end`, `interval_us`, `completed` |
| `rig/reply` | The answer to a command | `ok`, `text` |

An example session:

```text
move 24000 150
rig/status {"state":"moving","position":0}
rig/move {"from":0,"target":24000,"end":24000,"interval_us":150,"completed":true}
rig/status {"state":"idle","position":24000}
move 100 20
rig/reply {"ok":false,"text":"move: interval_us must be 40..1000"}
```

`completed` is `false` when `stop` stopped the move before the target.

## Host tests

The command parser, `rig_ctl` and the record text have host tests in
`test/test_rig_ctl/`. Run all the host tests:

```bash
pio test -e native
```
