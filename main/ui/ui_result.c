#include "ui_result.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "stats_store.h"
#include "ui_home.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "cw_settings.h"
#include "koch_engine.h"

static const trainer_t *s_t;

static void go_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void again(lv_event_t *e)
{
    (void)e;
    if (s_t->mode == TRAINER_MODE_KOCH) ui_preview_open(s_t->lesson, true);
    else ui_practice_open(s_t->mode);
}
static void next_lesson(lv_event_t *e)
{
    (void)e;
    cw_settings_t *c = cw_settings_get();
    if (c->koch_lesson < KOCH_LESSON_MAX) { c->koch_lesson++; cw_settings_save(); }
    ui_preview_open(c->koch_lesson, false);
}

static lv_obj_t *build(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "本轮结果", NULL);

    uint8_t pct = trainer_percent(s_t);
    bool koch = s_t->mode == TRAINER_MODE_KOCH;
    bool pass = koch && koch_passed(pct);

    lv_obj_t *big = lv_label_create(s);
    lv_obj_set_style_text_font(big, UI_FONT_BIG, 0);
    lv_label_set_text_fmt(big, "%u%%", pct);
    lv_obj_align(big, LV_ALIGN_TOP_LEFT, 16, 40);

    lv_obj_t *cnt = lv_label_create(s);
    lv_label_set_text_fmt(cnt, "%u / %u 正确", s_t->correct_rounds, TRAINER_ROUNDS);
    lv_obj_align(cnt, LV_ALIGN_TOP_LEFT, 16, 130);

    char err[96] = "";
    if (s_t->error_count == 0) strcpy(err, "全对");
    else {
        strcpy(err, "错字: ");
        for (int i = 0; i < s_t->error_count && strlen(err) < 80; i++) {
            char one[8]; snprintf(one, sizeof one, "%c>%c ", s_t->errors[i].expected, s_t->errors[i].got);
            strcat(err, one);
        }
    }
    lv_obj_t *el = lv_label_create(s);
    lv_obj_set_width(el, 160);
    lv_label_set_long_mode(el, LV_LABEL_LONG_WRAP);
    lv_label_set_text(el, err);
    lv_obj_align(el, LV_ALIGN_TOP_LEFT, 150, 40);

    if (koch) {
        lv_obj_t *pl = lv_label_create(s);
        if (pass) lv_label_set_text_fmt(pl, "达标，可进第 %u 课", (unsigned)(s_t->lesson + 1));
        else lv_label_set_text(pl, "再练练，90% 以上解锁下一课");
        lv_obj_set_width(pl, 160);
        lv_label_set_long_mode(pl, LV_LABEL_LONG_WRAP);
        lv_obj_align(pl, LV_ALIGN_TOP_LEFT, 150, 110);
    }

    int n = (pass && s_t->lesson < KOCH_LESSON_MAX) ? 3 : 2;
    int w = (320 - 12 * (n + 1)) / n, x = 12;
    if (n == 3) {
        char t[24]; snprintf(t, sizeof t, "进入第%u课", (unsigned)(s_t->lesson + 1));
        lv_obj_t *b = ui_button(s, t, next_lesson, NULL);
        lv_obj_set_width(b, w); lv_obj_set_pos(b, x, 196); x += w + 12;
    }
    lv_obj_t *b1 = ui_button(s, "再来一轮", again, NULL);
    lv_obj_set_width(b1, w); lv_obj_set_pos(b1, x, 196); x += w + 12;
    lv_obj_t *b2 = ui_button(s, "返回主页", go_home, NULL);
    lv_obj_set_width(b2, w); lv_obj_set_pos(b2, x, 196);
    return s;
}

void ui_result_open(const trainer_t *t)
{
    s_t = t;
    /* 三种模式都记：错字热力图看的是字母，与练的是 Koch 还是呼号无关。 */
    stats_store_record(t);
    if (t->mode == TRAINER_MODE_KOCH) {
        cw_settings_t *c = cw_settings_get();
        c->koch_last_pct[t->lesson] = trainer_percent(t);
        cw_settings_save();
    }
    ui_nav_show(build);
}
