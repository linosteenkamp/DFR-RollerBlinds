# Diagnostic Trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record firmware decisions into RTC memory so a fault that occurs before a reset is still readable after it.

**Architecture:** A fixed ring of 16-byte records lives in `RTC_NOINIT` memory, which survives every reset (watchdog, panic, brownout, and the DTR/RTS reset caused by attaching a serial cable) but not power removal. Pure ring logic sits in `trace_ring.c` and is host-tested; ESP-specific storage, locking and printing sit in `trace.c`. The dispatcher emits records at each decision point, and `app_main` dumps the previous session's ring at boot before marking the new one.

**Tech Stack:** C11, ESP-IDF 5.5.1 via PlatformIO, Unity host tests on the `native` env, ESP32-C6 (`seeed_xiao_esp32c6`).

**Spec:** [docs/superpowers/specs/2026-08-11-diagnostic-trace-design.md](../specs/2026-08-11-diagnostic-trace-design.md)

## Global Constraints

- C11. `src/trace_ring.c` must be pure: no ESP-IDF headers, no FreeRTOS, nothing beyond `<stdint.h>` / `<stdbool.h>`. It is the only part host tests can reach.
- Host tests are Unity via `pio test -e native`. That env sets `build_src_filter = -<*>`, so test files `#include "../../src/<file>.c"` directly — the established project pattern (see `test/test_keypad/test_keypad.c`).
- `TRACE_DEPTH` = **256**. `TRACE_MAGIC` = **`0x54524331u`**. Both load-bearing.
- `TRACE_ENABLED` defaults to **1**. It ships on; the flag exists only to strip the facility if RAM or flash gets tight. When 0, the call sites and the RTC array vanish but `trace_init`/`trace_dump`/`trace_emit` must still link as no-ops, because `app_main` calls them unconditionally.
- Do NOT modify `esp-zb-common`, `src/key_filter.c`, `src/keypad_logic.c`, `src/position.c`, `src/ramp.c`, or any existing test file.
- Do NOT attempt to fix the Down-key fault. This plan builds the instrument only.
- New source files must be added to `src/CMakeLists.txt`'s explicit `SRCS` list or the firmware will not link.
- Commit messages are prose explaining *why*, not bullet lists.
- All work on branch **`diagnostic-trace`**, cut from `main` at the start of Task 1, merged `--no-ff` in Task 4. Do not push or tag.

## File Structure

| File | Responsibility |
|---|---|
| `include/trace_ring.h` (create) | Record and ring types, pure ring interface |
| `src/trace_ring.c` (create) | Ring logic: validity, reset, push, indexed read. No ESP-IDF |
| `test/test_trace_ring/test_trace_ring.c` (create) | Unity suite for the ring, cold-boot garbage case first |
| `include/trace.h` (create) | `trace_code_t` enum, `TRACE()` macro, glue interface |
| `src/trace.c` (create) | `RTC_NOINIT` instance, critical section, `ESP_LOGI` dump |
| `platformio.ini` (modify) | Register `test_trace_ring` in the `native` env |
| `src/CMakeLists.txt` (modify) | Add both new sources to `SRCS` |
| `src/main.c` (modify) | Boot sequence + six call sites in the dispatcher |
| `src/keypad.c` (modify) | One call site in `post_kp`'s queue-full path |
| `CLAUDE.md` (modify) | Module table rows for `trace` and `trace_ring` |

---

### Task 1: The `trace_ring` pure module

**Files:**
- Create: `include/trace_ring.h`, `src/trace_ring.c`
- Test: `test/test_trace_ring/test_trace_ring.c`
- Modify: `platformio.ini`

**Interfaces:**
- Consumes: nothing — leaf module.
- Produces: `trace_rec_t` (fields `t_ms`, `code`, `seq`, `a`, `b`), `trace_ring_t`, and `trace_ring_valid` / `trace_ring_reset` / `trace_ring_push` / `trace_ring_count` / `trace_ring_at`. Task 2 depends on these exact names.

- [ ] **Step 0: Cut the branch**

```bash
git checkout main
git checkout -b diagnostic-trace
```

- [ ] **Step 1: Create the header**

Create `include/trace_ring.h`:

