# esp-zb-common Library Extraction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract DoorSensor's proven Zigbee/OTA modules into a standalone `esp-zb-common` ESP-IDF component (own GitHub repo, tag-pinned), proven to compile via an example router app.

**Architecture:** The library is an ESP-IDF **component** at the root of a new git repo `~/Developer/499/esp-zb-common`, consumed by projects through the IDF Component Manager (git dependency in `src/idf_component.yml`, `override_path` for local dev). It contains `zb_core` (generalized router bring-up with app-supplied cluster builder), `ota_client` (parameterized IDs instead of consumer headers), `debounce` (pure C), the OTA packaging tools, and reference templates. An `examples/minimal_router` PlatformIO project is the compile proof.

**Tech Stack:** C11, ESP-IDF via PlatformIO (pioarduino platform fork), esp-zigbee-lib 1.6.x, Unity (host tests), pytest (tools).

## Global Constraints

- Platform pin (all ESP builds): `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.31-2/platform-espressif32.zip`
- Zigbee libs pinned: `espressif/esp-zigbee-lib: "~1.6.0"`, `espressif/esp-zboss-lib: "~1.6.0"` (2.x has a different native API — never bump past 1.6.x here)
- All Zigbee code gated by `#ifdef USE_ZIGBEE`; consumers pass `-DUSE_ZIGBEE`
- Naming: functions `module_verb_noun()`, constants `UPPER_SNAKE_CASE`, types `snake_case_t`, log tags short uppercase
- ZCL strings are length-prefixed: first byte = char count (e.g. `"\x0B" "DFRobot-DIY"`)
- Component/dependency name is `esp-zb-common` (must match the component folder basename for `override_path` resolution — verified in Task 6); repo name is also `esp-zb-common`
- GitHub owner `linosteenkamp`, repo **private** (matches siblings)
- Source of truth for copied files is `/Users/lino/Developer/499/DFR-DoorSensor` (referred to below as `$DS`)
- Library working directory: `/Users/lino/Developer/499/esp-zb-common` (all task paths relative to it unless absolute)

---

### Task 1: Repository scaffold

**Files:**
- Create: `/Users/lino/Developer/499/esp-zb-common/.gitignore`
- Create: `/Users/lino/Developer/499/esp-zb-common/idf_component.yml`
- Create: `/Users/lino/Developer/499/esp-zb-common/CMakeLists.txt`
- Create: `/Users/lino/Developer/499/esp-zb-common/platformio.ini`
- Create: `/Users/lino/Developer/499/esp-zb-common/README.md`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: repo layout + component manifest that Tasks 2–7 fill in. The component registers `src/debounce.c`, `src/ota_client.c`, `src/zb_core.c` — later tasks must use exactly those paths.

- [ ] **Step 1: Create the repo and directory layout**

```bash
mkdir -p /Users/lino/Developer/499/esp-zb-common/{include,src,test/test_debounce,tools,templates,examples}
cd /Users/lino/Developer/499/esp-zb-common
git init -b main
```

- [ ] **Step 2: Write `.gitignore`**

```gitignore
.pio/
__pycache__/
.pytest_cache/
examples/*/managed_components/
examples/*/dependencies.lock
examples/*/sdkconfig.dfrobot_firebeetle2_esp32c6_zigbee
examples/*/.pio/
```

- [ ] **Step 3: Write `idf_component.yml`** (component manifest — declares the Zigbee deps so consumers inherit them)

```yaml
version: "0.1.0"
description: >
  Shared Zigbee (esp_zb_* 1.6.x) core for DFR ESP32-C6 devices: router
  bring-up with app-supplied cluster builder, Zigbee OTA client with
  bootloader rollback, pure-C debounce, OTA packaging tools.
url: https://github.com/linosteenkamp/esp-zb-common
dependencies:
  espressif/esp-zigbee-lib: "~1.6.0"
  espressif/esp-zboss-lib: "~1.6.0"
```

- [ ] **Step 4: Write the component `CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS
        "src/debounce.c"
        "src/ota_client.c"
        "src/zb_core.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__esp-zigbee-lib
        espressif__esp-zboss-lib
        app_update
        esp_partition
)
```

- [ ] **Step 5: Write `platformio.ini`** (host tests only — the component itself is built by consumers)

```ini
; esp-zb-common — host-test harness only. The component is compiled by consumer
; projects (see examples/minimal_router for the compile proof).
[platformio]
default_envs = native

[env:native]
platform = native
framework =
test_framework = unity
build_flags = -std=c11 -Wall -Wextra -I include
; Tests #include the SUT .c directly; keep src/ out of the host build.
build_src_filter = -<*>
test_filter =
    test_debounce
```

