# Diagnostic trace: a fault log that survives the reset

**Date:** 2026-08-11
**Status:** approved, not yet implemented
**Repos:** `DFR-RollerBlinds` only

## Problem

An afternoon of bench debugging on 2026-08-11 failed to identify why the
keypad's Down key intermittently stopped moving the blind. Not for lack of
data — for lack of data that *survived being looked at*.

Every tool available resets the board:

- attaching to the serial port asserts DTR/RTS, which resets an ESP32-C6;
- flashing an instrumented build resets it by definition.

And the fault is cleared by a reset. Three separate times, the act of
observing destroyed the state being observed. Once, the reset also corrupted
`zb_storage` and the device came up factory-new, costing a re-pair.

The result was a sequence of confident, wrong diagnoses — the hardware glitch
filter, then swapped keypad wiring, then `s_raw` drift against the hard cap —
each plausible, each eliminated only after more of the user's time, and none
provable because the evidence evaporated on contact.

Two further gaps compounded it:

- **The firmware logs almost nothing.** A refused move logs to a serial
  console nobody is attached to; a no-op move logs nothing at all. From the
  keypad there is no feedback whatsoever, so "refused", "already there" and
  "dead" are indistinguishable to the operator.
- **The device is open-loop.** z2m position reports prove the firmware ran a
  move, never that the motor turned — so the one durable log that does exist
  cannot answer why.

## Goal

A fault that occurs before a reset is still readable after it.

## Non-goals

- **Surviving power removal.** RTC memory is cleared when power is actually
  lost. Accepted deliberately: the fault being chased is reset-cleared, and
  z2m's log (on a separate machine) is the durable half for anything else.
  Revisit only if a power-cut-immune soak becomes the primary use.
- **Remote readout over Zigbee.** Would need a new attribute or command, a
  converter change, and this project has already hit two attribute-layer
  defects in the prebuilt ZBOSS stack. Serial is sufficient for a developer
  tool.
- **A keypad gesture to re-dump.** The gesture matrix is already dense, and
  gestures are among the things being debugged.
- **Deferred printf / variable-length records.** Rejected as complexity: more
  RAM, formatting in a path that must stay callable from any context.
- **Fixing the Down-key fault itself.** This spec builds the instrument. The
  fault is still undiagnosed and is deliberately left that way rather than
  guessed at a fourth time.

## Approach

A fixed-size ring of small fixed-size records in `RTC_NOINIT` memory, dumped
to the console at boot.

`RTC_NOINIT` survives resets — including watchdog, panic, brownout and the
DTR/RTS reset that serial attachment causes — while costing nothing to write:
a couple of stores, no flash, no allocation, no blocking lock. The reset stops
being the thing that destroys the evidence and becomes the thing that prints
it.

Considered and rejected:

- **NVS in the spare `storage` partition** (durable across power loss). Every
  event becomes a flash write: wear, plus write latency in `dispatcher_task`,
  the task that must not stall during a move.
- **Typed per-subsystem decision structs.** Prettier output, but every new
  thing to trace means a new variant and edits to the dump code — rigid
  exactly where extensibility was the requirement.

## Design

### 1. Record and storage

```c
typedef struct {
    uint32_t t_ms;   /* uptime when recorded */
    uint16_t code;   /* trace_code_t */
    uint16_t seq;    /* monotonic; retained records are contiguous, never gapped */
    int32_t  a, b;   /* payload, meaning depends on code */
} trace_rec_t;       /* 16 bytes */
```

`TRACE_DEPTH` = **256** → 4 KB of the ~15 KB free RTC RAM. At roughly three
records per movement that is ~85 movements, enough to cover a 48-hour soak
with headroom. An idle blind emits nothing — records mark events, never polls.

The ring lives in one `RTC_NOINIT_ATTR trace_ring_t s_ring;` instance.

**Cold-boot validity is the crux.** `RTC_NOINIT` memory is genuinely
uninitialised at power-on: it holds garbage, not zeros. `trace_init()` tests a
magic word; mismatch means cold boot, so clear and stamp the magic. Get this
wrong and the first dump prints random noise indistinguishable from data,
which is worse than having no tool.

**The ring is never cleared after dumping.** Each boot appends a `TRC_BOOT`
marker and continues, so one dump shows both sides of a reset with the
boundary labelled — precisely the view that was missing.

