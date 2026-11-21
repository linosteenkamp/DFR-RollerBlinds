/**
 * @file rig_console.c
 * @brief The serial console of the test rig (USB-Serial-JTAG). It reads
 *        command lines and writes the published records. No decisions.
 */
#include "rig_console.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "rig_cmd.h"
#include "rig_event.h"

static const char *TAG = "RIG_CON";

#define IN_LINE_LEN  128   /* longer than the parser limit, so that the
                            * parser sees and rejects a line that is too long */
#define OUT_LINE_LEN 192
#define OUT_DEPTH    16

static QueueHandle_t s_cmd_q;
static QueueHandle_t s_out_q;   /* items: char[OUT_LINE_LEN] */

static void post_line(const char *line)
{
    rig_event_t ev = { 0 };
    rig_cmd_t cmd;
    switch (rig_cmd_parse(line, &cmd, ev.error)) {
    case RIG_PARSE_EMPTY:
        return;
    case RIG_PARSE_OK:
        ev.type = RIG_EVT_CMD;
        ev.cmd  = cmd;
        break;
    case RIG_PARSE_ERROR:
        ev.type = RIG_EVT_BAD_CMD;   /* ev.error has the message */
        break;
    }
    if (xQueueSend(s_cmd_q, &ev, 0) != pdTRUE) {
        /* This task is not the dispatcher, so it can write here. */
        printf("rig: the queue is full, the command was dropped\n");
    }
}

static void reader_task(void *pv)
{
    (void)pv;
    char line[IN_LINE_LEN];
    size_t n = 0;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            post_line(line);
            n = 0;
        } else if (n < sizeof line - 1) {
            line[n++] = (char)c;
        }
        /* When the line is full, the remaining bytes are dropped. The
         * parser then rejects the line as too long. */
    }
}

static void writer_task(void *pv)
{
    (void)pv;
    char line[OUT_LINE_LEN];
    for (;;) {
        if (xQueueReceive(s_out_q, line, portMAX_DELAY) == pdTRUE) {
            fputs(line, stdout);
            fflush(stdout);
        }
    }
}

bool rig_console_publish(const rig_record_t *rec, uint8_t qos, bool retain)
{
    (void)qos;
    (void)retain;
    char json[OUT_LINE_LEN];
    if (rig_record_json(rec, json, sizeof json) < 0) return false;
    char line[OUT_LINE_LEN];
    int n = snprintf(line, sizeof line, "%s %s\n", rig_record_topic(rec->topic), json);
    if (n < 0 || (size_t)n >= sizeof line) return false;
    return xQueueSend(s_out_q, line, 0) == pdTRUE;
}

esp_err_t rig_console_init(QueueHandle_t cmd_q)
{
    s_cmd_q = cmd_q;
    s_out_q = xQueueCreate(OUT_DEPTH, OUT_LINE_LEN);
    if (s_out_q == NULL) return ESP_ERR_NO_MEM;

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) return err;
    /* The driver gives blocking reads. The reader task handles CR and LF
     * itself, so the VFS must not change them. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);

    if (xTaskCreate(reader_task, "rig_con_rx", 4096, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(writer_task, "rig_con_tx", 3072, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}