```c
#ifndef TRACE_RING_H
#define TRACE_RING_H

#include <stdbool.h>
#include <stdint.h>

#ifndef TRACE_DEPTH
#define TRACE_DEPTH 256
#endif

/* One traced decision. 16 bytes; meaning of a/b depends on code. */
typedef struct {
    uint32_t t_ms;   /* uptime when recorded */
    uint16_t code;   /* trace_code_t, see trace.h */
    uint16_t seq;    /* monotonic; a gap in the dump means the ring wrapped */
    int32_t  a, b;
} trace_rec_t;

/* The array is inline rather than behind a pointer so the whole structure is
 * a single RTC_NOINIT object on target and a plain local in host tests. */
typedef struct {
    uint32_t    magic;
    uint32_t    head;   /* next write index */
    uint32_t    count;  /* entries held; saturates at TRACE_DEPTH */
    uint16_t    seq;    /* next sequence number */
    trace_rec_t rec[TRACE_DEPTH];
} trace_ring_t;

/* True only if the ring holds real data. RTC_NOINIT memory is genuinely
 * uninitialised at power-on — garbage, not zeros — so this must reject a
 * cold boot or the first dump prints noise that looks exactly like records. */
bool trace_ring_valid(const trace_ring_t *r, uint32_t magic);

void trace_ring_reset(trace_ring_t *r, uint32_t magic);

void trace_ring_push(trace_ring_t *r, uint32_t t_ms, uint16_t code,
                     int32_t a, int32_t b);

uint32_t trace_ring_count(const trace_ring_t *r);

/* i == 0 is the oldest retained record; NULL if i >= count. */
const trace_rec_t *trace_ring_at(const trace_ring_t *r, uint32_t i);

#endif /* TRACE_RING_H */
```

- [ ] **Step 2: Write the failing test suite**

Create `test/test_trace_ring/test_trace_ring.c`:

```c
#include <unity.h>
#include <string.h>
#include "../../src/trace_ring.c"

void setUp(void) {}
void tearDown(void) {}

#define MAGIC 0x54524331u

static trace_ring_t r;

/* The cold-boot case. RTC_NOINIT holds garbage at power-on, and if this
 * passes validation the very first dump prints noise indistinguishable
 * from real data — worse than having no tool at all. */
static void test_uninitialised_ring_is_not_valid(void)
{
    memset(&r, 0xA5, sizeof r);
    TEST_ASSERT_FALSE(trace_ring_valid(&r, MAGIC));
}

/* Garbage that happens to collide with the magic must still be rejected
 * on its out-of-range bookkeeping. */
static void test_right_magic_but_bogus_indices_is_not_valid(void)
{
    memset(&r, 0xA5, sizeof r);
    r.magic = MAGIC;
    TEST_ASSERT_FALSE(trace_ring_valid(&r, MAGIC));
}

static void test_reset_makes_valid_and_empty(void)
{
    memset(&r, 0xA5, sizeof r);
    trace_ring_reset(&r, MAGIC);
    TEST_ASSERT_TRUE(trace_ring_valid(&r, MAGIC));
    TEST_ASSERT_EQUAL_UINT32(0, trace_ring_count(&r));
    TEST_ASSERT_NULL(trace_ring_at(&r, 0));
}

static void test_push_below_depth_orders_oldest_first(void)
{
    trace_ring_reset(&r, MAGIC);
    trace_ring_push(&r, 10, 1, 100, 200);
    trace_ring_push(&r, 20, 2, 300, 400);
    trace_ring_push(&r, 30, 3, 500, 600);

    TEST_ASSERT_EQUAL_UINT32(3, trace_ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(10, trace_ring_at(&r, 0)->t_ms);
    TEST_ASSERT_EQUAL_UINT32(30, trace_ring_at(&r, 2)->t_ms);
    TEST_ASSERT_EQUAL_INT32(500, trace_ring_at(&r, 2)->a);
    TEST_ASSERT_NULL(trace_ring_at(&r, 3));
}

static void test_push_past_depth_saturates_and_drops_oldest(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH + 5; i++) {
        trace_ring_push(&r, i, 1, (int32_t)i, 0);
    }
    TEST_ASSERT_EQUAL_UINT32(TRACE_DEPTH, trace_ring_count(&r));
    /* oldest surviving push is number 5 */
    TEST_ASSERT_EQUAL_INT32(5, trace_ring_at(&r, 0)->a);
    TEST_ASSERT_EQUAL_INT32(TRACE_DEPTH + 4,
                            trace_ring_at(&r, TRACE_DEPTH - 1)->a);
}

/* A wrapped ring must still read oldest-first, not from index 0. */
static void test_wrapped_ring_reads_in_chronological_order(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH * 2; i++) {
        trace_ring_push(&r, i, 1, (int32_t)i, 0);
    }
    for (uint32_t i = 1; i < trace_ring_count(&r); i++) {
        TEST_ASSERT_TRUE(trace_ring_at(&r, i)->t_ms >
                         trace_ring_at(&r, i - 1)->t_ms);
    }
}

static void test_seq_is_monotonic_across_wrap(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH + 10; i++) {
        trace_ring_push(&r, i, 1, 0, 0);
    }
    uint16_t prev = trace_ring_at(&r, 0)->seq;
    for (uint32_t i = 1; i < trace_ring_count(&r); i++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(prev + 1), trace_ring_at(&r, i)->seq);
        prev = trace_ring_at(&r, i)->seq;
    }
}

static void test_reset_clears_count_but_keeps_magic(void)
{
    trace_ring_reset(&r, MAGIC);
    trace_ring_push(&r, 1, 1, 0, 0);
    trace_ring_reset(&r, MAGIC);
    TEST_ASSERT_EQUAL_UINT32(0, trace_ring_count(&r));
    TEST_ASSERT_TRUE(trace_ring_valid(&r, MAGIC));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uninitialised_ring_is_not_valid);
    RUN_TEST(test_right_magic_but_bogus_indices_is_not_valid);
    RUN_TEST(test_reset_makes_valid_and_empty);
    RUN_TEST(test_push_below_depth_orders_oldest_first);
    RUN_TEST(test_push_past_depth_saturates_and_drops_oldest);
    RUN_TEST(test_wrapped_ring_reads_in_chronological_order);
    RUN_TEST(test_seq_is_monotonic_across_wrap);
    RUN_TEST(test_reset_clears_count_but_keeps_magic);
    return UNITY_END();
}
```

