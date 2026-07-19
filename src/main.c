/**
 * DFR-RollerBlinds — skeleton main. Joins as a Zigbee router with an empty
 * Window Covering-less endpoint; Task 9 replaces this with the full wiring.
 */
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "zb_core.h"
#include "fw_version.h"

static const char *TAG = "BLINDS";

#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x0F" "DFR-RollerBlinds"

static void build_clusters(esp_zb_cluster_list_t *clusters)
{
    (void)clusters;   /* Window Covering cluster arrives with the covering module */
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
        .app_device_id     = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
        .manufacturer_name = MANUF_NAME,
        .model_identifier  = MODEL_ID,
        .ota = {
            .manufacturer_code = OTA_MANUFACTURER_CODE,
            .image_type        = OTA_IMAGE_TYPE,
            .file_version      = FW_VERSION_U32,
            .version_str       = FW_VERSION_STR,
        },
        .build_clusters    = build_clusters,
        .post_register     = NULL,
        .on_joined         = NULL,
        .action_handler    = NULL,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));
    ESP_LOGI(TAG, "router starting (%s); waiting for join…", FW_VERSION_STR);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    /* A freshly OTA'd image boots pending-verify; confirm it once the app is
     * up (join not required — the firmware itself is healthy) or the
     * bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
