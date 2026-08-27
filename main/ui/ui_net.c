#include "ui_net.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_keyer.h"
#include "ui_wifi.h"
#include "kob_net.h"
#include "net_store.h"
#include "key_input.h"
#include "cw_player.h"

/*
 * 联网对练页。布局见 spec 6.4，垂直预算是死的：
 *     顶栏 28 + 收报 78 + 状态带 24 + 电键 110 = 240
 *
 * 三个结构决定（spec 6.4）：
 *   1. 收报区是**双方**对话记录，不只显示对方——我发出的内容打完落进同一条
 *      流，前缀区分。这样「我刚才发的对不对」有据可查。
 *   2. 状态带永远回答「现在什么情况」：空闲说该你了，对方发时说谁在发，
 *      我发时实时回显。用一个元素解掉新手最主要的两处犹豫。
 *   3. 电键在「对方正在发」时**不禁用**——打断是闭合电路电报的正常交互，
 *      禁用等于把这个功能藏起来。只在断线重连时置灰。
 */

#define LOG_LINES 3
#define LOG_COLS  38

static lv_obj_t  *s_scr, *s_peers, *s_log, *s_band, *s_pad[2];
static lv_timer_t *s_timer;
static char       s_lines[LOG_LINES][LOG_COLS + 1];
static int        s_line_n;

/* ---------------------------------------------------------------- 记录 */

static void redraw_log(void)
{
    if (!s_log) return;
    char all[LOG_LINES * (LOG_COLS + 2) + 1] = "";
    for (int i = 0; i < s_line_n; i++) {
        strncat(all, s_lines[i], sizeof all - strlen(all) - 2);
        if (i < s_line_n - 1) strncat(all, "\n", sizeof all - strlen(all) - 1);
    }
    lv_label_set_text(s_log, all);
}

static void push_line(const char *who, const char *txt)
{
    if (s_line_n == LOG_LINES) {
        memmove(s_lines[0], s_lines[1], (LOG_LINES - 1) * (LOG_COLS + 1));
        s_line_n--;
    }
    snprintf(s_lines[s_line_n], LOG_COLS + 1, "%-7s %s", who, txt);
    s_line_n++;
    redraw_log();
}

/*
 * 同一个发报方连续发来的字符要接到同一行，而不是每个字符一行——CW 是一个
 * 字符一个包，不合并的话三行记录只能显示三个字母。
 */
static void append_or_push(const char *who, const char *txt)
{
    if (s_line_n > 0 && strncmp(s_lines[s_line_n - 1], who, strlen(who)) == 0) {
        size_t l = strlen(s_lines[s_line_n - 1]);
        if (l + strlen(txt) < LOG_COLS) {
            strcat(s_lines[s_line_n - 1], txt);
            redraw_log();
            return;
        }
    }
    push_line(who, txt);
}

/* 从 kob_net 的环形队列里取走这一拍的新记录。见 kob_net.h：那边故意不用
   回调，否则 socket 任务会为了 ui_async_post() 去抢 lv_timer_handler() 持有
   的那把锁，与在同一线程里等它退出的 kob_net_stop() 互等。 */
static void drain_lines(void)
{
    kob_net_line_t l;
    while (kob_net_pop_line(&l)) {
        append_or_push(l.from[0] ? l.from : "我", l.txt);
    }
}

/* ---------------------------------------------------------------- 状态 */

