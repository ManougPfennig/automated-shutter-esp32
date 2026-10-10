/*
 * web_server.c
 */

#include "web_server.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "scheduler.h"
#include "settings_store.h"
#include "shutter_controller.h"
#include "wifi.h"

#define MAX_BODY_LEN 512

static const char *TAG = "app";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", message);
    return send_json(req, status, json);
}

static cJSON *state_to_json(void)
{
    shutter_state_t state;
    shutter_get_state(&state);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", shutter_status_str(state.status));
    const char *last_action = shutter_action_str(state.last_action);
    if (last_action) {
        cJSON_AddStringToObject(json, "last_action", last_action);
    } else {
        cJSON_AddNullToObject(json, "last_action");
    }
    if (state.last_action_time) {
        cJSON_AddNumberToObject(json, "last_action_time", (double)state.last_action_time);
    } else {
        cJSON_AddNullToObject(json, "last_action_time");
    }
    cJSON_AddBoolToObject(json, "busy", state.busy);
    cJSON_AddBoolToObject(json, "simulated", false);
    return json;
}

/* ---- GET / ------------------------------------------------------------ */

/* Renders index.html, substituting its {{ placeholder }} tags with the
 * current settings. */
static esp_err_t index_get(httpd_req_t *req)
{
    char open_time[SETTINGS_TIME_LEN], close_time[SETTINGS_TIME_LEN];
    settings_get(open_time, close_time);
    const char *schedule_checked = settings_get_schedule_enabled() ? "checked" : "";

    const struct {
        const char *placeholder;
        const char *value;
    } vars[] = {
        { "{{ open_time }}", open_time },
        { "{{ close_time }}", close_time },
        { "{{ schedule_checked }}", schedule_checked },
    };

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    /* EMBED_TXTFILES appends a NUL terminator, which must not be sent. */
    const char *end = index_html_end - 1;
    const char *p = index_html_start;
    while (p < end) {
        const char *next = end;
        int which = -1;
        for (int i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
            const char *found = strstr(p, vars[i].placeholder);
            if (found && found < next) {
                next = found;
                which = i;
            }
        }
        if (next > p && httpd_resp_send_chunk(req, p, next - p) != ESP_OK) {
            return ESP_FAIL;
        }
        if (which < 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, vars[which].value, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
        p = next + strlen(vars[which].placeholder);
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ---- GET /api/status --------------------------------------------------- */

static esp_err_t status_get(httpd_req_t *req)
{
    char open_time[SETTINGS_TIME_LEN], close_time[SETTINGS_TIME_LEN];
    settings_get(open_time, close_time);

    cJSON *json = state_to_json();
    cJSON_AddStringToObject(json, "open_time", open_time);
    cJSON_AddStringToObject(json, "close_time", close_time);
    cJSON_AddBoolToObject(json, "schedule_enabled", settings_get_schedule_enabled());
    /* Extra diagnostic field, not used by the page. */
    int rssi;
    if (wifi_get_rssi(&rssi)) {
        cJSON_AddNumberToObject(json, "rssi", rssi);
    }
    return send_json(req, HTTPD_200, json);
}

/* ---- POST /api/settings ------------------------------------------------ */

/* In-place decoding of an application/x-www-form-urlencoded value. */
static void url_decode(char *s)
{
    char *out = s;
    for (char *in = s; *in; in++) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = { in[1], in[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

typedef enum {
    FIELD_MISSING,      /* absent or empty: "required" error */
    FIELD_PRESENT,      /* copied into the output buffer */
    FIELD_INVALID,      /* present but not a string: "format" error */
} field_result_t;

static field_result_t json_field(const cJSON *root, const char *key, char *out, size_t out_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL || cJSON_IsNull(item) || cJSON_IsFalse(item)) {
        return FIELD_MISSING;
    }
    if (!cJSON_IsString(item)) {
        return FIELD_INVALID;
    }
    if (item->valuestring[0] == '\0') {
        return FIELD_MISSING;
    }
    strlcpy(out, item->valuestring, out_len);
    return FIELD_PRESENT;
}

static field_result_t form_field(const char *body, const char *key, char *out, size_t out_len)
{
    esp_err_t err = httpd_query_key_value(body, key, out, out_len);
    if (err == ESP_ERR_HTTPD_RESULT_TRUNC) {
        return FIELD_INVALID;
    }
    if (err != ESP_OK) {
        return FIELD_MISSING;
    }
    url_decode(out);
    return out[0] ? FIELD_PRESENT : FIELD_MISSING;
}

/* Reads the whole request body into body (MAX_BODY_LEN + 1 bytes).
 * On failure an error response has already been sent, or the socket is
 * dead: the handler must just return the error. */
static esp_err_t read_body(httpd_req_t *req, char *body)
{
    if (req->content_len > MAX_BODY_LEN) {
        send_error(req, "400 Bad Request", "request body too large");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';
    return ESP_OK;
}

static esp_err_t settings_post(httpd_req_t *req)
{
    char body[MAX_BODY_LEN + 1];
    if (read_body(req, body) != ESP_OK) {
        return ESP_FAIL;
    }

    char content_type[64] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));

    /* Values are copied into buffers larger than "HH:MM" so overlong
     * input is rejected by validation instead of being truncated. */
    char open_raw[16] = "", close_raw[16] = "";
    field_result_t open_res, close_res;
    cJSON *root = strstr(content_type, "application/json") ? cJSON_Parse(body) : NULL;
    if (root != NULL) {
        open_res = json_field(root, "open_time", open_raw, sizeof(open_raw));
        close_res = json_field(root, "close_time", close_raw, sizeof(close_raw));
        cJSON_Delete(root);
    } else if (strstr(content_type, "application/json")) {
        /* Invalid JSON is handled like an empty body. */
        open_res = close_res = FIELD_MISSING;
    } else {
        open_res = form_field(body, "open_time", open_raw, sizeof(open_raw));
        close_res = form_field(body, "close_time", close_raw, sizeof(close_raw));
    }

    if (open_res == FIELD_MISSING || close_res == FIELD_MISSING) {
        return send_error(req, "400 Bad Request", "open_time and close_time are required");
    }
    char open_time[SETTINGS_TIME_LEN], close_time[SETTINGS_TIME_LEN];
    if (open_res == FIELD_INVALID || close_res == FIELD_INVALID ||
        !settings_valid_time(open_raw, open_time) || !settings_valid_time(close_raw, close_time)) {
        return send_error(req, "400 Bad Request", "times must be in HH:MM format");
    }

    if (settings_update(open_time, close_time) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "could not save schedule");
    }
    scheduler_reset_last_dates();
    ESP_LOGI(TAG, "Schedule updated: open=%s close=%s", open_time, close_time);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "open_time", open_time);
    cJSON_AddStringToObject(json, "close_time", close_time);
    return send_json(req, HTTPD_200, json);
}

/* ---- POST /api/schedule ------------------------------------------------ */

/* Body: {"enabled": true|false}. Enables/disables the automatic schedule. */
static esp_err_t schedule_post(httpd_req_t *req)
{
    char body[MAX_BODY_LEN + 1];
    if (read_body(req, body) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    bool valid = cJSON_IsBool(item);
    bool enabled = cJSON_IsTrue(item);
    cJSON_Delete(root);
    if (!valid) {
        return send_error(req, "400 Bad Request", "enabled must be true or false");
    }

    if (settings_set_schedule_enabled(enabled) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "could not save schedule");
    }
    ESP_LOGI(TAG, "Schedule %s", enabled ? "enabled" : "disabled");

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "schedule_enabled", enabled);
    return send_json(req, HTTPD_200, json);
}

/* ---- POST /api/manual/<action> ----------------------------------------- */

static esp_err_t manual_post(httpd_req_t *req)
{
    static const char prefix[] = "/api/manual/";
    const char *action = req->uri + strlen(prefix);
    size_t len = strcspn(action, "?#");

    bool started;
    if (len == 4 && strncmp(action, "open", len) == 0) {
        started = shutter_open();
    } else if (len == 5 && strncmp(action, "close", len) == 0) {
        started = shutter_close();
    } else if (len == 4 && strncmp(action, "stop", len) == 0) {
        shutter_stop();
        started = true;
    } else {
        return send_error(req, "400 Bad Request", "unknown action");
    }

    if (!started) {
        return send_error(req, "409 Conflict", "shutter is already moving");
    }
    return send_json(req, HTTPD_200, state_to_json());
}

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 6144;
    /* The page polls /api/status every 2s: recycle the oldest socket
     * instead of refusing new clients when all slots are taken. */
    config.lru_purge_enable = true;

    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post },
        { .uri = "/api/schedule", .method = HTTP_POST, .handler = schedule_post },
        { .uri = "/api/manual/*", .method = HTTP_POST, .handler = manual_post },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &handlers[i]));
    }
    ESP_LOGI(TAG, "HTTP server listening on port %d", HTTP_PORT);
}
