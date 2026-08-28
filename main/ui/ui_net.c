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
 *     顶栏 28 + 收报 74 + 状态带 28 + 电键 110 = 240
 *
 * 状态带原本是 24px，而里面的标签用 UI_FONT_CN、行高 26——比容器还高，
 * 文字上下被裁掉一点。和配网页输入框那个坑同一类，容器高度必须 >= 行高。
 *
 * 三个结构决定（spec 6.4）：
 *   1. 收报区是**双方**对话记录，不只显示对方——我发出的内容打完落进同一条
 *      流，前缀区分。这样「我刚才发的对不对」有据可查。
 *   2. 状态带永远回答「现在什么情况」：空闲说该你了，对方发时说谁在发，
 *      我发时实时回显。用一个元素解掉新手最主要的两处犹豫。
 *   3. 电键在「对方正在发」时**不禁用**——打断是闭合电路电报的正常交互，
 *      禁用等于把这个功能藏起来。只在断线重连时置灰。
 */

/*
 * 收报区的文本缓冲。
 *
 * 初版把每行定死 38 列、只保留 3 行，用 LV_LABEL_LONG_CLIP 显示——结果是
 * 308px 宽的标签根本放不下 38 个字，超出的部分被直接裁掉、永远看不见。
 *
 * 改成"一个不断增长的缓冲 + LVGL 自己按宽度折行 + 自动滚到底"：显示多少行
 * 由字体和控件宽度自己决定，不需要我去猜；而且往回滚还能看前面抄到的内容，
 * 这在 CW 里是有用的（一句话没抄全，可以回头对）。
 */
#define LOG_CAP  512      /* 缓冲上限 */
#define LOG_KEEP 360      /* 超出后保留的尾部长度 */

static lv_obj_t  *s_scr, *s_peers, *s_box, *s_log, *s_band, *s_pad[2], *s_title;

/*
 * wire 号对用户没有意义，上面有什么才有意义。所以顶栏显示的是名字，
 * 未知的 wire 才退回显示号码。
 */
static const char *wire_name(void)
{
    static char buf[20];
    int16_t w = net_store_get()->wire;
    const char *n = cw_net_wire_label(w);
    if (n) return n;
    snprintf(buf, sizeof buf, "wire %d", w);
    return buf;
}
static lv_timer_t *s_timer;
static char       s_text_buf[LOG_CAP + 1];
static char       s_last_who[24];

/* ---------------------------------------------------------------- 记录 */

static void redraw_log(void)
{
    if (!s_log) return;
    lv_label_set_text(s_log, s_text_buf);
    /* 追加之后滚到底，保证最新的一行总是可见。用户仍然可以手动往回滚。 */
    if (s_box) lv_obj_scroll_to_y(s_box, LV_COORD_MAX, LV_ANIM_OFF);
}

/* 缓冲写满后从头部丢弃，并对齐到一个换行边界——否则第一行会从半截开始，
   看着像乱码。 */
static void trim_buf(void)
{
    size_t len = strlen(s_text_buf);
    if (len <= LOG_CAP) return;
    char *from = s_text_buf + (len - LOG_KEEP);
    char *nl = strchr(from, '\n');
    if (nl) from = nl + 1;
    memmove(s_text_buf, from, strlen(from) + 1);
}

/*
 * 追加一个字符。同一个发报方连续发来的字符接在同一行，换人才起新行——
 * CW 是一个字符一个包，不合并的话每个字母都要占一行。
 */
