/*
 * settings_store.h
 *
 * Open/close times and the schedule on/off switch, persisted in NVS so
 * they survive reboots and power cuts.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* "HH:MM" plus terminator */
#define SETTINGS_TIME_LEN 6

/* Initialise NVS and load the saved schedule (defaults if none saved). */
void settings_init(void);

void settings_get(char open_time[SETTINGS_TIME_LEN], char close_time[SETTINGS_TIME_LEN]);

/* Persist a new schedule. Both times must already be valid "HH:MM". */
esp_err_t settings_update(const char *open_time, const char *close_time);

/* Whether the scheduler may open/close the shutter automatically. */
bool settings_get_schedule_enabled(void);
esp_err_t settings_set_schedule_enabled(bool enabled);

/* True if value is "H:MM"/"HH:MM" with 0 <= HH <= 23 and 0 <= MM <= 59.
 * When normalized is not NULL it receives the zero-padded "HH:MM" form. */
bool settings_valid_time(const char *value, char normalized[SETTINGS_TIME_LEN]);
