#ifndef RIG_CONSOLE_H
#define RIG_CONSOLE_H

/* The serial console of the test rig, on the USB-Serial-JTAG port. It is a
 * thin adapter with no decisions:
 *   - A reader task reads lines, gives them to rig_cmd_parse(), and puts a
 *     RIG_EVT_CMD or RIG_EVT_BAD_CMD item in the dispatcher queue.
 *   - rig_console_publish() is the publish port of rig_ctl for now. It
 *     writes "<topic> <json>" lines. The MQTT adapter (ticket #36) will
 *     replace it. */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rig_record.h"

/* Install the USB-Serial-JTAG driver and start the reader and writer
 * tasks. cmd_q has items of sizeof(rig_event_t). */
esp_err_t rig_console_init(QueueHandle_t cmd_q);

/* The publish port. It does not block: it puts the line in the queue of
 * the writer task, and returns false when that queue is full (the line is
 * dropped). qos and retain have no effect on the serial console. */
bool rig_console_publish(const rig_record_t *rec, uint8_t qos, bool retain);

#endif /* RIG_CONSOLE_H */
