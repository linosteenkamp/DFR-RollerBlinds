/**
 * @file blind_store.c
 * @brief NVS persistence, namespace "blind". Thin wrapper — logic lives in
 *        position.c. Writes happen at move boundaries only (NVS wear).
 */
#include "blind_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "STORE";
static nvs_handle_t s_nvs;

static bool get_u8_bool(const char *key, bool dflt)
{
    uint8_t v = dflt ? 1 : 0;
    nvs_get_u8(s_nvs, key, &v);   /* NOT_FOUND leaves default */
    return v != 0;
}

static int32_t get_i32(const char *key, int32_t dflt)
{
    int32_t v = dflt;
    nvs_get_i32(s_nvs, key, &v);
    return v;
}

esp_err_t blind_store_init(blind_store_data_t *out)
{
    esp_err_t err = nvs_open("blind", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        *out = (blind_store_data_t){0};
        return err;
    }
    out->span_valid       = get_u8_bool("span_ok", false);
    out->closed_steps     = get_i32("span", 0);
    out->pos_known        = get_u8_bool("pos_ok", false);
    out->cur_steps        = get_i32("pos", 0);
    out->motor_reversed   = get_u8_bool("rev", false);
    out->move_in_progress = get_u8_bool("moving", false);
    return ESP_OK;
}

static esp_err_t commit2(esp_err_t a, esp_err_t b)
{
    esp_err_t c = nvs_commit(s_nvs);
    if (a != ESP_OK) return a;
    if (b != ESP_OK) return b;
    return c;
}

esp_err_t blind_store_save_span(bool span_valid, int32_t closed_steps)
{
    /* value first, flag second: a torn pair (power loss mid-write) then
     * fails toward the flag not being set, not toward a trusted stale value */
    return commit2(nvs_set_i32(s_nvs, "span", closed_steps),
                   nvs_set_u8(s_nvs, "span_ok", span_valid ? 1 : 0));
}

esp_err_t blind_store_save_position(bool pos_known, int32_t cur_steps)
{
    return commit2(nvs_set_i32(s_nvs, "pos", cur_steps),
                   nvs_set_u8(s_nvs, "pos_ok", pos_known ? 1 : 0));
}

esp_err_t blind_store_save_motor_reversed(bool reversed)
{
    return commit2(nvs_set_u8(s_nvs, "rev", reversed ? 1 : 0), ESP_OK);
}

esp_err_t blind_store_set_move_flag(bool in_progress)
{
    return commit2(nvs_set_u8(s_nvs, "moving", in_progress ? 1 : 0), ESP_OK);
}
