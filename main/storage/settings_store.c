/*
 * settings_store.c
 *
 * Keeps the schedule in RAM for fast access and mirrors every change to
 * the "shutter" NVS namespace.
 */

#include "settings_store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "shutter"
#define KEY_OPEN "open_time"
#define KEY_CLOSE "close_time"
#define KEY_SCHEDULE_ENABLED "sched_on"

static const char *TAG = "config";

static SemaphoreHandle_t s_lock;
static char s_open_time[SETTINGS_TIME_LEN];
static char s_close_time[SETTINGS_TIME_LEN];
static bool s_schedule_enabled = true;

static bool parse_int(const char *start, const char *end, int *out)
{
    if (start == end || end - start > 2) {
        return false;
    }
    int value = 0;
    for (const char *p = start; p < end; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        value = value * 10 + (*p - '0');
    }
    *out = value;
    return true;
}

bool settings_valid_time(const char *value, char normalized[SETTINGS_TIME_LEN])
{
    if (value == NULL) {
        return false;
    }
    const char *colon = strchr(value, ':');
    if (colon == NULL) {
        return false;
    }
    int hh, mm;
    if (!parse_int(value, colon, &hh) || !parse_int(colon + 1, colon + strlen(colon), &mm)) {
        return false;
    }
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        return false;
    }
    if (normalized != NULL) {
        snprintf(normalized, SETTINGS_TIME_LEN, "%02d:%02d", hh, mm);
    }
    return true;
}

static void load_key(nvs_handle_t nvs, const char *key, const char *fallback, char out[SETTINGS_TIME_LEN])
{
    char buf[16];
    size_t len = sizeof(buf);
    if (nvs_get_str(nvs, key, buf, &len) == ESP_OK && settings_valid_time(buf, out)) {
        return;
    }
    strlcpy(out, fallback, SETTINGS_TIME_LEN);
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        load_key(nvs, KEY_OPEN, DEFAULT_OPEN_TIME, s_open_time);
        load_key(nvs, KEY_CLOSE, DEFAULT_CLOSE_TIME, s_close_time);
        uint8_t enabled;
        if (nvs_get_u8(nvs, KEY_SCHEDULE_ENABLED, &enabled) == ESP_OK) {
            s_schedule_enabled = enabled != 0;
        }
        nvs_close(nvs);
    } else {
        /* Namespace doesn't exist yet: first boot. */
        strlcpy(s_open_time, DEFAULT_OPEN_TIME, sizeof(s_open_time));
        strlcpy(s_close_time, DEFAULT_CLOSE_TIME, sizeof(s_close_time));
    }
    ESP_LOGI(TAG, "Schedule loaded: open=%s close=%s (%s)", s_open_time, s_close_time,
             s_schedule_enabled ? "enabled" : "disabled");
}

void settings_get(char open_time[SETTINGS_TIME_LEN], char close_time[SETTINGS_TIME_LEN])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(open_time, s_open_time, SETTINGS_TIME_LEN);
    memcpy(close_time, s_close_time, SETTINGS_TIME_LEN);
    xSemaphoreGive(s_lock);
}

esp_err_t settings_update(const char *open_time, const char *close_time)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(nvs, KEY_OPEN, open_time);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, KEY_CLOSE, close_time);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving schedule failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_open_time, open_time, sizeof(s_open_time));
    strlcpy(s_close_time, close_time, sizeof(s_close_time));
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool settings_get_schedule_enabled(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_schedule_enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}

esp_err_t settings_set_schedule_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(nvs, KEY_SCHEDULE_ENABLED, enabled);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving schedule switch failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_schedule_enabled = enabled;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