- [ ] **Step 6: Write `README.md` stub** (full docs come in Task 7)

```markdown
# esp-zb-common

Shared Zigbee core for DFR ESP32-C6 devices (DFR-DoorSensor,
DFR-MoistureTracker, DFR-RollerBlinds). ESP-IDF component; esp-zigbee-lib 1.6.x.

Documentation is completed alongside the first consumer. See
`examples/minimal_router` for usage.
```

- [ ] **Step 7: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add -A
git commit -m "Scaffold esp-zb-common component repo"
```

---

### Task 2: `debounce` module (verbatim port, test-first)

**Files:**
- Create: `test/test_debounce/test_debounce.c` (copied from `$DS/test/test_debounce/test_debounce.c`)
- Create: `include/debounce.h` (copied from `$DS/include/debounce.h`)
- Create: `src/debounce.c` (copied from `$DS/src/debounce.c`)

**Interfaces:**
- Consumes: Task 1 layout.
- Produces: `debounce_t`, `void debounce_init(debounce_t *d, int initial_level)`, `bool debounce_settle(debounce_t *d, int sampled_level)`, `int debounce_stable_level(const debounce_t *d)` — used later by DFR-RollerBlinds `keypad`.

- [ ] **Step 1: Copy the test first**

```bash
cp "$DS/test/test_debounce/test_debounce.c" /Users/lino/Developer/499/esp-zb-common/test/test_debounce/test_debounce.c
```

The test includes the SUT as `#include "../../src/debounce.c"` — the repo layout matches DoorSensor's, so the copy needs **no edits**.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /Users/lino/Developer/499/esp-zb-common && pio test -e native`
Expected: FAIL — cannot open `../../src/debounce.c` (module not copied yet).

- [ ] **Step 3: Copy the module**

```bash
cp "$DS/include/debounce.h" /Users/lino/Developer/499/esp-zb-common/include/debounce.h
cp "$DS/src/debounce.c"     /Users/lino/Developer/499/esp-zb-common/src/debounce.c
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd /Users/lino/Developer/499/esp-zb-common && pio test -e native`
Expected: PASS — 5/5 test cases green (`test_init_sets_stable_level`, `test_settle_same_level_no_change`, `test_settle_different_level_reports_change`, `test_settle_change_then_same_no_second_change`, `test_bounce_back_to_original_no_change`).

- [ ] **Step 5: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add test/test_debounce include/debounce.h src/debounce.c
git commit -m "Port debounce module + Unity tests from DFR-DoorSensor"
```

---

### Task 3: OTA packaging tools + pytest suite

**Files:**
- Create: `tools/make_ota_image.py`, `tools/update_ota_index.py`, `tools/test_ota_tools.py` (all copied from `$DS/tools/`)

**Interfaces:**
- Consumes: Task 1 layout.
- Produces: `tools/make_ota_image.py --in <bin> --out <ota> --manufacturer <hex> --image-type <hex> --file-version <hex>` and `tools/update_ota_index.py --index <json> --model <id> --manufacturer <hex> --image-type <hex> --file-version <hex> --url <url> --image <ota>` — consumed by every project's `release-ota.yml` CI.

- [ ] **Step 1: Copy the tools and their tests**

```bash
cp "$DS/tools/make_ota_image.py" "$DS/tools/update_ota_index.py" "$DS/tools/test_ota_tools.py" \
   /Users/lino/Developer/499/esp-zb-common/tools/
```

No edits: the tools are already project-agnostic (all identity comes from CLI flags).

- [ ] **Step 2: Run the pytest suite**

Run: `cd /Users/lino/Developer/499/esp-zb-common && python3 -m pytest tools/ -q`
Expected: PASS — all tests green (header layout, index update round-trip).

- [ ] **Step 3: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add tools
git commit -m "Port OTA packaging tools + pytest suite from DFR-DoorSensor"
```

---

### Task 4: `ota_client` with parameterized identity

The DoorSensor version `#include`s the **consumer's** `ota_ids.h`/`fw_version.h`. A component cannot see consumer headers, so identity moves into a runtime struct passed once at cluster-build time. Everything else is verbatim.

**Files:**
- Create: `include/ota_client.h` (adapted from `$DS/include/ota_client.h`)
- Create: `src/ota_client.c` (copied from `$DS/src/ota_client.c`, then 3 edits below)

