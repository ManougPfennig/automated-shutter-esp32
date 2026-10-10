/*
 * scheduler.c
 *
 * Wakes up every SCHEDULER_CHECK_INTERVAL_SECONDS, reads the current
 * schedule and triggers the shutter when the current HH:MM matches.
 * Each action fires at most once per day. Nothing is triggered while the
 * schedule is disabled from the web page, or until the clock has been set
 * by NTP.
 */

#include "scheduler.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_store.h"
#include "shutter_controller.h"

#define NO_DATE (-1)

static const char *TAG = "scheduler";

/* Accessed from the scheduler task and the HTTP server task; plain int
 * stores are atomic on the ESP32-C3. */
static volatile int s_last_open_date = NO_DATE;
static volatile int s_last_close_date = NO_DATE;

static bool clock_is_set(const struct tm *now)
{
    return now->tm_year + 1900 >= 2024;
}

static void check_once(void)
{
    if (!settings_get_schedule_enabled()) {
        return;
    }

    time_t t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);

    static bool warned = false;
    if (!clock_is_set(&now)) {
        if (!warned) {
            ESP_LOGW(TAG, "Clock not synchronised yet, schedule paused");
            warned = true;
        }
        return;
    }
    warned = false;

    char current_hhmm[SETTINGS_TIME_LEN];
    strftime(current_hhmm, sizeof(current_hhmm), "%H:%M", &now);
    int today = (now.tm_year + 1900) * 1000 + now.tm_yday;

    char open_time[SETTINGS_TIME_LEN], close_time[SETTINGS_TIME_LEN];
    settings_get(open_time, close_time);

    if (strcmp(current_hhmm, open_time) == 0 && s_last_open_date != today) {
        ESP_LOGI(TAG, "Scheduled OPEN triggered at %s", current_hhmm);
        shutter_open();
        s_last_open_date = today;
    } else if (strcmp(current_hhmm, close_time) == 0 && s_last_close_date != today) {
        ESP_LOGI(TAG, "Scheduled CLOSE triggered at %s", current_hhmm);
        shutter_close();
        s_last_close_date = today;
    }
}

static void scheduler_task(void *arg)
{
    for (;;) {
        check_once();
        vTaskDelay(pdMS_TO_TICKS(SCHEDULER_CHECK_INTERVAL_SECONDS * 1000));
    }
}

void scheduler_reset_last_dates(void)
{
    s_last_open_date = NO_DATE;
    s_last_close_date = NO_DATE;
}

void scheduler_start(void)
{
    xTaskCreate(scheduler_task, "scheduler", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "Scheduler started (checking every %ds)", SCHEDULER_CHECK_INTERVAL_SECONDS);
}