### 2. Module split

| File | Responsibility |
|---|---|
| `src/trace_ring.c`, `include/trace_ring.h` | Pure ring: validity, reset, push, indexed read. No ESP-IDF. Host-tested. |
| `src/trace.c`, `include/trace.h` | ESP glue: the `RTC_NOINIT` instance, critical section, `ESP_LOGI` dump, `esp_reset_reason()`. |

This matches the seam the codebase already uses — `position` / `ramp` /
`keypad_logic` pure and host-tested, `keypad` / `motion` as glue — and is what
makes the ring testable at all, since `RTC_NOINIT_ATTR` and
`portENTER_CRITICAL_SAFE` do not exist on the host.

`trace_rec_t` and the ring live in `trace_ring.h`, which includes only
`<stdint.h>` / `<stdbool.h>`:

```c
typedef struct {
    uint32_t    magic;
    uint32_t    head;            /* next write index */
    uint32_t    count;           /* entries held, saturates at TRACE_DEPTH */
    uint16_t    seq;             /* next sequence number */
    trace_rec_t rec[TRACE_DEPTH];
} trace_ring_t;

bool     trace_ring_valid(const trace_ring_t *r, uint32_t magic);
void     trace_ring_reset(trace_ring_t *r, uint32_t magic);
void     trace_ring_push(trace_ring_t *r, uint32_t t_ms, uint16_t code,
                         int32_t a, int32_t b);
uint32_t trace_ring_count(const trace_ring_t *r);
/* i == 0 is the oldest retained record; NULL if i >= count. */
const trace_rec_t *trace_ring_at(const trace_ring_t *r, uint32_t i);
```

The array is inline rather than behind a pointer, so the whole structure is a
single `RTC_NOINIT` object on target and a plain local in host tests.

### 3. Interface

```c
#ifndef TRACE_ENABLED
#define TRACE_ENABLED 1
#endif
#define TRACE_DEPTH 256

typedef enum {
    TRC_BOOT = 1,      /* a = esp_reset_reason()    b = —            */
    TRC_KEY_EVENT,     /* a = kp_event_type_t       b = key_id_t     */
    TRC_MOVE_START,    /* a = from_steps            b = to_steps     */
    TRC_MOVE_NOOP,     /* a = target                b = s_raw        */
    TRC_MOVE_REFUSED,  /* a = esp_err_t             b = hard_cap     */
    TRC_MOVE_DONE,     /* a = steps                 b = completed    */
    TRC_ZB_CMD,        /* a = pct           b = 1 if parked pending  */
    TRC_QUEUE_FULL,    /* a = dropped event type    b = —            */
} trace_code_t;

void trace_init(void);   /* magic check; clear only if cold. Emits nothing. */
void trace_dump(void);   /* print oldest -> newest */
void trace_emit(uint16_t code, int32_t a, int32_t b);

#if TRACE_ENABLED
#  define TRACE(c, a, b) trace_emit((c), (a), (b))
#else
#  define TRACE(c, a, b) ((void)0)
#endif
```

`trace_emit` wraps its stores in `portENTER_CRITICAL_SAFE`, making it callable
from task context, the `esp_timer` callback and the Zigbee stack callback
alike, at a cost of about a microsecond. No allocation, no flash, no lock that
can block, nothing that can stall a move.

### 4. Call sites

Almost all in `main.c`, deliberately: this project's concurrency rule is that
the ISR and the Zigbee action handler never decide anything, and every
decision happens in `dispatcher_task`. Tracing there records *decisions*
rather than noise.

| Where | Record |
|---|---|
| `handle_keypad()` entry | `TRC_KEY_EVENT(e.type, e.key)` |
| `start_move()` no-op branch | `TRC_MOVE_NOOP(target, s_raw)` |
| `start_move()` before `motion_start` | `TRC_MOVE_START(s_raw, target)` |
| `start_move()` on error | `TRC_MOVE_REFUSED(err, cap)` |
| `APP_EVT_MOTION_DONE` | `TRC_MOVE_DONE(ev.steps, ev.completed)` |
| `zb_goto_request()` | `TRC_ZB_CMD(type, pct)` |
| `post_kp()` queue-full path (`keypad.c`) | `TRC_QUEUE_FULL(e.type)` |