**Interfaces:**
- Consumes: Task 1 layout.
- Produces (all under `#ifdef USE_ZIGBEE`):
  - `ota_client_ids_t` — `{ uint16_t manufacturer_code; uint16_t image_type; uint32_t file_version; const char *version_str; }`
  - `void ota_client_add_cluster(esp_zb_cluster_list_t *clusters, const ota_client_ids_t *ids)`
  - `esp_err_t ota_client_on_value(const esp_zb_zcl_ota_upgrade_value_message_t *msg)`
  - `void ota_client_start(uint8_t endpoint)`
  - `void ota_client_on_joined(void)`
  - `bool ota_client_image_pending_verify(void)`
  - `void ota_client_mark_valid(void)`
  - Task 5's `zb_core` calls `ota_client_add_cluster`, `ota_client_start`, `ota_client_on_joined`, `ota_client_on_value`.

- [ ] **Step 1: Write `include/ota_client.h`** (complete file)

```c
#ifndef OTA_CLIENT_H
#define OTA_CLIENT_H

#ifdef USE_ZIGBEE
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"  /* esp_zb_zcl_ota_upgrade_value_message_t */

/* Product identity for Zigbee OTA. The consumer supplies these (typically from
 * its ota_ids.h / fw_version.h); they MUST match the .ota image header and the
 * z2m OTA index. version_str is used only for logging (e.g. "v1.0.2"). */
typedef struct {
    uint16_t    manufacturer_code;
    uint16_t    image_type;
    uint32_t    file_version;
    const char *version_str;
} ota_client_ids_t;

/* OTA value callback — invoked from the core action handler on
 * ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID. Writes received blocks to the inactive
 * slot and reboots into the new image on FINISH. */
esp_err_t ota_client_on_value(const esp_zb_zcl_ota_upgrade_value_message_t *msg);

/* Add the OTA Upgrade client cluster to an existing cluster list (call while
 * building the endpoint). ids is copied. */
void ota_client_add_cluster(esp_zb_cluster_list_t *clusters,
                            const ota_client_ids_t *ids);

/* Called once after esp_zb_start: records the client endpoint. Does NOT start
 * querying — that needs server discovery (ota_client_on_joined). */
void ota_client_start(uint8_t endpoint);

/* Call from the Zigbee signal handler on join success (stack context). Discovers
 * the coordinator's OTA server and arms periodic image queries. Without this the
 * client never queries and z2m can't deliver an update to it. */
void ota_client_on_joined(void);

/* True if the running image is in pending-verify (freshly OTA'd, not yet confirmed). */
bool ota_client_image_pending_verify(void);

/* Confirm a pending-verify image so the bootloader keeps it (no-op otherwise). */
void ota_client_mark_valid(void);

#endif /* USE_ZIGBEE */
#endif /* OTA_CLIENT_H */
```

- [ ] **Step 2: Copy the implementation**

```bash
cp "$DS/src/ota_client.c" /Users/lino/Developer/499/esp-zb-common/src/ota_client.c
```

- [ ] **Step 3: Edit 1 — replace the consumer-header includes**

In `src/ota_client.c`, replace:

```c
#include "ota_client.h"
#include "ota_ids.h"
#include "fw_version.h"
```

with:

```c
#include "ota_client.h"
```

- [ ] **Step 4: Edit 2 — store the identity struct**

Immediately after the `#define OTA_MAX_DATA_SIZE 223` line, add:

```c
/* Identity captured in ota_client_add_cluster; zeroed until then. */
static ota_client_ids_t s_ids;
```

- [ ] **Step 5: Edit 3 — rewrite `ota_client_add_cluster`**

Replace the entire existing `ota_client_add_cluster` function body with:

