/*
 * shutter_controller.c
 *
 * The motor has no built-in limit switches, so each relay must stay
 * energised for a fixed travel time and then switch off on its own.
 *
 * Safety rules enforced here:
 *   - The UP and DOWN relays are never energised at the same time.
 *   - Only one operation (open or close) can run at a time.
 *   - Every operation has a hard time limit (TRAVEL_SECONDS) after which
 *     the relay switches off automatically.
 *   - shutter_stop() interrupts an in-progress operation immediately.
 */

#include "shutter_controller.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RELAY_INACTIVE_LEVEL (!RELAY_ACTIVE_LEVEL)
#define STOP_BIT BIT0

static const char *TAG = "relay_controller";

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static QueueHandle_t s_queue;
static shutter_state_t s_state;

static void relay_set(gpio_num_t pin, bool energised)
{
    gpio_set_level(pin, energised ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
}

static void run(shutter_direction_t direction)
{
    gpio_num_t relay_on = direction == SHUTTER_UP ? RELAY_UP_GPIO : RELAY_DOWN_GPIO;
    gpio_num_t relay_off = direction == SHUTTER_UP ? RELAY_DOWN_GPIO : RELAY_UP_GPIO;

    /* Belt-and-braces: make sure the opposite relay is off first. */
    relay_set(relay_off, false);

    shutter_status_t status = direction == SHUTTER_UP ? SHUTTER_OPENING : SHUTTER_CLOSING;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.status = status;
    s_state.last_action = direction == SHUTTER_UP ? SHUTTER_ACTION_OPEN : SHUTTER_ACTION_CLOSE;
    s_state.last_action_time = time(NULL);
    xSemaphoreGive(s_lock);

    relay_set(relay_on, true);
    ESP_LOGI(TAG, "Shutter %s started (max %ds)", shutter_status_str(status), TRAVEL_SECONDS);

    EventBits_t bits = xEventGroupWaitBits(s_events, STOP_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(TRAVEL_SECONDS * 1000));

    relay_set(relay_on, false);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.status = SHUTTER_IDLE;
    s_state.busy = false;
    xSemaphoreGive(s_lock);

    if (bits & STOP_BIT) {
        ESP_LOGI(TAG, "Shutter operation stopped early by user");
    } else {
        ESP_LOGI(TAG, "Shutter operation finished (%s)", direction == SHUTTER_UP ? "up" : "down");
    }
}

static void motor_task(void *arg)
{
    shutter_direction_t direction;
    for (;;) {
        if (xQueueReceive(s_queue, &direction, portMAX_DELAY) == pdTRUE) {
            run(direction);
        }
    }
}

void shutter_init(void)
{
    /* Never start in an energised state: set the output latch before the
     * pins are switched to outputs so there is no glitch. */
    relay_set(RELAY_UP_GPIO, false);
    relay_set(RELAY_DOWN_GPIO, false);

    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << RELAY_UP_GPIO) | (1ULL << RELAY_DOWN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    relay_set(RELAY_UP_GPIO, false);
    relay_set(RELAY_DOWN_GPIO, false);

    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    s_queue = xQueueCreate(1, sizeof(shutter_direction_t));
    configASSERT(s_lock && s_events && s_queue);

    s_state = (shutter_state_t){ .status = SHUTTER_IDLE, .last_action = SHUTTER_ACTION_NONE };

    xTaskCreate(motor_task, "motor", 3072, NULL, 10, NULL);
    ESP_LOGI(TAG, "up_pin: %d down_pin: %d button_up_pin: %d button_down_pin: %d",
             RELAY_UP_GPIO, RELAY_DOWN_GPIO, BUTTON_UP_GPIO, BUTTON_DOWN_GPIO);
}

bool shutter_start(shutter_direction_t direction)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state.busy) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "Ignored %s request: shutter is already moving",
                 direction == SHUTTER_UP ? "up" : "down");
        return false;
    }
    s_state.busy = true;
    xEventGroupClearBits(s_events, STOP_BIT);
    xSemaphoreGive(s_lock);

    /* The queue is always empty here: busy is only cleared after the
     * motor task has dequeued and finished the previous command. */
    xQueueSend(s_queue, &direction, portMAX_DELAY);
    return true;
}

bool shutter_open(void)
{
    return shutter_start(SHUTTER_UP);
}

bool shutter_close(void)
{
    return shutter_start(SHUTTER_DOWN);
}

void shutter_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xEventGroupSetBits(s_events, STOP_BIT);
    if (!s_state.busy) {
        s_state.last_action = SHUTTER_ACTION_STOP;
        s_state.last_action_time = time(NULL);
    }
    xSemaphoreGive(s_lock);
}

void shutter_get_state(shutter_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

const char *shutter_status_str(shutter_status_t status)
{
    switch (status) {
    case SHUTTER_OPENING: return "opening";
    case SHUTTER_CLOSING: return "closing";
    default:              return "idle";
    }
}

const char *shutter_action_str(shutter_action_t action)
{
    switch (action) {
    case SHUTTER_ACTION_OPEN:  return "open";
    case SHUTTER_ACTION_CLOSE: return "close";
    case SHUTTER_ACTION_STOP:  return "stop";
    default:                   return NULL;
    }
}