static void append_or_push(const char *who, const char *txt)
{
    bool same = strcmp(who, s_last_who) == 0;
    size_t len = strlen(s_text_buf);

    if (!same) {
        snprintf(s_last_who, sizeof s_last_who, "%s", who);
        if (len) {
            strncat(s_text_buf, "\n", LOG_CAP - len);
            len = strlen(s_text_buf);
        }
        snprintf(s_text_buf + len, LOG_CAP - len, "%.10s ", who);
        len = strlen(s_text_buf);
    }
    strncat(s_text_buf, txt, LOG_CAP - len);
    trim_buf();
    redraw_log();
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
        snprintf(band, sizeof band, "正在接入 %.16s ...", wire_name());
        pads_on = false;
        break;
    case KOB_NET_ERR_WIFI:
        /* 连不上时最可能的情形是换了地方、这里的网络还没存过。让这行可点，
           直接去配网——用户正卡在这儿，不该让他自己去设置里翻。 */
        snprintf(band, sizeof band, "连不上 %.24s，点这里配网",
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
        } else if (st->sender[0]) {
            /* 不禁用电键，但要明说按下会发生什么——补偿新手误触打断的风险 */
            snprintf(band, sizeof band, "%.16s 正在发报 - 按电键将打断", st->sender);
        } else {
            /*
             * 线路空闲时这条带子只做一件事：把本机正在打的点划显示清楚。
             *
             * 原来这里在没按键时显示"该你发了"、按下时才显示点划。但
             * i_am_sending 跟的是键的按下/抬起，而形成一个字符的过程中键
             * 大部分时间是抬起的——于是两条文案高频交替，点划根本看不清。
             * 提示语没什么信息量（能发报时本来就该你发），直接去掉。
             *
             * 点划之间插空格，和电键页 ui_keyer.c 的显示方式一致，读起来
             * 比 "-..." 挤在一起清楚得多。
             */
            const char *p = key_input_pattern();
            size_t n = 0;
            for (; *p && n + 2 < sizeof band; p++) {
                band[n++] = *p;
                band[n++] = ' ';
            }
            band[n] = '\0';
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

/* ---------------------------------------------------------------- 换 wire */

static void picker_close(lv_event_t *e)
{
    lv_obj_del((lv_obj_t *)lv_event_get_user_data(e));
}

static lv_obj_t *s_num_lbl;
static int16_t   s_num_val;

static void num_adj(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_num_val += d;
    if (s_num_val < 1) s_num_val = 999;
    if (s_num_val > 999) s_num_val = 1;
    lv_label_set_text_fmt(s_num_lbl, "wire %d", s_num_val);
}

static void apply_wire(int16_t w)
{
    net_store_get()->wire = w;
    net_store_save();
    /* 换 wire 等于换一条线路，只能断开重连。整页重建最省事，也顺带把
       收报记录清空——上一条 wire 上的对话留在屏幕上只会造成误解。 */
    ui_nav_show(ui_net_create);
}

static void num_ok(lv_event_t *e) { (void)e; apply_wire(s_num_val); }

/* 手动输入：+/- 调数字。不用键盘——只有一个 1~999 的数，键盘要占掉大半屏，
   而 +/- 在这个量级上更快也更不容易输错。 */
static void picker_manual(lv_event_t *e)
{
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_clean(dlg);
    s_num_val = net_store_get()->wire;

    s_num_lbl = lv_label_create(dlg);
    lv_obj_set_style_text_font(s_num_lbl, UI_FONT_MID, 0);
    lv_label_set_text_fmt(s_num_lbl, "wire %d", s_num_val);
    lv_obj_align(s_num_lbl, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *m = ui_cell(dlg, "-", UI_FONT_MID, num_adj, (void *)(intptr_t)-1);
    lv_obj_set_pos(m, 0, 56);  lv_obj_set_size(m, 78, 56);
    lv_obj_t *p = ui_cell(dlg, "+", UI_FONT_MID, num_adj, (void *)(intptr_t)1);
    lv_obj_set_pos(p, 158, 56); lv_obj_set_size(p, 78, 56);
    lv_obj_t *ok = ui_cell(dlg, "确定", UI_FONT_CN, num_ok, NULL);
    lv_obj_set_pos(ok, 0, 118); lv_obj_set_size(ok, 236, 56);
}

static void picker_pick(lv_event_t *e)
{
    int16_t w = (int16_t)(intptr_t)lv_event_get_user_data(e);
    apply_wire(w);
    /* 换 wire 等于换一条线路，只能断开重连。整页重建最省事，也顺带把
       收报记录清空——上一条 wire 上的对话留在屏幕上只会造成误解。 */
    ui_nav_show(ui_net_create);
}

/*
 * 顶栏标题可点，弹出选择。用列表而不是让标题循环切换：循环切换没有可发现性，
 * 用户不点一下不知道有哪些选项、也不知道点下去会去哪。
 */
static void title_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, UI_WHITE, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(overlay);
    lv_obj_remove_style_all(dlg);
    lv_obj_set_size(dlg, 240, 228);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, UI_WHITE, 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, UI_BLACK, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    /* 三行各 56px，远超 44px 触控下限 */
    const int16_t wires[2] = { CW_NET_QSO_WIRE, CW_NET_ECHO_WIRE };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = ui_cell(dlg, cw_net_wire_label(wires[i]), UI_FONT_CN,
                              picker_pick, (void *)(intptr_t)wires[i]);
        lv_obj_set_pos(c, 0, i * 56);
        lv_obj_set_size(c, 236, 56);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_BOTTOM, 0);
    }
    /* 第三行是手动输入：陪练只占了两条 wire，其余的要能自由接入
       （和别人约一条自己的 wire 对练，或者接国际 wire）。 */
    lv_obj_t *man = ui_cell(dlg, "手动输入", UI_FONT_CN, picker_manual, dlg);
    lv_obj_set_pos(man, 0, 112);
    lv_obj_set_size(man, 236, 56);
    lv_obj_set_style_border_side(man, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *c = ui_cell(dlg, "取消", UI_FONT_CN, picker_close, overlay);
    lv_obj_set_pos(c, 0, 168);
    lv_obj_set_size(c, 236, 56);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_NONE, 0);
}

