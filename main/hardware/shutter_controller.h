/*
 * shutter_controller.h
 *
 * Drives the two relays that take the place of the roller shutter's
 * 3-position (UP / NEUTRAL / DOWN) wall switch.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

typedef enum {
    SHUTTER_UP,
    SHUTTER_DOWN,
} shutter_direction_t;

typedef enum {
    SHUTTER_IDLE,
    SHUTTER_OPENING,
    SHUTTER_CLOSING,
} shutter_status_t;

typedef enum {
    SHUTTER_ACTION_NONE,
    SHUTTER_ACTION_OPEN,
    SHUTTER_ACTION_CLOSE,
    SHUTTER_ACTION_STOP,
} shutter_action_t;

typedef struct {
    shutter_status_t status;
    shutter_action_t last_action;
    time_t last_action_time;    /* 0 when no action happened yet */
    bool busy;
} shutter_state_t;

/* Configure the relay GPIOs (de-energised) and start the motor task. */
void shutter_init(void);

/* Start a travel. Returns false if the shutter is already moving. */
bool shutter_open(void);
bool shutter_close(void);
bool shutter_start(shutter_direction_t direction);

/* Interrupt whatever is currently running. Safe to call anytime. */
void shutter_stop(void);

void shutter_get_state(shutter_state_t *out);

const char *shutter_status_str(shutter_status_t status);
const char *shutter_action_str(shutter_action_t action);
