/*
 * buttons.c
 *
 * Buttons are wired between the GPIO and GND, with the internal pull-up
 * enabled. They are polled and
 * debounced in a small task rather than using interrupts, so contact
 * bounce can never fire a burst of start/stop commands.
 */

#include "buttons.h"

#include <stdbool.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shutter_controller.h"

#define POLL_INTERVAL_MS 20
#define DEBOUNCE_SAMPLES 3

static const char *TAG = "buttons";

typedef struct {
    gpio_num_t pin;
    shutter_direction_t direction;
    bool pressed;           /* debounced state */
    int stable_count;       /* consecutive samples disagreeing with it */
} button_t;

static button_t s_buttons[] = {
    { .pin = BUTTON_UP_GPIO, .direction = SHUTTER_UP },
    { .pin = BUTTON_DOWN_GPIO, .direction = SHUTTER_DOWN },
};

static void on_pressed(button_t *b)
{
    ESP_LOGI(TAG, "Button %s pressed", b->direction == SHUTTER_UP ? "up" : "down");
    shutter_start(b->direction);
}

static void on_released(button_t *b)
{
    ESP_LOGI(TAG, "Button %s released", b->direction == SHUTTER_UP ? "up" : "down");
    shutter_stop();
}

static void buttons_task(void *arg)
{
    for (;;) {
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
            button_t *b = &s_buttons[i];
            bool raw_pressed = gpio_get_level(b->pin) == 0;

            if (raw_pressed == b->pressed) {
                b->stable_count = 0;
                continue;
            }
            if (++b->stable_count < DEBOUNCE_SAMPLES) {
                continue;
            }
            b->stable_count = 0;
            b->pressed = raw_pressed;
            if (b->pressed) {
                on_pressed(b);
            } else {
                on_released(b);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void buttons_init(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) | (1ULL << BUTTON_DOWN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    xTaskCreate(buttons_task, "buttons", 2560, NULL, 9, NULL);
}