```c
void ota_client_add_cluster(esp_zb_cluster_list_t *clusters,
                            const ota_client_ids_t *ids)
{
    s_ids = *ids;

    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_file_version        = s_ids.file_version,
        .ota_upgrade_manufacturer        = s_ids.manufacturer_code,
        .ota_upgrade_image_type          = s_ids.image_type,
        .ota_upgrade_downloaded_file_ver = s_ids.file_version,
    };
    esp_zb_attribute_list_t *ota_attrs = esp_zb_ota_cluster_create(&ota_cfg);

    /* The OTA *client* additionally requires the client-data variable (0xFFF1)
     * plus server addr/endpoint attrs. esp_zb_ota_cluster_create() only adds the
     * mandatory attrs; without the client variable the stack dereferences missing
     * client state and asserts in zcl_ota_upgrade_commands.c at startup. Static
     * storage so the values outlive this function regardless of how add_attr keeps them. */
    static esp_zb_zcl_ota_upgrade_client_variable_t ota_var = {
        .timer_query   = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version    = OTA_HW_VERSION,
        .max_data_size = OTA_MAX_DATA_SIZE,
    };
    static uint16_t ota_server_addr = 0xFFFF;   /* unknown — discover via the network */
    static uint8_t  ota_server_ep   = 0xFF;     /* unknown — discover */
    esp_zb_ota_cluster_add_attr(ota_attrs, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID,     &ota_var);
    esp_zb_ota_cluster_add_attr(ota_attrs, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,     &ota_server_addr);
    esp_zb_ota_cluster_add_attr(ota_attrs, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, &ota_server_ep);

    esp_zb_cluster_list_add_ota_cluster(clusters, ota_attrs, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    ESP_LOGI(TAG, "OTA client cluster added (fw %s = 0x%08x)",
             s_ids.version_str ? s_ids.version_str : "?",
             (unsigned)s_ids.file_version);
}
```

- [ ] **Step 6: Compile check note**

This file cannot compile standalone (needs the consumer toolchain + esp-zigbee-lib). **Its compile proof is Task 6's example build** — do not skip Task 6.

- [ ] **Step 7: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add include/ota_client.h src/ota_client.c
git commit -m "Port ota_client with runtime identity struct (no consumer headers)"
```

---

### Task 5: `zb_core` module

Generalized from `$DS/src/zb_router.c`: IAS-Zone specifics removed; endpoint identity, device id, cluster building, post-register hook, join hook, and residual action handling become configuration.

**Files:**
- Create: `include/zb_core.h` (complete file below)
- Create: `src/zb_core.c` (complete file below)

**Interfaces:**
- Consumes: Task 4's `ota_client_ids_t`, `ota_client_add_cluster`, `ota_client_start`, `ota_client_on_joined`, `ota_client_on_value`.
- Produces (all under `#ifdef USE_ZIGBEE`):
  - `zb_core_role_t` — `ZB_CORE_ROLE_ROUTER`, `ZB_CORE_ROLE_END_DEVICE` (ED returns `ESP_ERR_NOT_SUPPORTED` for now)
  - `zb_core_cfg_t` — see header below
  - `esp_err_t zb_core_init(const zb_core_cfg_t *cfg)`
  - `bool zb_core_wait_ready(uint32_t timeout_ms)`
  - `bool zb_core_is_joined(void)`
  - The library defines the global `esp_zb_app_signal_handler` — consumers must NOT define their own.

- [ ] **Step 1: Write `include/zb_core.h`** (complete file)

```c
#ifndef ZB_CORE_H
#define ZB_CORE_H

#ifdef USE_ZIGBEE
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"
#include "ota_client.h"   /* ota_client_ids_t */

/* Only ROUTER is implemented. END_DEVICE exists so the interface won't churn
 * when the MoistureTracker retrofit adds it; passing it returns
 * ESP_ERR_NOT_SUPPORTED today. */
typedef enum {
    ZB_CORE_ROLE_ROUTER = 0,
    ZB_CORE_ROLE_END_DEVICE,
} zb_core_role_t;

typedef struct {
    zb_core_role_t role;           /* ZB_CORE_ROLE_ROUTER */
    uint8_t        endpoint;       /* HA endpoint id (also the OTA client endpoint) */
    uint16_t       app_device_id;  /* HA device id, e.g. 0x0202 window covering */

    /* ZCL length-prefixed identity strings (first byte = char count),
     * e.g. "\x0B" "DFRobot-DIY". Must outlive the stack (use literals). */
    const char *manufacturer_name;
    const char *model_identifier;

    ota_client_ids_t ota;          /* OTA product identity (from the app's ota_ids.h) */

    /* Add device-specific clusters. Basic (mains-powered, with identity),
     * Identify, and the OTA client cluster are added by zb_core — add only what
     * is specific to this device (IAS Zone, Window Covering, ...). */
    void (*build_clusters)(esp_zb_cluster_list_t *clusters);

    /* Optional (NULL to skip): runs in the Zigbee task after
     * esp_zb_device_register() and before esp_zb_start() — the place for
     * esp_zb_zcl_update_reporting_info() calls. */
    void (*post_register)(void);

    /* Optional (NULL to skip): join success. Zigbee stack context — the lock is
     * already held; do NOT call esp_zb_lock_acquire or blocking APIs. */
    void (*on_joined)(void);

    /* Optional (NULL = log-and-ignore): core actions not consumed by zb_core.
     * OTA upgrade values are handled internally and never reach this. */
    esp_err_t (*action_handler)(esp_zb_core_action_callback_id_t cb_id,
                                const void *message);
} zb_core_cfg_t;

/* Start the Zigbee stack (cfg is copied). Router: radio always on. Restores
 * network state if already joined, else begins BDB steering. Returns once the
 * stack task is created; join completes asynchronously (see zb_core_wait_ready).
 * Call exactly once. */
esp_err_t zb_core_init(const zb_core_cfg_t *cfg);

/* Block until joined or the timeout elapses. Returns true if joined. */
bool zb_core_wait_ready(uint32_t timeout_ms);

/* True once the device has (re)joined a network. */
bool zb_core_is_joined(void);

#endif /* USE_ZIGBEE */
#endif /* ZB_CORE_H */
```

