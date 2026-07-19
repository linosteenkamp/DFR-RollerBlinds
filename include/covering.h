#ifndef COVERING_H
#define COVERING_H

#ifdef USE_ZIGBEE
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Wire-up: main passes these three into zb_core_cfg_t. */
void covering_build_clusters(esp_zb_cluster_list_t *clusters);
void covering_post_register(void);
esp_err_t covering_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                  const void *message);

/* Dispatcher queue for APP_EVT_ZB_* events (set before zb_core_init). */
void covering_set_queue(QueueHandle_t q);

/* Motion lockout flag, owned by the dispatcher: while false, movement
 * commands get a ZCL failure response and are NOT queued (spec §5). */
void covering_set_motion_allowed(bool allowed);

/* Attribute updates — call from TASK context only (they take the Zigbee
 * lock). pct: 0..100 or POSITION_LIFT_UNKNOWN (0xFF). */
void covering_report_lift(uint8_t pct);
void covering_set_operational(bool calibrated);   /* ConfigStatus bit0 */
void covering_report_mode(bool reversed);         /* Mode attr bit0 */

#endif /* USE_ZIGBEE */
#endif /* COVERING_H */
