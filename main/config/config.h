/*
 * config.h
 *
 * Hardware and behaviour settings for the shutter controller. These are
 * compile-time constants (edit, rebuild, reflash). Only the open/close
 * times and the schedule on/off switch are editable from the web page,
 * and those are persisted in NVS (see settings_store.c).
 *
 * Wi-Fi credentials live in wifi_config.h (git-ignored).
 */
#pragma once

/* ---- GPIO wiring (ESP32-C3 Super Mini) -------------------------------
 * Avoid GPIO2/8/9 (strapping, GPIO8 = onboard LED, GPIO9 = BOOT button),
 * GPIO18/19 (USB) and GPIO20/21 (UART0). GPIO4-7 come out of reset with
 * a weak pull-up, so active-low relay modules stay off while booting. */
#define RELAY_UP_GPIO       4
#define RELAY_DOWN_GPIO     5
#define BUTTON_UP_GPIO      6
#define BUTTON_DOWN_GPIO    7

/* Level that energises a relay: 0 for active-low relay modules (the
 * default, 0 = relay on), 1 for active-high modules. */
#define RELAY_ACTIVE_LEVEL  0

/* How long a relay stays energised for a full open/close. Time it on the
 * real shutter and set it a couple of seconds longer than the slowest
 * direction. */
#define TRAVEL_SECONDS      15

/* ---- Schedule defaults (used until a schedule is saved from the page) - */
#define DEFAULT_OPEN_TIME   "08:00"
#define DEFAULT_CLOSE_TIME  "20:00"

/* Seconds between two scheduler checks. */
#define SCHEDULER_CHECK_INTERVAL_SECONDS 5

/* ---- Network ---------------------------------------------------------- */
/* POSIX TZ string, here Europe/Paris (CET/CEST with automatic DST). */
#define TIMEZONE            "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER          "pool.ntp.org"
/* Page reachable at http://<MDNS_HOSTNAME>.local */
#define MDNS_HOSTNAME       "volet"
#define HTTP_PORT           80
/* Maximum Wi-Fi transmit power, in 0.25 dBm units (8..84, i.e. 2..20 dBm).
 * Higher = better range but more heat. Some ESP32-C3 Super Mini boards have
 * a badly matched antenna and fail to connect at full power: if the board
 * never connects, lower this (34 = 8.5 dBm is the usual fix). If requests
 * are slow or lost, raise it (72 = 18 dBm, 80 = 20 dBm). */
#define WIFI_MAX_TX_POWER   60

/* 1 = modem sleep: the radio sleeps between the router's beacons. Much less
 * heat, but each request can wait up to a beacon interval (~100-300ms) and
 * some routers lose packets with sleeping clients. 0 = radio always on:
 * fastest and most reliable, the board runs warmer (still within spec). */
#define WIFI_POWER_SAVE     1
