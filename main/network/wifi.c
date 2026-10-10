/*
 * wifi.c
 */

#include "wifi.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "mdns.h"

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#error "main/config/wifi_config.h is missing: copy main/config/wifi_config.h.example to main/config/wifi_config.h and set your Wi-Fi SSID/password"
#endif

_Static_assert(sizeof(WIFI_SSID) <= 33, "WIFI_SSID is longer than 32 characters");
_Static_assert(sizeof(WIFI_PASSWORD) <= 64, "WIFI_PASSWORD is longer than 63 characters");

#define RETRY_MIN_MS 1000
#define RETRY_MAX_MS 30000

static const char *TAG = "wifi";

static TimerHandle_t s_retry_timer;
static uint32_t s_retry_delay_ms = RETRY_MIN_MS;

static void retry_timer_cb(TimerHandle_t timer)
{
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to \"%s\"...", WIFI_SSID);
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "Disconnected (reason %d), retrying in %lums",
                 ev->reason, (unsigned long)s_retry_delay_ms);
        xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(s_retry_delay_ms), 0);
        xTimerStart(s_retry_timer, 0);
        s_retry_delay_ms = s_retry_delay_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : s_retry_delay_ms * 2;
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    s_retry_delay_ms = RETRY_MIN_MS;
    ESP_LOGI(TAG, "Connected, IP " IPSTR " -> http://" IPSTR "/ or http://%s.local/",
             IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.ip), MDNS_HOSTNAME);
    int rssi;
    if (wifi_get_rssi(&rssi)) {
        ESP_LOGI(TAG, "Signal strength: %d dBm", rssi);
    }
}

bool wifi_get_rssi(int *rssi)
{
    return esp_wifi_sta_get_rssi(rssi) == ESP_OK;
}

static void time_sync_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm local;
    char buf[32];
    localtime_r(&now, &local);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    ESP_LOGI(TAG, "Clock synchronised: %s", buf);
}

static void start_sntp(void)
{
    setenv("TZ", TIMEZONE, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    config.sync_cb = time_sync_cb;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
}

static void start_mdns(void)
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("Shutter Control"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", HTTP_PORT, NULL, 0));
}

void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, MDNS_HOSTNAME));

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_MIN_MS), pdFALSE, NULL, retry_timer_cb);
    configASSERT(s_retry_timer);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            /* Accept WPA2 and WPA3 access points. */
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strlcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_POWER_SAVE ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Must be called after esp_wifi_start(). */
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(WIFI_MAX_TX_POWER));

    start_sntp();
    start_mdns();
}