- [ ] **Step 2: Write `src/zb_core.c`** (complete file)

```c
/**
 * @file zb_core.c
 * @brief Shared Zigbee bring-up: router role, BDB steering/join, signal
 *        handling, endpoint assembly (Basic + Identify + OTA + app clusters).
 *        esp-zigbee-lib 1.6.x native esp_zb_* API. Generalized from
 *        DFR-DoorSensor's zb_router.c.
 */
#ifdef USE_ZIGBEE

#include "zb_core.h"
#include "ota_client.h"

#include "esp_zigbee_core.h"
#include "platform/esp_zigbee_platform.h"
#include "nwk/esp_zigbee_nwk.h"           /* esp_zb_get_short_address */
#include "bdb/esp_zigbee_bdb_commissioning.h"
#include "zdo/esp_zigbee_zdo_common.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_endpoint.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_basic.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ZB_CORE";

static zb_core_cfg_t s_cfg;
static volatile bool s_joined = false;

/* Single global core-action dispatcher — only ONE may be registered
 * stack-wide, so it lives here. OTA first, then the app's handler. */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                   const void *message)
{
    if (cb_id == ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID) {
        return ota_client_on_value(
            (const esp_zb_zcl_ota_upgrade_value_message_t *)message);
    }
    if (s_cfg.action_handler) {
        return s_cfg.action_handler(cb_id, message);
    }
    ESP_LOGD(TAG, "unhandled core action cb_id=0x%x", (unsigned)cb_id);
    return ESP_OK;
}

static void steering_alarm_cb(uint8_t mode)
{
    esp_zb_bdb_start_top_level_commissioning(
        (esp_zb_bdb_commissioning_mode_mask_t)mode);
}

static void on_joined(void)
{
    s_joined = true;
    ESP_LOGI(TAG, "joined network, short addr 0x%04hx", esp_zb_get_short_address());
    ota_client_on_joined();
    if (s_cfg.on_joined) {
        s_cfg.on_joined();   /* stack context: lock already held */
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = (esp_zb_app_signal_type_t)*p_sg_p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "stack ready, starting BDB init");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "factory-new — starting network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                on_joined();  /* rejoined existing network */
            }
        } else {
            ESP_LOGW(TAG, "start/reboot status %d", err);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err == ESP_OK) {
            on_joined();
        } else {
            ESP_LOGW(TAG, "steering failed (%d), retry in 1s", err);
            esp_zb_scheduler_alarm(steering_alarm_cb,
                                   (uint8_t)ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000U);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal %s (0x%x) status %d",
                 esp_zb_zdo_signal_to_string(sig), (unsigned)sig, err);
        break;
    }
}

static void esp_zb_task(void *pv)
{
    (void)pv;

    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg    = { .max_children = 10 },
    };
    esp_zb_init(&zb_cfg);
    esp_zb_set_rx_on_when_idle(true);   /* router: receiver always on */

    /* Basic cluster (+ manufacturer/model identity for z2m recognition).
     * power_source 0x01 = mains — routers are mains-powered by definition. */
    esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 8, .power_source = 1 };
    esp_zb_attribute_list_t *basic_attrs = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)s_cfg.manufacturer_name);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)s_cfg.model_identifier);

    /* Identify cluster. */
    esp_zb_identify_cluster_cfg_t id_cfg = { .identify_time = 0 };
    esp_zb_attribute_list_t *id_attrs = esp_zb_identify_cluster_create(&id_cfg);

    /* Assemble clusters: shared first, then the app's, then OTA. */
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(clusters, basic_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(clusters, id_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (s_cfg.build_clusters) {
        s_cfg.build_clusters(clusters);
    }
    ota_client_add_cluster(clusters, &s_cfg.ota);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = s_cfg.endpoint,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = s_cfg.app_device_id,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg);
    esp_zb_device_register(ep_list);

    if (s_cfg.post_register) {
        s_cfg.post_register();   /* reporting info etc. */
    }

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    ota_client_start(s_cfg.endpoint);
    esp_zb_stack_main_loop();  /* never returns */
    vTaskDelete(NULL);
}

esp_err_t zb_core_init(const zb_core_cfg_t *cfg)
{
    if (!cfg || !cfg->manufacturer_name || !cfg->model_identifier) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->role != ZB_CORE_ROLE_ROUTER) {
        ESP_LOGE(TAG, "role %d not supported (router only)", (int)cfg->role);
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_cfg = *cfg;

    ESP_LOGI(TAG, "init Zigbee router");
    esp_zb_platform_config_t pcfg = {
        .radio_config = { .radio_mode = ZB_RADIO_MODE_NATIVE },
        .host_config  = { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&pcfg));
    BaseType_t ret = xTaskCreate(esp_zb_task, "esp_zb_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create Zigbee task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool zb_core_wait_ready(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (!s_joined && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    return s_joined;
}

bool zb_core_is_joined(void)
{
    return s_joined;
}

#endif /* USE_ZIGBEE */
```

