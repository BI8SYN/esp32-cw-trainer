#include "ui_callsign.h"
#include <stdio.h>
#include <string.h>
#include "ui_home.h"
#include "cw_settings.h"
#include "net_store.h"

static char                s_buf[12];
static lv_obj_t           *s_label;
static lv_obj_t           *s_hint;
static ui_screen_builder_t s_back_to;

void ui_callsign_set_return(ui_screen_builder_t back_to) { s_back_to = back_to; }

static void leave(void)
{
    ui_nav_show(s_back_to ? s_back_to : ui_home_create);
}

static void back_cb(lv_event_t *e) { (void)e; leave(); }

static void key_cb(lv_event_t *e)
{
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    size_t l = strlen(s_buf);

    if (!strcmp(k, "退格")) {
        if (l) s_buf[l - 1] = 0;
        lv_label_set_text(s_hint, "");
    } else if (!strcmp(k, "确定")) {
        /* 长度不足时不能悄悄放弃：停在本页、给出提示，不导航离开——否则
           操作者看到页面正常返回，会以为已经保存成功。 */
        if (l < 3) { lv_label_set_text(s_hint, "呼号至少 3 个字符，未保存"); return; }
        strcpy(cw_settings_get()->callsign, s_buf);
        cw_settings_save();
        /* 「用户亲自定过呼号」的唯一来源。开机清理遗留出厂呼号那一步看它，
           保证只清一次——BI8SYN 是个合法呼号，真有人叫这个。 */
        cw_net_callsign_confirm();
        leave();
        return;
    } else if (l < 10) {
        s_buf[l] = k[0]; s_buf[l + 1] = 0;
        lv_label_set_text(s_hint, "");
    }
    lv_label_set_text(s_label, s_buf[0] ? s_buf : "-");
}

lv_obj_t *ui_callsign_create(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "我的呼号", back_cb);

    snprintf(s_buf, sizeof s_buf, "%s", cw_settings_get()->callsign);

    s_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_label, UI_FONT_MID, 0);
    lv_label_set_text(s_label, s_buf[0] ? s_buf : "-");
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + 2);

    /* 这一行平时是空的，只在「确定」被按下而长度不够时才出声。 */
    s_hint = lv_label_create(s);
    lv_obj_set_style_text_font(s_hint, UI_FONT_CN, 0);
    lv_label_set_text(s_hint, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + 22);

    /* 10 列 x 32px = 320 铺满屏宽；4 行 x 42px 从 y=68 排到 236，正好放得下。 */
    const char *keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const int cols = 10, cw = 32, ch = 42, y0 = UI_TOPBAR_H + 40;
    for (int i = 0; i < 36; i++) {
        char t[2] = { keys[i], 0 };
        lv_obj_t *k = ui_cell(s, t, UI_FONT_CN, key_cb, NULL);
        lv_obj_set_pos(k, (i % cols) * cw, y0 + (i / cols) * ch);
        lv_obj_set_size(k, cw, ch);
    }
    /* 第四行只用掉前 6 格（30~35），退格/确定接在后面。 */
    lv_obj_t *bs = ui_cell(s, "退格", UI_FONT_CN, key_cb, NULL);
    lv_obj_set_pos(bs, 6 * cw, y0 + 3 * ch); lv_obj_set_size(bs, 2 * cw, ch);
    lv_obj_t *ok = ui_cell(s, "确定", UI_FONT_CN, key_cb, NULL);
    lv_obj_set_pos(ok, 8 * cw, y0 + 3 * ch); lv_obj_set_size(ok, 2 * cw, ch);

    return s;
}