static void pads_enabled(bool on)
{
    for (int i = 0; i < 2; i++) {
        if (!s_pad[i]) continue;
        if (on) lv_obj_add_flag(s_pad[i], LV_OBJ_FLAG_CLICKABLE);
        else    lv_obj_clear_flag(s_pad[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(s_pad[i], on ? UI_BLACK : UI_GRAY_DOWN, 0);
    }
}

static void apply_status(void)
{
    if (!s_scr) return;
    const kob_net_status_t *st = kob_net_status();

    /* 顶栏右侧：谁在线 */
    char peers[40];
    if (st->peer_n == 0) snprintf(peers, sizeof peers, "0 人");
    else if (st->peer_n == 1) snprintf(peers, sizeof peers, "%.24s", st->peers[0]);
    else snprintf(peers, sizeof peers, "%.18s +%d", st->peers[0], st->peer_n - 1);
    lv_label_set_text(s_peers, peers);

    /* 状态带：永远回答「现在什么情况」 */
    char band[64];
    bool pads_on = true;
    switch (st->state) {
    case KOB_NET_WIFI:
        snprintf(band, sizeof band, "正在连接 %.32s ...",
                 st->wifi_ssid[0] ? st->wifi_ssid : "WiFi");
        pads_on = false;
        break;
    case KOB_NET_CONNECTING:
        snprintf(band, sizeof band, "正在接入 wire %d ...", net_store_get()->wire);
        pads_on = false;
        break;
    case KOB_NET_ERR_WIFI:
        snprintf(band, sizeof band, "连不上 %.32s",
                 st->wifi_ssid[0] ? st->wifi_ssid : "WiFi");
        pads_on = false;
        break;
    case KOB_NET_ERR_SERVER:
        snprintf(band, sizeof band, "连不上服务器 %.32s", net_store_get()->host);
        pads_on = false;
        break;
    case KOB_NET_RECONNECTING:
        snprintf(band, sizeof band, "连接中断，正在重连 ...");
        pads_on = false;     /* 唯一一处置灰电键的状态 */
        break;
    case KOB_NET_ONLINE:
        if (st->broken) {
            snprintf(band, sizeof band, "%.16s 打断了你 - 松开电键让位", st->sender);
        } else if (st->i_am_sending) {
            snprintf(band, sizeof band, "我 %.24s", key_input_pattern());
        } else if (st->sender[0]) {
            /* 不禁用电键，但要明说按下会发生什么——补偿新手误触打断的风险 */
            snprintf(band, sizeof band, "%.16s 正在发报 - 按电键将打断", st->sender);
        } else {
            snprintf(band, sizeof band, "该你发了");
        }
        break;
    default:
        snprintf(band, sizeof band, "未连接");
        pads_on = false;
        break;
    }
    lv_label_set_text(s_band, band);
    pads_enabled(pads_on);
}

/* 唯一的 UI 更新入口：取记录 + 刷状态。100ms 一拍，对状态带里「我正在发」
   的点划回显也够快。 */
static void poll(lv_timer_t *t)
{
    (void)t;
    if (!s_scr) return;
    drain_lines();
    apply_status();
}

/* ---------------------------------------------------------------- 交互 */

static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    kob_net_stop();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_scr = NULL;
    s_pad[0] = s_pad[1] = NULL;
    s_peers = s_log = s_band = NULL;
}

/* ---------------------------------------------------------------- 构建 */

lv_obj_t *ui_net_create(void)
{
    /* 没配过网就先去配网——对练页在这种状态下什么都显示不了。 */
    if (!net_store_has_wifi()) return ui_wifi_create();

    /* cw_player 和本页都要用 tone_gen。ui_nav_show 会先建新页再删旧页，
       旧页可能还在播，用 stop_if 只掐属于它自己的那次播放。 */
    cw_player_stop_if(NULL);

    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    s_line_n = 0;

    lv_obj_t *bar = ui_topbar(s_scr, "联网对练", back);
    s_peers = lv_label_create(bar);
    lv_obj_set_style_text_font(s_peers, UI_FONT_CN, 0);
    lv_obj_align(s_peers, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_label_set_text(s_peers, "");

    /* 收报区 78px：3 行对话记录 */
    s_log = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_log, UI_FONT_CN, 0);
    lv_obj_set_pos(s_log, 6, UI_TOPBAR_H + 4);
    lv_obj_set_size(s_log, 308, 74);
    lv_label_set_long_mode(s_log, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_log, "");

    /* 状态带 24px */
    lv_obj_t *bandbg = lv_obj_create(s_scr);
    lv_obj_remove_style_all(bandbg);
    lv_obj_set_pos(bandbg, 0, UI_TOPBAR_H + 78);
    lv_obj_set_size(bandbg, 320, 24);
    lv_obj_set_style_bg_color(bandbg, UI_GRAY_BG, 0);
    lv_obj_set_style_bg_opa(bandbg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bandbg, 1, 0);
    lv_obj_set_style_border_color(bandbg, UI_BLACK, 0);
    lv_obj_set_style_border_side(bandbg, LV_BORDER_SIDE_BOTTOM, 0);

    s_band = lv_label_create(bandbg);
    lv_obj_set_style_text_font(s_band, UI_FONT_CN, 0);
    lv_obj_align(s_band, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(s_band, "");

    /* 电键 110px：两块 160x110，远超 44px 触控下限。
       用电键页导出的同一个构造函数，不再复制一份——之前那份把字体写成了
       UI_FONT_MID，而 font28 没有中文字形，"点""划"渲染成了豆腐块。 */
    int py = UI_TOPBAR_H + 78 + 24;
    ui_keyer_paddles(s_scr, py, 240 - py, s_pad);

    kob_net_start();
    apply_status();
    s_timer = lv_timer_create(poll, 100, NULL);
    return s_scr;
}
