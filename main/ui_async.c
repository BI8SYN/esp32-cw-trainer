#include "ui_async.h"
#include <stdlib.h>
#include "lvgl.h"

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
    if (!m) return;
    m->fn = fn; m->arg = arg;
    lv_async_call(trampoline, m);
}