`start_move()` currently passes `hard_cap()` inline as an argument; hoist it
to a local so the refusal record can carry it. Those two numbers — distance
and cap — are exactly what could not be seen on 2026-08-11.

`TRC_QUEUE_FULL` is the single record outside the dispatcher, because a
dropped event is the one thing the dispatcher cannot observe. It was a
dead-end hypothesis that afternoon and should be falsifiable next time.

### 5. Boot sequence

First thing in `app_main`, before anything can emit:

```c
trace_init();                              /* magic check; clear only if cold */
trace_dump();                              /* history from BEFORE this reset */
TRACE(TRC_BOOT, esp_reset_reason(), 0);    /* then mark the new session */
```

Dumping before recording the boot marker keeps the dump purely the previous
session. `esp_reset_reason()` in `TRC_BOOT` answers a question that was
unanswerable on 2026-08-11: whether a reboot was a brownout, a panic, a
watchdog, or an incidental serial-attach reset.

### 6. Dump format

Uniform, one line per record, oldest first:

```
TRACE: 42 records, seq 118..159
TRACE: [   412330] KEY_EVENT     a=1 b=1
TRACE: [   412330] MOVE_REFUSED  a=-258 b=347400
TRACE: [   498102] BOOT          a=3 b=0
```

Code names come from a small static table; `a`/`b` print as signed decimals
and are read against the enum comments above. This keeps the emit path trivial
and the dump code tiny, at the cost of reading the output with the header to
hand — an acceptable trade for a developer tool.

### 7. The flag

`TRACE_ENABLED` defaults to 1; `-DTRACE_ENABLED=0` compiles out both the call
sites and the RTC arrays.

**It ships on**, and that is the point. A trace compiled out by default could
never catch this class of bug: enabling it means reflashing, reflashing resets
the board, and the reset clears the fault. The instrument would arrive exactly
when the thing to measure has gone. The flag exists so the facility can be
stripped if RAM or flash ever gets tight, not as a normal operating mode.

## Testing

New host suite `test/test_trace_ring/`, added to the `native` env's
`test_filter`:

1. **Uninitialised ring (wrong magic) → not valid.** The cold-boot-garbage
   case; the most important test here.
2. After reset → valid, count 0.
3. Pushes below depth → count rises, `at(0)` is oldest.
4. Pushes past depth → count saturates at `TRACE_DEPTH`, oldest dropped,
   newest retained.
5. `seq` monotonic across the wrap. Retained records are always contiguous —
   a gap can never appear — so this only confirms ordering, not wrap detection.
6. Reset clears the count but preserves the magic.

The four existing suites must pass unchanged.

**Not reachable by host tests:** the `RTC_NOINIT` placement itself, the
critical section, and whether the contents genuinely survive a reset on
silicon. Bench acceptance: flash, press a key, reset via serial attach, and
confirm the boot dump shows the pre-reset `KEY_EVENT`.

## Honest limitations

- **Lost on power removal.** RTC RAM is cleared when power is actually cut.
  For a soak, pair it with z2m's log: z2m survives power loss and shows *that*
  the blind moved; the trace shows *why*, and is lost if power drops.
- **256 records.** Sustained activity wraps the ring, silently dropping the
  oldest records; retained records stay contiguous, so there is no `seq` gap
  to notice. A wrap is visible instead as `count == TRACE_DEPTH`, or as a
  header whose first record's `seq` is not 0.
- **Uptime, not wall clock.** Correlating with z2m's wall-clock log means
  noting current uptime at read time and subtracting.
- **Records decisions, not causes.** If an event never reaches the dispatcher,
  the trace shows its absence — informative, but not why it was absent.

## What this would have shown on 2026-08-11

One press, two lines, instead of an afternoon:

```
[ 412330 ms] KEY_EVENT     a=TAP b=DOWN
[ 412330 ms] MOVE_REFUSED  a=ESP_ERR_INVALID_ARG b=347400
```

or, had the cause been different:

```
[ 412330 ms] KEY_EVENT     a=TAP b=DOWN
[ 412330 ms] MOVE_NOOP     target=345000 raw=345000
```

And had *neither* appeared, that would itself have proved the event never
reached the dispatcher — eliminating in one press the hypothesis that cost
the most time.
