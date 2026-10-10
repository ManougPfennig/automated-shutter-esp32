/*
 * wifi.h
 *
 * Wi-Fi station using the credentials from wifi_config.h, with automatic
 * reconnection, NTP time sync and mDNS (http://<MDNS_HOSTNAME>.local).
 */
#pragma once

#include <stdbool.h>

/* Non-blocking: connection happens in the background and is retried
 * forever, so the physical buttons keep working without network. */
void wifi_start(void);

/* Signal strength of the current connection in dBm. Returns false when
 * not connected. Above -67 is good, below -75 expect lost requests. */
bool wifi_get_rssi(int *rssi);
