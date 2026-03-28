#include "at_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "modem_uart.h"
#include "sdkconfig.h"

static const char *TAG = "at_client";

typedef struct {
    const char *cmd;
    char *response;
    size_t response_cap;
    int timeout_ms;
    int retries;
    esp_err_t out_err;
    SemaphoreHandle_t done;
} at_job_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_api_lock;
static at_urc_handler_t s_urc_handler;
static void *s_urc_ctx;
static bool s_running;

static bool line_is_urc(const char *line)
{
    if (line[0] == '+') {
        return true;
    }
    if (line[0] == '^') {
        return true;
    }
    return false;
}

static esp_err_t read_line(char *line, size_t cap, int64_t deadline_us)
{
    size_t i = 0;
    while (i < cap - 1) {
        int64_t now = esp_timer_get_time();
        if (now >= deadline_us) {
            return ESP_ERR_TIMEOUT;
        }
        int wait_ms = (int)((deadline_us - now) / 1000);
        if (wait_ms < 1) {
            wait_ms = 1;
        }
        if (wait_ms > 500) {
            wait_ms = 500;
        }
        uint8_t ch;
        int n = modem_uart_read(&ch, 1, pdMS_TO_TICKS(wait_ms));
        if (n <= 0) {
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[i] = '\0';
            return ESP_OK;
        }
        line[i++] = (char)ch;
    }
    return ESP_ERR_NO_MEM;
}

static bool result_line(const char *line)
{
    if (strcmp(line, "OK") == 0) {
        return true;
    }
    if (strcmp(line, "ERROR") == 0) {
        return true;
    }
    if (strncmp(line, "+CME ERROR:", 11) == 0) {
        return true;
    }
    if (strncmp(line, "+CMS ERROR:", 11) == 0) {
        return true;
    }
    return false;
}

static esp_err_t run_job_once(const at_job_t *job)
{
    modem_uart_drain();

    char send_buf[CONFIG_AT_CLIENT_MAX_LINE_LEN + 8];
    const char *cmd = job->cmd;
    size_t len = strlen(cmd);
    if (len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len >= sizeof(send_buf) - 3) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(send_buf, cmd, len);
    if (len < 2 || strcmp(cmd + len - 2, "\r\n") != 0) {
        memcpy(send_buf + len, "\r\n", 3);
    } else {
        send_buf[len] = '\0';
    }

    int w = modem_uart_write((const uint8_t *)send_buf, strlen(send_buf));
    if (w < 0) {
        return ESP_FAIL;
    }

    char line[CONFIG_AT_CLIENT_MAX_LINE_LEN];
    char *agg = job->response;
    size_t agg_cap = job->response_cap;
    size_t agg_len = 0;
    if (agg && agg_cap) {
        agg[0] = '\0';
    }

    int64_t deadline = esp_timer_get_time() + (int64_t)job->timeout_ms * 1000;

    for (;;) {
        esp_err_t err = read_line(line, sizeof(line), deadline);
        if (err != ESP_OK) {
            return err;
        }

        if (line[0] == '\0') {
            continue;
        }

        ESP_LOGD(TAG, "line: %s", line);

        if (result_line(line)) {
            if (strcmp(line, "OK") == 0) {
                return ESP_OK;
            }
            return ESP_FAIL;
        }

        if (line_is_urc(line) && s_urc_handler) {
            s_urc_handler(line, s_urc_ctx);
        }

        if (agg && agg_cap > 1) {
            size_t l = strlen(line);
            if (agg_len > 0 && agg_len + 1 < agg_cap) {
                agg[agg_len++] = '\n';
                agg[agg_len] = '\0';
            }
            if (agg_len + l < agg_cap) {
                memcpy(agg + agg_len, line, l + 1);
                agg_len += l;
            }
        }
    }
}

static void modem_task(void *arg)
{
    (void)arg;
    for (;;) {
        at_job_t *job = NULL;
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE || job == NULL) {
            continue;
        }

        esp_err_t final = ESP_FAIL;
        int tries = job->retries + 1;
        for (int t = 0; t < tries; t++) {
            final = run_job_once(job);
            if (final == ESP_OK) {
                break;
            }
            if (final == ESP_ERR_TIMEOUT && t + 1 < tries) {
                ESP_LOGW(TAG, "retry cmd (timeout) %s", job->cmd);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            if (final != ESP_OK && t + 1 < tries) {
                ESP_LOGW(TAG, "retry cmd (err=%s) %s", esp_err_to_name(final), job->cmd);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            break;
        }
        job->out_err = final;
        xSemaphoreGive(job->done);
    }
}

esp_err_t at_client_init(void)
{
    if (s_running) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(CONFIG_AT_CLIENT_QUEUE_DEPTH, sizeof(at_job_t *));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_api_lock = xSemaphoreCreateMutex();
    if (s_api_lock == NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(modem_task, "modem_at", CONFIG_AT_CLIENT_TASK_STACK, NULL,
                                CONFIG_AT_CLIENT_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        vSemaphoreDelete(s_api_lock);
        s_api_lock = NULL;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }
    s_running = true;
    ESP_LOGI(TAG, "modem task started");
    return ESP_OK;
}

void at_client_deinit(void)
{
    if (!s_running) {
        return;
    }
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_api_lock) {
        vSemaphoreDelete(s_api_lock);
        s_api_lock = NULL;
    }
    s_running = false;
}

void at_client_set_urc_handler(at_urc_handler_t handler, void *user_ctx)
{
    s_urc_handler = handler;
    s_urc_ctx = user_ctx;
}

esp_err_t at_client_cmd(const char *cmd, char *response, size_t response_cap, int timeout_ms,
                        int retries)
{
    if (!s_running || cmd == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        xSemaphoreGive(s_api_lock);
        return ESP_ERR_NO_MEM;
    }

    at_job_t job = {
        .cmd = cmd,
        .response = response,
        .response_cap = response ? response_cap : 0,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS,
        .retries = retries < 0 ? CONFIG_AT_CLIENT_DEFAULT_RETRIES : retries,
        .out_err = ESP_FAIL,
        .done = done,
    };

    at_job_t *pjob = &job;
    if (xQueueSend(s_queue, &pjob, pdMS_TO_TICKS(5000)) != pdTRUE) {
        vSemaphoreDelete(done);
        xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }

    int wait_ms = job.timeout_ms * (job.retries + 2) + 2000;
    if (xSemaphoreTake(done, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "command wait timeout: %s", cmd);
        vSemaphoreDelete(done);
        xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(done);
    esp_err_t e = job.out_err;
    xSemaphoreGive(s_api_lock);
    return e;
}

esp_err_t at_client_cmd_simple(const char *cmd, char *response, size_t response_cap)
{
    return at_client_cmd(cmd, response, response_cap, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS,
                         CONFIG_AT_CLIENT_DEFAULT_RETRIES);
}
