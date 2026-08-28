#include "ui_ota.h"
#include <stdio.h>
#include "ui_common.h"
#include "ui_settings.h"
#include "ota.h"
#include "net_store.h"
#include "wifi_net.h"

/*
 * 固件升级页。
 *
 * 升级要联网，而本机 WiFi 是按需开的（spec 3.5：常连会让 WiFi 的电流尖峰
 * 窜进内置 DAC，拿主线场景的侧音质量去换可选功能的便利）。所以进这一页
 * 才开射频，退出就关。
 *
 * 失败不可怕，写坏才可怕——所以流程是「下载并校验通过才切槽，重启后由
 * app_main 的 ota_mark_valid() 确认」。没确认就崩溃或断电的话 bootloader
 * 会自动回滚回旧槽（CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE）。这是敢把这个
 * 按钮交给用户的唯一理由。
 */

static lv_obj_t *s_scr, *s_msg, *s_bar, *s_btn, *s_url, *s_latest;
static lv_timer_t *s_timer;

static void refresh(void)
{
    if (!s_scr) return;
    const ota_status_t *st = ota_status();
    const ota_check_t *ck = ota_check_result();

    /*
     * 两行平行的标注：当前版本 / 最新版本。不显示固件地址——用户改不了它，
     * 看了也没有任何可做的事。也不说"服务器 vX"：用户关心的是"最新的是
     * 哪个"，服务器只是它碰巧待的地方。
     */
    lv_label_set_text_fmt(s_url, "当前版本  %s", ota_current_version());
    if (ota_check_busy())      lv_label_set_text(s_latest, "最新版本  检查中...");
    else if (!ck->checked)     lv_label_set_text(s_latest, "最新版本  --");
    else if (ck->err[0])       lv_label_set_text_fmt(s_latest, "最新版本  %s", ck->err);
    else                       lv_label_set_text_fmt(s_latest, "最新版本  %s", ck->latest);

    switch (st->state) {
    case OTA_IDLE:
        lv_label_set_text(s_msg, "");
        break;
    case OTA_CONNECTING:
    case OTA_DOWNLOADING:
        if (st->percent >= 0)
            lv_label_set_text_fmt(s_msg, "%s %d%%", st->msg, st->percent);
        else
            lv_label_set_text(s_msg, st->msg);
        break;
    case OTA_DONE:
        lv_label_set_text(s_msg, "完成，重启后生效");
        break;
    case OTA_FAIL:
        lv_label_set_text_fmt(s_msg, "失败：%s", st->msg);
        break;
    }

    int pct = st->percent < 0 ? 0 : st->percent;
    lv_obj_set_width(s_bar, 4 + pct * 300 / 100);

    lv_obj_t *lbl = lv_obj_get_child(s_btn, 0);
    if (st->state == OTA_DONE) lv_label_set_text(lbl, "重启");
    else if (st->state == OTA_DOWNLOADING || st->state == OTA_CONNECTING)
        lv_label_set_text(lbl, "进行中");
    else if (ota_check_busy()) lv_label_set_text(lbl, "检查中");
    else if (ck->checked && ck->available) lv_label_set_text(lbl, "升级");
    else lv_label_set_text(lbl, "重新检查");
}

static void poll(lv_timer_t *t) { (void)t; refresh(); }

static void btn_cb(lv_event_t *e)
{
    (void)e;
    const ota_status_t *st = ota_status();
    if (st->state == OTA_DONE) { ota_reboot(); return; }
    if (st->state == OTA_DOWNLOADING || st->state == OTA_CONNECTING) return;
    if (ota_check_busy()) return;

    const ota_check_t *ck = ota_check_result();
    /* 没有新版本时这个按钮的意思是"再查一次"——比灰掉一个按钮让用户
       无事可做要好。 */
    if (!ck->checked || !ck->available) ota_check_start();
    else ota_start(NULL);          /* 用检查拿到的那个地址 */
    refresh();
}

static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_settings_create); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    /* 下载进行中不关射频，否则会把自己的连接掐断。用户返回只是不看了，
       任务还在后台跑完；下次进来还能看到结果。 */
    if (ota_status()->state != OTA_DOWNLOADING && ota_status()->state != OTA_CONNECTING
        && !ota_check_busy())
        wifi_net_stop();
    s_scr = NULL; s_msg = s_bar = s_btn = s_url = s_latest = NULL;
}

lv_obj_t *ui_ota_create(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    ui_topbar(s_scr, "固件升级", back);

    s_url = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_url, UI_FONT_CN, 0);
    lv_obj_set_pos(s_url, 6, UI_TOPBAR_H + 6);
    lv_obj_set_width(s_url, 308);
    lv_label_set_long_mode(s_url, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_url, "");

    s_latest = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_latest, UI_FONT_CN, 0);
    lv_obj_set_pos(s_latest, 6, UI_TOPBAR_H + 36);
    lv_obj_set_width(s_latest, 308);
    lv_label_set_long_mode(s_latest, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_latest, "");

    s_msg = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_msg, UI_FONT_CN, 0);
    lv_obj_set_pos(s_msg, 6, UI_TOPBAR_H + 68);
    lv_label_set_text(s_msg, "");

    /* 进度条：外框固定，内条宽度随进度变 */
    lv_obj_t *frame = lv_obj_create(s_scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_pos(frame, 6, UI_TOPBAR_H + 104);
    lv_obj_set_size(frame, 308, 16);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, UI_BLACK, 0);

    s_bar = lv_obj_create(frame);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_size(s_bar, 4, 14);
    lv_obj_set_style_bg_color(s_bar, UI_GRAY_DOWN, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);

    s_btn = ui_button(s_scr, "开始升级", btn_cb, NULL);
    lv_obj_set_size(s_btn, 160, 40);
    lv_obj_align(s_btn, LV_ALIGN_BOTTOM_MID, 0, -12);

    /* 升级要联网，而射频平时是关的（见文件顶部）。 */
    wifi_net_init();
    wifi_net_start(NULL, NULL);
    /* 进页面就自动查一次——用户点进来就是想知道"有没有新的"，
       让他再点一次按钮才开始查是多余的一步。 */
    if (ota_status()->state == OTA_IDLE || ota_status()->state == OTA_FAIL)
        ota_check_start();

    refresh();
    s_timer = lv_timer_create(poll, 300, NULL);
    return s_scr;
}