- [ ] **Step 3: Register the suite in `platformio.ini`**

In `[env:native]`, extend `test_filter`:

```ini
test_filter =
    test_position
    test_ramp
    test_keypad
    test_key_filter
    test_trace_ring
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `pio test -e native -f test_trace_ring`
Expected: FAIL at compile time — `src/trace_ring.c` does not exist yet.

- [ ] **Step 5: Write the implementation**

Create `src/trace_ring.c`:

```c
#include "trace_ring.h"

bool trace_ring_valid(const trace_ring_t *r, uint32_t magic)
{
    /* Bookkeeping is checked as well as the magic: uninitialised RTC memory
     * can collide with any single value, but is very unlikely to also carry
     * in-range head/count. */
    return r->magic == magic &&
           r->head  <  TRACE_DEPTH &&
           r->count <= TRACE_DEPTH;
}

void trace_ring_reset(trace_ring_t *r, uint32_t magic)
{
    r->magic = magic;
    r->head  = 0;
    r->count = 0;
    r->seq   = 0;
}

void trace_ring_push(trace_ring_t *r, uint32_t t_ms, uint16_t code,
                     int32_t a, int32_t b)
{
    trace_rec_t *rec = &r->rec[r->head];
    rec->t_ms = t_ms;
    rec->code = code;
    rec->seq  = r->seq++;
    rec->a    = a;
    rec->b    = b;

    r->head = (r->head + 1) % TRACE_DEPTH;
    if (r->count < TRACE_DEPTH) r->count++;
}

uint32_t trace_ring_count(const trace_ring_t *r)
{
    return r->count;
}