- [ ] **Step 3: Commit** (compile proof is Task 6)

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add include/zb_core.h src/zb_core.c
git commit -m "Add zb_core: generalized router bring-up with app cluster builder"
```

---

### Task 6: Example consumer `examples/minimal_router` (compile proof)

A minimal PlatformIO project inside the repo that consumes the component via `override_path`. `pio run` succeeding here is the compile proof for Tasks 4–5; it also documents the consumer wiring for the RollerBlinds plan.

**Files:**
- Create: `examples/minimal_router/platformio.ini`
- Create: `examples/minimal_router/CMakeLists.txt`
- Create: `examples/minimal_router/partitions.csv` (copied from `$DS/partitions.csv`)
- Create: `examples/minimal_router/sdkconfig.defaults` (copied from `$DS/sdkconfig.defaults`)
- Create: `examples/minimal_router/sdkconfig.defaults.zigbee` (copied from `$DS/sdkconfig.defaults.zigbee`)
- Create: `examples/minimal_router/src/CMakeLists.txt`
- Create: `examples/minimal_router/src/idf_component.yml`
- Create: `examples/minimal_router/src/main.c`

**Interfaces:**
- Consumes: `zb_core_cfg_t`, `zb_core_init`, `zb_core_wait_ready` (Task 5); `ota_client_ids_t` (Task 4).
- Produces: the canonical consumer wiring (platformio.ini + idf_component.yml pattern) that DFR-RollerBlinds will replicate.

- [ ] **Step 1: Copy build templates from DoorSensor**

```bash
cd /Users/lino/Developer/499/esp-zb-common/examples/minimal_router
cp "$DS/partitions.csv" .
cp "$DS/sdkconfig.defaults" .
cp "$DS/sdkconfig.defaults.zigbee" .
```

- [ ] **Step 2: Write `examples/minimal_router/platformio.ini`**

```ini
; Compile-proof consumer for the esp-zb-common component.
[platformio]
default_envs = dfrobot_firebeetle2_esp32c6_zigbee

[env:dfrobot_firebeetle2_esp32c6_zigbee]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.31-2/platform-espressif32.zip
framework = espidf
board = dfrobot_firebeetle2_esp32c6
monitor_speed = 115200
board_build.partitions = partitions.csv
build_flags = -DUSE_ZIGBEE
board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee"
```

- [ ] **Step 3: Write `examples/minimal_router/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-zb-common-minimal-router)
```

- [ ] **Step 4: Write `examples/minimal_router/src/idf_component.yml`** (the local-override consumer pattern)

```yaml
dependencies:
  esp-zb-common:
    version: "*"
    override_path: "../../.."
```

- [ ] **Step 5: Write `examples/minimal_router/src/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event
)
```

(The `esp-zb-common` dependency is injected by the component manager from `idf_component.yml`; its include dirs propagate automatically.)

- [ ] **Step 6: Write `examples/minimal_router/src/main.c`** (complete file)

```c
/**
 * Minimal esp-zb-common consumer: joins as a bare router (Basic + Identify +
 * OTA only). Compile proof for the component and the reference consumer wiring.
 */
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "zb_core.h"

static const char *TAG = "MINIMAL";

