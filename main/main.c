/*
 * main.c
 *
 * Automated roller shutter controller for the ESP32-C3.
 *
 * Two relays (UP / DOWN) take the place of the shutter's 3-position wall
 * switch. The shutter can be driven from the physical buttons,
 * from the web page (http://shutter.local) or automatically at the
 * open/close times saved from that page.
 */

#include "buttons.h"
#include "scheduler.h"
#include "settings_store.h"
#include "shutter_controller.h"
#include "web_server.h"
#include "wifi.h"

void app_main(void)
{
    /* Relays first, so they are forced off as early as possible. */
    shutter_init();
    settings_init();
    /* Buttons work standalone, even while Wi-Fi is down. */
    buttons_init();

    wifi_start();
    web_server_start();
    scheduler_start();
}
