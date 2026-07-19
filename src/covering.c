/**
 * @file covering.c
 * @brief Window Covering (0x0102) server glue. The action handler runs in
 *        Zigbee stack context WITH THE LOCK HELD (zb_core contract): it only
 *        classifies the message, checks the lockout flag, and posts to the
 *        dispatcher queue. All real work happens in main's dispatcher task.
 */
#ifdef USE_ZIGBEE
#include "covering.h"
#include "app_event.h"
#include "position.h"          /* POSITION_LIFT_UNKNOWN */

#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_window_covering.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "esp_log.h"

static const char *TAG = "COVER";

#define COVER_ENDPOINT 1U

static QueueHandle_t  s_queue;
static volatile bool  s_motion_allowed = false;

/* Attribute storage — the ZCL table keeps pointers; must live forever. */
static uint8_t s_lift_pct   = POSITION_LIFT_UNKNOWN;
static uint8_t s_mode       = 0;   /* bit0 = motor reversed */
static uint8_t s_cfg_status = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_ONLINE; /* not yet operational */

void covering_set_queue(QueueHandle_t q) { s_queue = q; }
void covering_set_motion_allowed(bool allowed) { s_motion_allowed = allowed; }

void covering_build_clusters(esp_zb_cluster_list_t *clusters)
{
    esp_zb_window_covering_cluster_cfg_t cfg = {
        .covering_type   = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_ROLLERSHADE,
        .covering_status = s_cfg_status,
        .covering_mode   = s_mode,
    };
    esp_zb_attribute_list_t *attrs = esp_zb_window_covering_cluster_create(&cfg);
    /* Lift percentage is not among the create()-mandatory attrs — add it
     * (u8, read+report). 0xFF = unknown until calibrated. */
    esp_zb_cluster_add_attr(attrs, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &s_lift_pct);
    esp_zb_cluster_list_add_window_covering_cluster(clusters, attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void covering_post_register(void)
{
    /* Device-side reporting on lift % so the stack pushes on-change reports
     * (same mechanism the siblings use). */
    esp_zb_zcl_reporting_info_t rep = {
        .direction      = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
        .ep             = COVER_ENDPOINT,
        .cluster_id     = ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
        .cluster_role   = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id        = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
        .manuf_code     = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .u.send_info    = { .min_interval = 0, .max_interval = 0,
                            .def_min_interval = 0, .def_max_interval = 0,
                            .delta.u8 = 1 },
    };
    esp_zb_zcl_update_reporting_info(&rep);
}

static void post(app_event_t ev)
{
    if (s_queue) xQueueSend(s_queue, &ev, 0);
}

esp_err_t covering_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                  const void *message)
{
    switch (cb_id) {
    case ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID: {
        const esp_zb_zcl_window_covering_movement_message_t *msg = message;
        switch (msg->command) {
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN:
            if (!s_motion_allowed) return ESP_FAIL;      /* ZCL failure resp */
            post((app_event_t){ .type = APP_EVT_ZB_OPEN });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE:
            if (!s_motion_allowed) return ESP_FAIL;
            post((app_event_t){ .type = APP_EVT_ZB_CLOSE });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP:
            /* Stop is always safe — queue it even when locked out. */
            post((app_event_t){ .type = APP_EVT_ZB_STOP });
            return ESP_OK;
        case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
            if (!s_motion_allowed) return ESP_FAIL;
            if (msg->payload.percentage_lift_value > 100) return ESP_FAIL;
            post((app_event_t){ .type = APP_EVT_ZB_GOTO,
                                .pct = msg->payload.percentage_lift_value });
            return ESP_OK;
        default:
            ESP_LOGW(TAG, "unsupported covering cmd 0x%x", msg->command);
            return ESP_FAIL;
        }
    }
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
        const esp_zb_zcl_set_attr_value_message_t *msg = message;
        if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING &&
            msg->attribute.id == ESP_ZB_ZCL_ATTR_WINDOW_COVERING_MODE_ID &&
            msg->attribute.data.value) {
            uint8_t mode = *(uint8_t *)msg->attribute.data.value;
            post((app_event_t){ .type = APP_EVT_ZB_SET_REVERSED,
                                .on = (mode & ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_REVERSED_MOTOR_DIRECTION) != 0 });
        }
        return ESP_OK;
    }
    default:
        return ESP_OK;
    }
}

/* ---- task-context attribute updates (take the Zigbee lock) ---- */

static void set_attr(uint16_t attr_id, void *val)
{
    if (!esp_zb_lock_acquire(portMAX_DELAY)) return;
    esp_zb_zcl_set_attribute_val(COVER_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        attr_id, val, false);
    esp_zb_lock_release();
}

void covering_report_lift(uint8_t pct)
{
    s_lift_pct = pct;
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
             &s_lift_pct);
}

void covering_set_operational(bool calibrated)
{
    s_cfg_status = ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_ONLINE |
                   (calibrated ? ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_OPERATIONAL : 0);
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CONFIG_STATUS_ID, &s_cfg_status);
}

void covering_report_mode(bool reversed)
{
    s_mode = reversed ? ESP_ZB_ZCL_ATTR_WINDOW_COVERING_TYPE_REVERSED_MOTOR_DIRECTION : 0;
    set_attr(ESP_ZB_ZCL_ATTR_WINDOW_COVERING_MODE_ID, &s_mode);
}
#endif /* USE_ZIGBEE */