/* ZCL length-prefixed strings: first byte = character count. */
#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x12" "ZbCommon-MinRouter"

static void build_clusters(esp_zb_cluster_list_t *clusters)
{
    (void)clusters;   /* no device-specific clusters in the minimal example */
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    zb_core_cfg_t cfg = {
        .role              = ZB_CORE_ROLE_ROUTER,
        .endpoint          = 1,
        .app_device_id     = ESP_ZB_HA_ON_OFF_SWITCH_DEVICE_ID,
        .manufacturer_name = MANUF_NAME,
        .model_identifier  = MODEL_ID,
        .ota = {
            .manufacturer_code = 0xFEFE,
            .image_type        = 0x00FF,   /* example-only image type */
            .file_version      = 0x00000000,
            .version_str       = "v0.0.0",
        },
        .build_clusters    = build_clusters,
        .post_register     = NULL,
        .on_joined         = NULL,
        .action_handler    = NULL,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));
    ESP_LOGI(TAG, "router starting; waiting for join…");
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    } else {
        ESP_LOGW(TAG, "not joined after 60 s (keeps steering in background)");
    }
}
```

- [ ] **Step 7: Build — this is the compile proof for Tasks 4 & 5**

Run: `cd /Users/lino/Developer/499/esp-zb-common/examples/minimal_router && pio run`
Expected: SUCCESS. First run downloads the platform + managed components (esp-zigbee-lib 1.6.x via the component's manifest) — takes several minutes. Fix any compile errors in `zb_core.c` / `ota_client.c` before proceeding (these are the first real compiles of both).

- [ ] **Step 8 (optional, hardware on hand): join check**

Run: `pio run -t upload -t monitor` with a FireBeetle 2 ESP32-C6 attached and z2m permit-join on.
Expected: `ZB_CORE: joined network, short addr 0x....` in the monitor; device appears in z2m (unsupported model is fine — no converter for the example).

- [ ] **Step 9: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add examples/minimal_router
git commit -m "Add minimal_router example: compile proof + reference consumer wiring"
```

---

### Task 7: Templates + README documentation

**Files:**
- Create: `templates/partitions.csv`, `templates/sdkconfig.defaults`, `templates/sdkconfig.defaults.zigbee` (copied from `$DS/`)
- Create: `templates/README.md`
- Modify: `README.md` (replace the Task 1 stub entirely)

**Interfaces:**
- Consumes: everything prior (documents it).
- Produces: the consumer checklist the DFR-RollerBlinds plan will follow.

- [ ] **Step 1: Copy templates**

```bash
cd /Users/lino/Developer/499/esp-zb-common
cp "$DS/partitions.csv" "$DS/sdkconfig.defaults" "$DS/sdkconfig.defaults.zigbee" templates/
```

- [ ] **Step 2: Write `templates/README.md`**

```markdown
# Templates

Build-system files every consumer copies into its project root (PlatformIO
cannot ship these as component artifacts — they must live in the project):

- `partitions.csv` — dual-OTA layout (nvs / phy_init / otadata / ota_0 / ota_1 /
  storage / zb_storage / zb_fct) with explicit offsets pinned to work around a
  PlatformIO upload-offset bug. Required for Zigbee OTA with bootloader rollback.
- `sdkconfig.defaults` — base ESP-IDF config.
- `sdkconfig.defaults.zigbee` — Zigbee router config (ZCZR role, no power
  management) + `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

These are canonical HERE. If a consumer needs to diverge, prefer fixing the
template and re-copying.
```

- [ ] **Step 3: Rewrite `README.md`** (complete replacement)

```markdown
# esp-zb-common

Shared Zigbee core for DFR ESP32-C6 devices (siblings: DFR-DoorSensor,
DFR-MoistureTracker, DFR-RollerBlinds). An ESP-IDF **component**; native
`esp_zb_*` API, esp-zigbee-lib **1.6.x** (do not bump to 2.x — different API).

## Modules

| Module | Header | What it does |
|---|---|---|
| `zb_core` | `zb_core.h` | Router bring-up: BDB steering/join, network-state restore, signal handling, endpoint assembly (Basic + Identify + OTA + your clusters via a builder callback). Defines the global `esp_zb_app_signal_handler` — do not define your own. |
| `ota_client` | `ota_client.h` | Zigbee OTA download to the inactive slot, coordinator server discovery, periodic image queries, stall watchdog, bootloader-rollback helpers (`image_pending_verify` / `mark_valid`). |
| `debounce` | `debounce.h` | Pure-C settled-level debounce (ISR restarts a one-shot; settle on expiry). Host-testable. |

