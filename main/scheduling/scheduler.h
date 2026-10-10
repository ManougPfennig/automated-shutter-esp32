/*
 * scheduler.h
 *
 * Background task that opens/closes the shutter when the local time
 * matches the saved open_time/close_time.
 */
#pragma once

void scheduler_start(void);

/* Forget which days already triggered, so a freshly saved time can fire
 * today even if the previous one already did. */
void scheduler_reset_last_dates(void);