const trace_rec_t *trace_ring_at(const trace_ring_t *r, uint32_t i)
{
    if (i >= r->count) return NULL;
    /* oldest sits count slots behind head, modulo the ring */
    uint32_t idx = (r->head + TRACE_DEPTH - r->count + i) % TRACE_DEPTH;
    return &r->rec[idx];
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `pio test -e native -f test_trace_ring`
Expected: PASS, 8 tests, 0 failures.

- [ ] **Step 7: Run the full host suite**

Run: `pio test -e native`
Expected: PASS — five suites, no regressions.

- [ ] **Step 8: Commit**

```bash
git add include/trace_ring.h src/trace_ring.c test/test_trace_ring/test_trace_ring.c platformio.ini
git commit -F - <<'EOF'
trace_ring: the ring buffer behind a reset-surviving fault log

Pure ring logic, split out from the ESP glue so it can be host-tested at all
— RTC_NOINIT_ATTR and the critical section do not exist off-target, and this
is the half where the bugs would hide.

The validity check earns its keep. RTC memory is genuinely uninitialised at
power-on, so a ring that trusted its own contents would print garbage on the
first boot that looked exactly like real records. Checking the bookkeeping as
well as the magic makes a cold boot detectable rather than plausible, and the
cold-boot case is the first test.
EOF
```

---

### Task 2: The `trace` ESP glue

**Files:**
- Create: `include/trace.h`, `src/trace.c`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `trace_ring_t`, `trace_rec_t`, `trace_ring_valid`, `trace_ring_reset`, `trace_ring_push`, `trace_ring_count`, `trace_ring_at` from Task 1.
- Produces: `trace_code_t` enum (`TRC_BOOT`, `TRC_KEY_EVENT`, `TRC_MOVE_START`, `TRC_MOVE_NOOP`, `TRC_MOVE_REFUSED`, `TRC_MOVE_DONE`, `TRC_ZB_CMD`, `TRC_QUEUE_FULL`), `void trace_init(void)`, `void trace_dump(void)`, `void trace_emit(uint16_t, int32_t, int32_t)`, and the `TRACE(c, a, b)` macro. Task 3 depends on these exact names.

- [ ] **Step 1: Create the header**

Create `include/trace.h`:

```c
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "trace_ring.h"

/* Ships ON. A trace you must enable is a trace that is absent when the fault
 * happens: enabling means reflashing, reflashing resets the board, and a
 * reset clears exactly the class of fault this exists to catch. The flag is
 * here to strip the facility if RAM or flash gets tight, not as a mode. */
#ifndef TRACE_ENABLED
#define TRACE_ENABLED 1
#endif

typedef enum {
    TRC_BOOT = 1,      /* a = esp_reset_reason()   b = -                    */
    TRC_KEY_EVENT,     /* a = kp_event_type_t      b = key_id_t             */
    TRC_MOVE_START,    /* a = from_steps           b = to_steps             */
    TRC_MOVE_NOOP,     /* a = target               b = s_raw                */
    TRC_MOVE_REFUSED,  /* a = esp_err_t            b = hard_cap             */
    TRC_MOVE_DONE,     /* a = steps                b = completed            */
    TRC_ZB_CMD,        /* a = pct                  b = 1 if parked pending  */
    TRC_QUEUE_FULL,    /* a = dropped event type   b = -                    */
} trace_code_t;

/* Validate the RTC ring; clear it only if this was a cold boot. Emits nothing
 * so the caller can dump the previous session before marking the new one. */
void trace_init(void);

/* Print the ring, oldest first. */
void trace_dump(void);

/* Safe from task context, esp_timer callbacks and the Zigbee stack callback. */
void trace_emit(uint16_t code, int32_t a, int32_t b);

#if TRACE_ENABLED
#  define TRACE(c, a, b) trace_emit((uint16_t)(c), (int32_t)(a), (int32_t)(b))
#else
#  define TRACE(c, a, b) ((void)0)
#endif

#endif /* TRACE_H */
```

- [ ] **Step 2: Create the implementation**

Create `src/trace.c`:

```c
/**
 * @file trace.c
 * @brief RTC-backed decision log. Survives every reset — watchdog, panic,
 *        brownout, and the DTR/RTS reset that attaching a serial cable
 *        causes — but not power removal. That is the point: the reset used
 *        to destroy the evidence, and now it prints it.
 */
#include "trace.h"

#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "TRACE";

#define TRACE_MAGIC 0x54524331u   /* "TRC1" */

#if TRACE_ENABLED

RTC_NOINIT_ATTR static trace_ring_t s_ring;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *code_name(uint16_t c)
{
    switch (c) {
    case TRC_BOOT:         return "BOOT";
    case TRC_KEY_EVENT:    return "KEY_EVENT";
    case TRC_MOVE_START:   return "MOVE_START";
    case TRC_MOVE_NOOP:    return "MOVE_NOOP";
    case TRC_MOVE_REFUSED: return "MOVE_REFUSED";
    case TRC_MOVE_DONE:    return "MOVE_DONE";
    case TRC_ZB_CMD:       return "ZB_CMD";
    case TRC_QUEUE_FULL:   return "QUEUE_FULL";
    default:               return "?";
    }
}

void trace_init(void)
{
    if (!trace_ring_valid(&s_ring, TRACE_MAGIC)) {
        trace_ring_reset(&s_ring, TRACE_MAGIC);
    }
}

void trace_emit(uint16_t code, int32_t a, int32_t b)
{
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL_SAFE(&s_mux);
    trace_ring_push(&s_ring, t, code, a, b);
    portEXIT_CRITICAL_SAFE(&s_mux);
}

void trace_dump(void)
{
    uint32_t n = trace_ring_count(&s_ring);
    if (n == 0) {
        ESP_LOGI(TAG, "no records");
        return;
    }
    ESP_LOGI(TAG, "%u records, seq %u..%u", (unsigned)n,
             (unsigned)trace_ring_at(&s_ring, 0)->seq,
             (unsigned)trace_ring_at(&s_ring, n - 1)->seq);
    for (uint32_t i = 0; i < n; i++) {
        const trace_rec_t *r = trace_ring_at(&s_ring, i);
        ESP_LOGI(TAG, "[%9u] %-12s a=%ld b=%ld",
                 (unsigned)r->t_ms, code_name(r->code),
                 (long)r->a, (long)r->b);
    }
}

#else  /* !TRACE_ENABLED — app_main calls these unconditionally, so they must link */

void trace_init(void) {}
void trace_dump(void) {}
void trace_emit(uint16_t code, int32_t a, int32_t b)
{
    (void)code; (void)a; (void)b;
}

#endif
```

- [ ] **Step 3: Register both sources in the build**

In `src/CMakeLists.txt`, add to the `SRCS` list, after `"keypad.c"`:

```cmake
    "trace_ring.c"
    "trace.c"
```

The list is explicit, not a glob — omitting this produces link errors.

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 5: Verify the disabled path also builds**

Run: `PLATFORMIO_BUILD_FLAGS="-DTRACE_ENABLED=0" pio run -e seeed_xiao_esp32c6_zigbee`

(PlatformIO has no `--build-flag` switch; the environment variable is how you
add flags for a one-off build.)

Expected: SUCCESS. This proves the no-op stubs link and the flag genuinely strips the RTC array.

Then rebuild normally so the artefact on disk is the real one:

Run: `pio run -e seeed_xiao_esp32c6_zigbee`

- [ ] **Step 6: Verify no host regressions**

Run: `pio test -e native`
Expected: PASS — five suites.

- [ ] **Step 7: Commit**

```bash
git add include/trace.h src/trace.c src/CMakeLists.txt
git commit -F - <<'EOF'
trace: RTC-backed storage, dump and the always-on flag

The glue half: the RTC_NOINIT instance, a critical section short enough to
call from an esp_timer callback or the Zigbee stack, and a dump that prints
oldest-first at boot.

The flag defaults to on, and that is deliberate rather than lazy. Compiling
the trace out by default would guarantee it is missing whenever it is needed:
turning it on means reflashing, reflashing resets the board, and the reset is
what clears the faults worth tracing. It exists so the facility can be
stripped if RAM or flash gets tight.
EOF
```

---

### Task 3: Call sites and boot sequence

**Files:**
- Modify: `src/main.c` (boot sequence in `app_main`; `start_move`, `handle_keypad`, `zb_goto_request`, `APP_EVT_MOTION_DONE`)
- Modify: `src/keypad.c` (`post_kp` queue-full path)
- Modify: `CLAUDE.md` (module table)

**Interfaces:**
- Consumes: `TRACE()`, `trace_init()`, `trace_dump()` and the `trace_code_t` enumerators from Task 2.
- Produces: no new interface.

- [ ] **Step 1: Include the header in `main.c`**

In `src/main.c`, after `#include "ramp.h"`, add:

```c
#include "trace.h"
#include "esp_system.h"   /* esp_reset_reason */
```

- [ ] **Step 2: Add the boot sequence**

In `app_main`, make these the very first statements — before `nvs_flash_init()` — so nothing can emit before the ring is validated:

```c
void app_main(void)
{
    trace_init();                              /* validate; clear only if cold */
    trace_dump();                              /* history from BEFORE this reset */
    TRACE(TRC_BOOT, esp_reset_reason(), 0);    /* then mark the new session */

    esp_err_t err = nvs_flash_init();
```

Dumping before recording the boot marker keeps the dump purely the previous
session.

- [ ] **Step 3: Trace the keypad events**

In `handle_keypad`, add the TRACE as the first statement:

```c
static void handle_keypad(kp_event_t e)
{
    TRACE(TRC_KEY_EVENT, e.type, e.key);
    s_pending_valid = false;   /* any local input is the last writer (spec §7) */
    bool cal_dev = position_calibrated(&s_pos);
    switch (e.type) {
```

- [ ] **Step 4: Trace the move decisions**

Replace `start_move` in `src/main.c` entirely:

```c
static void start_move(int32_t target, const motion_profile_t *prof)
{
    if (target == s_raw) {
        TRACE(TRC_MOVE_NOOP, target, s_raw);
        refresh_outputs();   /* already there — keep reports honest, no NVS churn */
        return;
    }
    int32_t cap = hard_cap();          /* hoisted so a refusal can record it */
    TRACE(TRC_MOVE_START, s_raw, target);
    blind_store_set_move_flag(true);
    esp_err_t err = motion_start(s_raw, target, prof, cap);
    if (err != ESP_OK) {
        blind_store_set_move_flag(false);
        TRACE(TRC_MOVE_REFUSED, err, cap);
        ESP_LOGW(TAG, "move refused: %s", esp_err_to_name(err));
    } else {
        esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US);
    }
}
```

`hard_cap()` was previously evaluated inline as an argument; hoisting it is
what lets the refusal record carry the distance and the cap together.

- [ ] **Step 5: Trace the Zigbee commands**

Replace `zb_goto_request` in `src/main.c`:

```c
static void zb_goto_request(uint8_t pct)
{
    if (!position_calibrated(&s_pos)) return;   /* lockout backstop */
    TRACE(TRC_ZB_CMD, pct, motion_is_moving() ? 1 : 0);
    if (motion_is_moving()) {
        s_pending_pct   = pct;
        s_pending_valid = true;
        motion_stop();
    } else {
        goto_pct(pct);
    }
}
```

- [ ] **Step 6: Trace move completion**

In `dispatcher_task`, in `case APP_EVT_MOTION_DONE:`, add as the first statement inside the case block, immediately before `esp_timer_stop(s_report_timer);`:

```c
            TRACE(TRC_MOVE_DONE, ev.steps, ev.completed);
```

- [ ] **Step 7: Trace dropped keypad events**

In `src/keypad.c`, add `#include "trace.h"` after `#include "key_filter.h"`, then replace `post_kp`:

```c
static void post_kp(kp_event_t e)
{
    if (e.type == KP_EVT_NONE) return;
    app_event_t ev = { .type = APP_EVT_KEYPAD, .kp = e };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        TRACE(TRC_QUEUE_FULL, e.type, 0);
        ESP_LOGE(TAG, "queue full, dropped kp event type=%d", e.type);
        if (e.type == KP_EVT_HOLD_END || e.type == KP_EVT_TAP) {
            /* a dropped stop-class event must not leave the motor running */
            motion_stop();
        }
    }
}
```

This is the only record outside the dispatcher, because a dropped event is
the one thing the dispatcher cannot observe.

- [ ] **Step 8: Update the CLAUDE.md module table**

After the `key_filter` row, add:

```markdown
| `trace_ring` | `src/trace_ring.c` | Pure: fixed-size ring of decision records | `test/test_trace_ring/` |
| `trace` | `src/trace.c` | RTC_NOINIT storage + boot dump; survives resets, not power loss | — |
```

Also update line 42's host-test comment:

```
# Host tests (position / ramp / keypad_logic / key_filter / trace_ring — pure C, Unity)
```

- [ ] **Step 9: Verify the firmware builds**

Run: `pio run -e seeed_xiao_esp32c6_zigbee`
Expected: SUCCESS.

- [ ] **Step 10: Verify no host regressions**

Run: `pio test -e native`
Expected: PASS — five suites, all existing tests unchanged.

- [ ] **Step 11: Commit**

```bash
git add src/main.c src/keypad.c CLAUDE.md
git commit -F - <<'EOF'
Trace the dispatcher's decisions and dump them at boot

Records go in where decisions are made. This project's rule is that the step
ISR and the Zigbee action handler never decide anything and everything
happens in dispatcher_task, so tracing there yields a log of decisions rather
than a log of noise. The one exception is the queue-full path in keypad.c: a
dropped event is precisely the thing the dispatcher cannot see, and it was a
hypothesis that could not be tested on 2026-08-11.

hard_cap() moves from an inline argument to a local so a refused move can
record the distance and the cap together. Those two numbers were exactly what
an afternoon of bench debugging could not obtain.

The boot dump runs before the boot marker is written, so it shows purely the
session before the reset, and esp_reset_reason() rides in the marker — which
answers on sight whether a reboot was a brownout, a panic, a watchdog, or
somebody plugging in a USB cable.
EOF
```

---

### Task 4: Bench acceptance and merge

**Files:** none — this task produces evidence.

**Interfaces:**
- Consumes: the complete firmware from Tasks 1-3.
- Produces: a verified instrument.

Host tests cannot reach `RTC_NOINIT` placement, the critical section, or
whether the contents genuinely survive a reset on silicon. Those need the
board.

- [ ] **Step 1: Confirm the host suite is green**

Run: `pio test -e native`
Expected: PASS — `test_position`, `test_ramp`, `test_keypad`, `test_key_filter`, `test_trace_ring`.

- [ ] **Step 2: Flash the bench board**

Run: `pio run -e seeed_xiao_esp32c6_zigbee -t upload --upload-port /dev/cu.usbmodem1101`

Adjust the port if it differs; `ls /dev/cu.usbmodem*` lists it. The XIAO
sometimes needs a B+R nudge to enumerate behind a USB hub: hold **B**, tap
**R**, release **B**.

- [ ] **Step 3: Verify the cold-boot case prints nothing**

Power-cycle the board (unplug USB, wait five seconds, replug), then read the
boot log.

Expected: `TRACE: no records`, followed by nothing else from `TRACE` until
events occur. **A dump of random-looking records here means the validity
check is wrong and the tool cannot be trusted** — stop and fix it.

- [ ] **Step 4: Verify records survive a reset**

Press any keypad key twice, then reset the board by attaching to the serial
port (which asserts DTR/RTS), and read the boot log.

Expected: a dump containing the `KEY_EVENT` records from before the reset,
then `BOOT`. Something like:

```
TRACE: 5 records, seq 0..4
TRACE: [     8123] BOOT         a=1 b=0
TRACE: [    14200] KEY_EVENT    a=1 b=0
TRACE: [    14200] MOVE_NOOP    a=0 b=0
TRACE: [    18455] KEY_EVENT    a=1 b=0
TRACE: [    18455] MOVE_NOOP    a=0 b=0
```

**This is the acceptance criterion for the whole plan**: evidence recorded
before a reset, readable after it.

- [ ] **Step 5: Verify the reset reason is meaningful**

Compare the `BOOT` record's `a` value across a power-cycle and a serial-attach
reset. They must differ (`ESP_RST_POWERON` = 1 vs `ESP_RST_SW`/`ESP_RST_USB`).
Values are in `esp_system.h`'s `esp_reset_reason_t`.

- [ ] **Step 6: Merge**

```bash
git checkout main
git merge --no-ff diagnostic-trace
git branch -d diagnostic-trace
```

- [ ] **Step 7: Stop and hand back**

Do **not** push or tag. Report that the branch is merged locally and the
instrument is verified, then hand back for the actual investigation — the
Down-key fault remains undiagnosed and this plan deliberately did not touch
it.