## Consuming the component

In your project's `src/idf_component.yml`:

```yaml
dependencies:
  esp-zb-common:
    git: https://github.com/linosteenkamp/esp-zb-common.git
    version: v0.1.0
```

For local development against a checkout, use an override instead:

```yaml
dependencies:
  esp-zb-common:
    version: "*"
    override_path: "../../esp-zb-common"
```

Then:

1. Copy `templates/partitions.csv`, `templates/sdkconfig.defaults`,
   `templates/sdkconfig.defaults.zigbee` into your project root.
2. In `platformio.ini`: pinned pioarduino platform, `framework = espidf`,
   `build_flags = -DUSE_ZIGBEE`, `board_build.partitions = partitions.csv`,
   `board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee"`.
3. Keep your product identity app-side (`ota_ids.h`, `fw_version.h`) and pass it
   in `zb_core_cfg_t.ota` as an `ota_client_ids_t`.
4. Call `zb_core_init()` from `app_main()` — see `examples/minimal_router/`.

## OTA tooling

`tools/make_ota_image.py` wraps a `firmware.bin` into a Zigbee `.ota` image;
`tools/update_ota_index.py` maintains the z2m OTA index JSON. Both are invoked
from each project's `release-ota.yml` CI. Tests: `python3 -m pytest tools/ -q`.

## Tests

- Host: `pio test -e native` (Unity; pure modules).
- Compile proof: `cd examples/minimal_router && pio run`.

## Versioning

Tag releases `vX.Y.Z`; consumers pin the tag in `idf_component.yml`. Breaking
interface changes bump the major.
```

- [ ] **Step 4: Commit**

```bash
cd /Users/lino/Developer/499/esp-zb-common
git add templates README.md
git commit -m "Add build templates + full README (interface + consumer checklist)"
```

---

### Task 8: Publish to GitHub and tag v0.1.0

**Files:** none (repo operations only).

**Interfaces:**
- Consumes: all prior tasks committed.
- Produces: `https://github.com/linosteenkamp/esp-zb-common` at tag `v0.1.0` — the exact ref the DFR-RollerBlinds plan pins.

- [ ] **Step 1: Verify everything is committed and green**

Run, from `/Users/lino/Developer/499/esp-zb-common`:
```bash
git status --porcelain          # expected: empty
pio test -e native              # expected: PASS
python3 -m pytest tools/ -q     # expected: PASS
(cd examples/minimal_router && pio run)   # expected: SUCCESS
```

- [ ] **Step 2: Create the private GitHub repo and push**

```bash
cd /Users/lino/Developer/499/esp-zb-common
gh repo create linosteenkamp/esp-zb-common --private --source . --push
```

Expected: repo created, `main` pushed.

- [ ] **Step 3: Tag and push v0.1.0**

```bash
git tag -a v0.1.0 -m "esp-zb-common v0.1.0: zb_core (router), ota_client, debounce, OTA tools"
git push origin v0.1.0
```

Note: the component manifest's `version:` field already says `0.1.0` (Task 1) — they must stay in lockstep on future releases.

---

## Self-Review Notes

- **Spec coverage (spec §3):** `zb_core` ✔ (Task 5, role enum incl. ED-not-supported), `ota_client` ✔ (Task 4), `debounce` ✔ (Task 2), `tools/` ✔ (Task 3), templates ✔ (Tasks 6–7), own tests + README ✔ (Tasks 2, 3, 7), GitHub + version tag ✔ (Task 8). `ias_zone` and WiFi/MQTT modules deliberately absent (spec: not extracted now).
- **Deviation from spec §3 (approved 2026-07-18):** consumption is via **IDF Component Manager git dependency**, not PlatformIO `lib_deps` — `lib_deps` cannot express ESP-IDF component `REQUIRES` on esp-zigbee-lib in pure-espidf projects. All decided properties (own GitHub repo, tag pinning, local override) preserved. Spec updated to match.
- **Identity headers:** `ota_ids.h` / `fw_version.h` stay app-side (per spec); the component boundary forced `ota_client_add_cluster` to take `ota_client_ids_t` instead of including them. The DoorSensor keepalive + IAS logic stays behind (device-specific).
- **Type consistency check:** `ota_client_ids_t` field names match between Task 4 header, Task 5 `zb_core_cfg_t.ota` usage, and Task 6 example initializer. `zb_core_cfg_t` callback names (`build_clusters`, `post_register`, `on_joined`, `action_handler`) match between Tasks 5 and 6.