/* 只在 WiFi 连不上时有效——其他状态下点状态带没有意义，也不该有反应。 */
static void band_cb(lv_event_t *e)
{
    (void)e;
    if (kob_net_status()->state != KOB_NET_ERR_WIFI) return;
    ui_wifi_set_return(ui_net_create);
    ui_nav_show(ui_wifi_create);
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    kob_net_stop();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_scr = NULL;
    s_pad[0] = s_pad[1] = NULL;
    s_peers = s_box = s_log = s_band = NULL;
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

    /* 标题位显示当前 wire 上是什么，点击可换。这是 wire 的唯一切换入口——
       设置里只保留数字输入，用于接入没有陪练的自定义 wire。 */
    lv_obj_t *bar = ui_topbar(s_scr, "", back);
    s_title = lv_label_create(bar);
    lv_obj_set_style_text_font(s_title, UI_FONT_CN, 0);
    /* x=40 而不是 6：返回箭头占 0..36，写 6 会把它压住（ui_topbar 里
       带返回箭头时标题也是从 40 起）。 */
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 40, 0);
    /* 加 ">" 表示可点——项目里「更多练习 >」「设置 >」都是这个约定，
       用户已经认得。字体里没有任何下拉三角（▾ ▼ ∨ 都不在 font20 的
       码点范围内），只能用 ASCII。 */
    lv_label_set_text_fmt(s_title, "%s >", wire_name());
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_CLICKABLE);
    /* 电阻屏漏点常见，扩大可点区域（视觉大小不变） */
    lv_obj_set_ext_click_area(s_title, 12);
    lv_obj_add_event_cb(s_title, title_cb, LV_EVENT_CLICKED, NULL);

    s_peers = lv_label_create(bar);
    lv_obj_set_style_text_font(s_peers, UI_FONT_CN, 0);
    /* 右侧没有其他控件（返回是左边的箭头），靠到最右 */
    lv_obj_align(s_peers, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_label_set_text(s_peers, "");

    /* 收报区 74px：可滚动容器 + 自动折行的标签。行数由字体和宽度自己决定，
       不再定死列数——定死就会像初版那样把超出的部分直接裁掉。 */
    s_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_pos(s_box, 0, UI_TOPBAR_H);
    lv_obj_set_size(s_box, 320, 74);
    lv_obj_set_style_pad_left(s_box, 6, 0);
    lv_obj_set_style_pad_right(s_box, 6, 0);
    lv_obj_set_style_pad_top(s_box, 2, 0);
    lv_obj_set_scroll_dir(s_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_box, LV_SCROLLBAR_MODE_OFF);

    s_log = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_log, UI_FONT_CN, 0);
    lv_obj_set_width(s_log, 308);
    lv_label_set_long_mode(s_log, LV_LABEL_LONG_WRAP);
    s_text_buf[0] = '\0';
    s_last_who[0] = '\0';

    /*
     * 首次进入时把侧音底噪的提示作为记录的第一行。spec 6.2 要求这条必须告知、
     * 不能藏着：联网期间 WiFi 的电流尖峰可能窜进内置 DAC，侧音出现底噪属正常，
     * 离线练习不受影响。
     *
     * 用记录的第一行而不是 ui_notice()：本页垂直预算 28+74+28+110 已经把 240px
     * 用满，ui_notice() 会占掉 22px 挤垮布局。放进记录里零成本，而且随着对话
     * 往上滚自然消失，不需要用户去关它。
     */
    cw_net_cfg_t *ncfg = net_store_get();
    if (!ncfg->noise_notice_shown) {
        snprintf(s_text_buf, sizeof s_text_buf,
                 "提示 联网时侧音可能有底噪，属正常。离线练习不受影响。");
        ncfg->noise_notice_shown = 1;
        net_store_save();
    }
    lv_label_set_text(s_log, s_text_buf);

    /* 状态带 28px：必须 >= UI_FONT_CN 的行高 26，否则文字上下被裁 */
    lv_obj_t *bandbg = lv_obj_create(s_scr);
    lv_obj_remove_style_all(bandbg);
    lv_obj_set_pos(bandbg, 0, UI_TOPBAR_H + 74);
    lv_obj_set_size(bandbg, 320, 28);
    lv_obj_set_style_bg_color(bandbg, UI_GRAY_BG, 0);
    lv_obj_set_style_bg_opa(bandbg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bandbg, 1, 0);
    lv_obj_set_style_border_color(bandbg, UI_BLACK, 0);
    lv_obj_set_style_border_side(bandbg, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_add_flag(bandbg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bandbg, band_cb, LV_EVENT_CLICKED, NULL);

    s_band = lv_label_create(bandbg);
    lv_obj_set_style_text_font(s_band, UI_FONT_CN, 0);
    lv_obj_align(s_band, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(s_band, "");

    /* 电键 110px：两块 160x110，远超 44px 触控下限。
       用电键页导出的同一个构造函数，不再复制一份——之前那份把字体写成了
       UI_FONT_MID，而 font28 没有中文字形，"点""划"渲染成了豆腐块。 */
    int py = UI_TOPBAR_H + 74 + 28;
    ui_keyer_paddles(s_scr, py, 240 - py, s_pad);

    kob_net_start();
    apply_status();
    s_timer = lv_timer_create(poll, 100, NULL);
    return s_scr;
}
