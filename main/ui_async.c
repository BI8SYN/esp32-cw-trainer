#include "ui_async.h"
#include <stdlib.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui_async";

typedef struct { ui_async_fn_t fn; void *arg; } msg_t;

static void trampoline(void *p)
{
    msg_t *m = p;
    m->fn(m->arg);
    free(m);
}

void ui_async_post(ui_async_fn_t fn, void *arg)
{
    msg_t *m = malloc(sizeof *m);
    if (!m) {
        ESP_LOGW(TAG, "post dropped: malloc failed");
        return;
    }
    m->fn = fn; m->arg = arg;
    if (lv_async_call(trampoline, m) != LV_RES_OK) {
        ESP_LOGW(TAG, "post dropped: lv_async_call failed");
        free(m);
    }
}
