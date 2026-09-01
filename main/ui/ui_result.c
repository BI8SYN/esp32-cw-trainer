#include "ui_result.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_sound.h"
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

    /*
     * 右栏是**固定分区**：提示占上面 52px（两行），错字占下面 96px，按钮从 196 起。
     *
     * 原来两个 label 都是 LONG_WRAP、只给宽不给高，错字于是往下无限长——错得多
     * 的时候（一轮十组，错字可以有二十个）它从 y=40 一路画到 222，把 110 的提示
     * 和 196 的按钮全压在底下，三层字叠在一起。
     *
     * 提示排在上面：它是要照着做的那句话（还差多少能解锁），比错字明细更该先
     * 被看见。错字用 LONG_DOT——装不下就打省略号，而不是顶开别人。省略号丢掉的
     * 信息由标题里的总数补回来。
     */
    if (koch) {
        /* 只在达标时出声。**未达标刻意静音**：Koch 方法的前提就是错着学，一课
           十组错几组是常态，给常态配失败音，练一小时就是几十次惩罚。而「没有
           解锁音」本身已经是反馈。 */
        if (pass) ui_sound(TONE_SEQ_UNLOCK);
        lv_obj_t *pl = lv_label_create(s);
        if (pass) lv_label_set_text_fmt(pl, "达标，可进第 %u 课", (unsigned)(s_t->lesson + 1));
        else lv_label_set_text(pl, "再练练，90% 以上解锁下一课");
        lv_obj_set_size(pl, 160, 52);
        lv_label_set_long_mode(pl, LV_LABEL_LONG_DOT);
        lv_obj_align(pl, LV_ALIGN_TOP_LEFT, 150, 40);
    }

    char err[96] = "";
    if (s_t->error_count == 0) strcpy(err, "全对");
    else {
        /* 带上总数：省略号会截掉后面的明细，数量得留住。 */
        snprintf(err, sizeof err, "错字 %d: ", s_t->error_count);
        for (int i = 0; i < s_t->error_count && strlen(err) < 80; i++) {
            char one[8]; snprintf(one, sizeof one, "%c>%c ", s_t->errors[i].expected, s_t->errors[i].got);
            strcat(err, one);
        }
    }
    lv_obj_t *el = lv_label_create(s);
    lv_obj_set_size(el, 160, koch ? 96 : 148);
    lv_label_set_long_mode(el, LV_LABEL_LONG_DOT);
    lv_label_set_text(el, err);
    lv_obj_align(el, LV_ALIGN_TOP_LEFT, 150, koch ? 96 : 40);

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
