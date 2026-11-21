#!/usr/bin/env python3
"""Run back-and-forth cycles on the test rig through its serial console.

Each cycle is one move forward and one move back by the same number of
output revolutions. The script waits for the end of each move, and it
stops at the first problem: a move that is not complete, a rejected
command, or a position that does not come back to the start.

Usage (close the serial monitor first: only one program can use the port):

    ~/.platformio/penv/bin/python tools/rig_cycle.py
    ~/.platformio/penv/bin/python tools/rig_cycle.py --cycles 50 --revs 10 --interval 100

Push Ctrl+C to stop. The script then sends `stop`, and the motor
decelerates.

The firmware counts steps with no feedback. A move is "complete" when the
firmware sent all the steps, also if the motor stalled. Watch the mark.
"""
import argparse
import glob
import json
import sys
import time

import serial

STEPS_PER_REV = 24000   # 1/8 microstep through the 1:15 reduction
HARD_CAP = 400000       # the rig rejects longer moves (src/rig_main.c)


def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No /dev/cu.usbmodem* port. Connect USB first, then 24 V.")
    return ports[0]


class Rig:
    def __init__(self, port: str):
        self.s = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(1.0)
        self.s.reset_input_buffer()
        self.buf = ""

    def send(self, cmd: str) -> None:
        self.s.write((cmd + "\n").encode())
        self.s.flush()

    def lines(self, timeout: float):
        """Return the received lines, one at a time, until timeout."""
        end = time.time() + timeout
        while time.time() < end:
            self.buf += self.s.read(2000).decode(errors="replace")
            while "\n" in self.buf:
                line, self.buf = self.buf.split("\n", 1)
                line = line.strip()
                if line:
                    yield line
        raise TimeoutError("no answer from the rig")

    def status(self) -> dict:
        self.send("status")
        for line in self.lines(3):
            if line.startswith("rig/status "):
                return json.loads(line.split(" ", 1)[1])

    def move(self, steps: int, interval_us: int) -> dict:
        """Run one move. Return the rig/move record."""
        self.send(f"move {steps} {interval_us}")
        # The cruise time, with 50 % and 10 s margin.
        timeout = abs(steps) * interval_us / 1e6 * 1.5 + 10
        record = None
        for line in self.lines(timeout):
            if line.startswith("rig/reply "):
                reply = json.loads(line.split(" ", 1)[1])
                if not reply["ok"]:
                    raise RuntimeError(f"rejected: {reply['text']}")
            elif line.startswith("rig/move "):
                record = json.loads(line.split(" ", 1)[1])
            elif line.startswith("rig/status ") and record is not None:
                return record

    def stop(self) -> None:
        self.send("stop")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", type=int, default=50)
    ap.add_argument("--revs", type=float, default=10, help="output revolutions for each move")
    ap.add_argument("--interval", type=int, default=100, help="step interval in µs (40..1000)")
    ap.add_argument("--pause", type=float, default=30.0,
                    help="seconds of rest between the moves (the motor cools; the spec uses 30 s)")
    ap.add_argument("--port", default=None)
    args = ap.parse_args()

    steps = round(args.revs * STEPS_PER_REV)
    if not 0 < steps <= HARD_CAP:
        sys.exit(f"--revs gives {steps} steps; the rig accepts 1..{HARD_CAP} for each move")

    rig = Rig(args.port or find_port())
    start = rig.status()
    if start is None or start["state"] != "idle":
        sys.exit(f"the rig is not idle: {start}")
    home = start["position"]
    move_s = steps * args.interval / 1e6
    total_min = args.cycles * 2 * (move_s + args.pause) / 60
    print(f"{args.cycles} cycles of ±{args.revs:g} rev at {args.interval} µs, "
          f"from position {home}. Approximately {total_min:.0f} min. Ctrl+C stops.")

    t_run = time.time()
    try:
        for n in range(1, args.cycles + 1):
            times = []
            for direction in (+1, -1):
                t0 = time.time()
                rec = rig.move(direction * steps, args.interval)
                times.append(time.time() - t0)
                if not rec["completed"]:
                    print(f"cycle {n}: move not complete: {rec}")
                    return 1
                time.sleep(args.pause)
            pos = rig.status()["position"]
            print(f"cycle {n:3d}/{args.cycles}: {times[0]:.1f} s + {times[1]:.1f} s, "
                  f"position {pos}")
            if pos != home:
                print(f"position is {pos}, not {home}: stop")
                return 1
    except KeyboardInterrupt:
        rig.stop()
        print("\nstopped: `stop` sent")
        return 130
    except (RuntimeError, TimeoutError) as e:
        rig.stop()
        print(f"stopped: {e}")
        return 1
    print(f"done: {args.cycles} cycles in {(time.time() - t_run) / 60:.1f} min")
    return 0


if __name__ == "__main__":
    sys.exit(main())
