idf_component_register(SRCS "main.c"
                            "hardware/shutter_controller.c"
                            "hardware/buttons.c"
                            "storage/settings_store.c"
                            "scheduling/scheduler.c"
                            "network/wifi.c"
                            "web/web_server.c"
                       INCLUDE_DIRS "config" "hardware" "storage" "scheduling" "network" "web"
                       EMBED_TXTFILES "web/index.html"
                       PRIV_REQUIRES esp_driver_gpio esp_wifi esp_netif esp_event
                                     esp_http_server nvs_flash lwip)
