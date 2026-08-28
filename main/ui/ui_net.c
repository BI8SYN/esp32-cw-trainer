#include "ui_net.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_keyer.h"
#include "ui_wifi.h"
#include "ui_callsign.h"
#include "kob_net.h"
#include "net_store.h"
#include "key_input.h"
#include "cw_player.h"

/*
 * 联网对练页。布局见 spec 6.4，垂直预算是死的：
 *     顶栏 28 + 收报 100 + 状态带 28 + 电键 84 = 240（见 LOG_H / UI_PADDLE_H）
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
/*
 * 收报记录按「发报方的一轮」组织，不是一整块字符串。
 *
 * 原来是一块 char[512]，规则是「换人就换行」。CW 是一个字符一个包，你我交替
 * 发的时候就会被切成一堆碎行：我发一个、对方插一个、我再发一个 = 三行，自己
 * 连发的一串被对方劈开了。改成每条记录属于一个发报方，同一个人在 RUN_GAP_MS
 * 内继续发就接到**他自己那一条**后面，哪怕中间夹了别人的字符。
 *
 * 于是屏幕上是「我一行、对方一行」，各自连贯，这也正是一次 QSO 的样子。
 */
#define LOG_ROWS    32          /* 保留多少条历史 */
#define ROW_TXT     40          /* 一条最多多少字符，超了另起一条 */

/*
 * 收报区高度：整页 240 减去顶栏、状态带和电键，剩多少就是多少。
 *
 * 反推而不是写死，是为了和电键页共用同一个 UI_PADDLE_H——两边各写各的高度，
 * 同一双手在两页之间切换时按的位置就不一样了。原来收报区只有 74（两行），
 * 一次交替就翻页；现在 100，约三行（UI_FONT_CN 行高 26）。
 */
#define BAND_H      28
#define LOG_H       (240 - UI_TOPBAR_H - BAND_H - UI_PADDLE_H)

typedef struct {
    char     who[12];
    char     txt[ROW_TXT + 1];
    uint32_t last_ms;
} log_row_t;

static lv_obj_t  *s_scr, *s_peers, *s_box, *s_band, *s_pad[2], *s_title;

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
/*
 * 本页处于「还没有呼号」状态：不联网，收报区改放一个「设置呼号」按钮。
 *
 * 呼号是上到 wire 上的站点 ID，一条 wire 上两个同名站点分不清谁是谁——顶栏和
 * 收报记录里都只有这一个名字。出厂呼号留空就是为了让这件事必然被看见一次；
 * 老设备上呼号非空、但那是出厂值 BI8SYN（开机会清掉一次），所以判据统一走
 * cw_net_callsign_ok()。
 *
 * 只是显示一个按钮，不是自动弹层：自动弹层要么寄生在别人的屏幕上（踩过，
 * 见 ui_callsign.h），要么把返回路径变成死循环。按钮什么都不劫持。
 */
static bool        s_need_callsign;
static log_row_t   s_rows[LOG_ROWS];
static int         s_row_n;
/* ---------------------------------------------------------------- 记录 */

/*
 * 每条记录是一个独立的行控件：深底浅字的呼号牌 + 自动折行的内容。
 *
 * 原来整块记录是一个 label，呼号只是行首的几个字符，和后面的内容一个颜色、
 * 一样大小，扫一眼分不出哪是谁、哪是他说的话。而 LVGL 的 label 做不到分段
 * 背景——recolor 和 span 都只能改文字颜色，填不了底。所以只能拆成控件。
 *
 * 代价是要自己维护「控件和 s_rows 一一对应」。不能每来一个字符就全量重建：
 * 20 WPM 下每秒十几个字符，32 行 x 2 个对象重建十几次，ESP32 上这点开销
 * 是实打实的。所以结构变化（新起一条、挤掉最老一条）才动控件，往已有的
 * 一条里追加字符只 set_text 那一个 label。
 */
static lv_obj_t *s_rowobj[LOG_ROWS];   /* 行容器，与 s_rows 同下标 */
static lv_obj_t *s_rowtxt[LOG_ROWS];   /* 该行的内容 label */

/* 在 box 里造一行。out_txt 收内容 label；只看不改的地方（历史页）可以传 NULL。 */
static lv_obj_t *row_make(lv_obj_t *box, const char *who, const char *txt,
                          lv_obj_t **out_txt)
{
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    /* 顶对齐：内容折成两行时，呼号牌要停在第一行，不能被扯到中间去。 */
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_set_style_pad_bottom(row, 3, 0);

    lv_obj_t *badge = lv_label_create(row);
    lv_obj_set_style_text_font(badge, UI_FONT_CN, 0);
    lv_obj_set_style_bg_color(badge, UI_BLACK, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(badge, UI_WHITE, 0);
    lv_obj_set_style_pad_hor(badge, 4, 0);
    lv_obj_set_style_radius(badge, 2, 0);
    lv_label_set_text(badge, who);

    lv_obj_t *lb = lv_label_create(row);
    lv_obj_set_style_text_font(lb, UI_FONT_CN, 0);
    /* 撑满呼号牌右边剩下的宽度，内容自己折行。写死宽度的话，呼号长短一变
       就要么裁掉要么留一条空当。 */
    lv_obj_set_flex_grow(lb, 1);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lb, txt);

    if (out_txt) *out_txt = lb;
    return row;
}

/* 把 box 里已有的行控件全部删掉。 */
static void rows_clear(void)
{
    for (int i = 0; i < LOG_ROWS; i++) {
        if (s_rowobj[i]) lv_obj_del(s_rowobj[i]);
        s_rowobj[i] = NULL;
        s_rowtxt[i] = NULL;
    }
}

/* s_rows 挤掉最老一条时，控件要跟着挪，否则下标就对不上了。 */
static void rows_drop_first(void)
{
    if (s_rowobj[0]) lv_obj_del(s_rowobj[0]);
    memmove(&s_rowobj[0], &s_rowobj[1], sizeof s_rowobj[0] * (LOG_ROWS - 1));
    memmove(&s_rowtxt[0], &s_rowtxt[1], sizeof s_rowtxt[0] * (LOG_ROWS - 1));
    s_rowobj[LOG_ROWS - 1] = NULL;
    s_rowtxt[LOG_ROWS - 1] = NULL;
}

/*
 * 让第 i 行的控件和 s_rows[i] 一致，然后按需滚到底。
 *
 * 只有本来就贴着底部时才自动滚。原来是无条件滚到底：你手动往上翻记录，
 * 下一个字一到就被拽回去，等于翻不了。
 */
static void row_sync(int i)
{
    if (!s_box || s_need_callsign || i < 0 || i >= s_row_n) return;

    bool at_bottom = lv_obj_get_scroll_bottom(s_box) <= 4;

    if (!s_rowobj[i]) s_rowobj[i] = row_make(s_box, s_rows[i].who, s_rows[i].txt,
                                             &s_rowtxt[i]);
    else if (s_rowtxt[i]) lv_label_set_text(s_rowtxt[i], s_rows[i].txt);

    if (at_bottom) {
        /* 行高是 LV_SIZE_CONTENT，文字刚改完布局还没重算，直接滚会滚到旧的
           高度上。先结算再滚。 */
        lv_obj_update_layout(s_box);
        lv_obj_scroll_to_y(s_box, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

/* 腾出一条新记录的位置，满了就挤掉最老的。 */
static log_row_t *row_new(const char *who)
{
    if (s_row_n == LOG_ROWS) {
        memmove(&s_rows[0], &s_rows[1], sizeof(log_row_t) * (LOG_ROWS - 1));
        s_row_n = LOG_ROWS - 1;
        rows_drop_first();
    }
    log_row_t *r = &s_rows[s_row_n++];
    snprintf(r->who, sizeof r->who, "%s", who);
    r->txt[0] = '\0';
    r->last_ms = lv_tick_get();
    return r;
}

/*
 * 确保最后一条属于 who，不是就新起一条。
 *
 * 换人就换行——一行就是一个人连着发的一段，这是最直观的读法。之前试过
 * 「往回找同一个人那条接上去」，交替发报时各自的字确实聚在了一起，但新来的字
 * 会插进屏幕上早就滚过去的那一行里，看着莫名其妙。宁可多几行。
 *
 * 幂等：最后一条已经是他的就什么都不做。kob_net 在一轮发报的第一个包到达时
 * 会推一条空内容的记录进来，走的就是这里——呼号牌因此不用等音频。
 */
static void row_open(const char *who)
{
    if (s_row_n > 0 && strcmp(s_rows[s_row_n - 1].who, who) == 0) {
        s_rows[s_row_n - 1].last_ms = lv_tick_get();
        return;
    }
    row_new(who);
    row_sync(s_row_n - 1);
}

/* 追加一个字符到这个人当前这一行。 */
static void append_or_push(const char *who, const char *txt)
{
    bool same = s_row_n > 0 && strcmp(s_rows[s_row_n - 1].who, who) == 0;
    /* 一行装不下就换行，同一个人也一样。 */
    if (same && strlen(s_rows[s_row_n - 1].txt) + strlen(txt) > ROW_TXT) row_new(who);
    else if (!same) row_open(who);

    log_row_t *r = &s_rows[s_row_n - 1];
    strncat(r->txt, txt, ROW_TXT - strlen(r->txt));
    r->last_ms = lv_tick_get();
    row_sync(s_row_n - 1);
}

/* 从 kob_net 的环形队列里取走这一拍的新记录。见 kob_net.h：那边故意不用
   回调，否则 socket 任务会为了 ui_async_post() 去抢 lv_timer_handler() 持有
   的那把锁，与在同一线程里等它退出的 kob_net_stop() 互等。 */
static void drain_lines(void)
{
    kob_net_line_t l;
    while (kob_net_pop_line(&l)) {
        const char *who = l.from[0] ? l.from : "我";
        /* 空内容是「有人开始发了」的信号：只起一条把呼号牌亮出来，内容随后
           跟着声音一个个填。见 kob_net.c 的 on_code()。 */
        if (l.txt[0]) append_or_push(who, l.txt);
        else          row_open(who);
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

    /* 没呼号时本页根本没联网，kob_net 的状态是陈的，照着它刷只会显示一个
       和当下无关的「未连接」。直接说清楚在等什么，电键也置灰。 */
    if (s_need_callsign) {
        lv_label_set_text(s_peers, "");
        lv_label_set_text(s_band, "还没有呼号，设置后才能上线");
        pads_enabled(false);
        return;
    }

    const kob_net_status_t *st = kob_net_status();

    /*
     * 顶栏右侧：线上还有几个人（不含自己）。
     *
     * 只报数，不报呼号。一条 wire 是共线，人数才是这里唯一稳定有用的信息；
     * 呼号最多只塞得下一个，那个「第一个」纯粹取决于谁的 ID 先到，没有意义，
     * 而谁在发报，收报记录每一行前面本来就写着。
     */
    char peers[32];
    snprintf(peers, sizeof peers, "%d 人", st->peer_n);

    /*
     * 同呼号仍然要说出来，只是不再重复那个呼号。两个人用同一个呼号，收发本身
     * 没问题（见 kob_client_rx 的注释），但收报记录里的发报方是同一个名字，
     * 分不清哪句是谁说的。
     */
    for (int i = 0; i < st->peer_n; i++) {
        if (strcmp(st->peers[i], cw_net_office()) == 0) {
            snprintf(peers, sizeof peers, "%d 人 呼号重复", st->peer_n);
            break;
        }
    }
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
        } else {
            /*
             * 在线时这条带子只做一件事：把本机正在打的点划显示清楚。
             *
             * 原来这里在没按键时显示"该你发了"、按下时才显示点划。但
             * i_am_sending 跟的是键的按下/抬起，而形成一个字符的过程中键
             * 大部分时间是抬起的——于是两条文案高频交替，点划根本看不清。
             * 提示语没什么信息量（能发报时本来就该你发），直接去掉。
             *
             * 对方发报时也一样只显示点划，不再插一条「XXX 正在发报 - 按电键
             * 将打断」。那条本意是提醒误触的代价，实际效果是：对方一开始发，
             * 带子就被这句话占满，而这正是你最需要看清自己手法的时候——
             * 抄收的同时想跟着打一遍，点划却没地方显示了。谁在发报顶栏的
             * 站点名已经说了，打断与否是电键本来就有的语义，不必每次复述。
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

/*
 * wire 选择器的遮罩。原来它是靠「选完整页重建」顺带销毁的，现在换 wire 不再
 * 重建页面，必须显式关掉，否则选完弹窗会赖在屏幕上。
 *
 * 用 lv_obj_del_async 而不是 lv_obj_del：关闭动作都是从遮罩**子孙**的事件
 * 回调里发起的，同步删掉祖先会让当前正在执行的那个回调所属的对象在返回前
 * 就被释放。
 */
static lv_obj_t *s_picker;

static void picker_dismiss(void)
{
    if (s_picker) { lv_obj_del_async(s_picker); s_picker = NULL; }
}

static void picker_close(lv_event_t *e) { (void)e; picker_dismiss(); }

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
    picker_dismiss();
    net_store_get()->wire = w;
    net_store_save();

    /*
     * 就地换线：停 socket 任务、按新 wire 重开一遍，页面不重建、WiFi 不重启。
     *
     * 原来这里是 ui_nav_show(ui_net_create)，理由是「整页重建最省事」。代价
     * 在两个地方：一是重建会走 kob_net_stop() -> wifi_net_stop() ->
     * esp_wifi_deinit()，几十 KB 的 WiFi 驱动在 LVGL 事件回调里拆了又建，而
     * wifi_net_stop() 并不等连接 worker 退出，deinit 会从它脚下抽走驱动；
     * 二是 ui_nav_show() 先建新页、下一拍才删旧页，旧页那次迟到的删除会把新页
     * 刚起的网络和定时器一起关掉。换一条线路不该牵动这么多东西。
     *
     * 收报记录还是要清——上一条 wire 上的对话留在屏幕上只会造成误解，
     * 只是现在显式清，不再靠重建顺带。
     */
    s_row_n = 0;
    rows_clear();
    kob_net_rewire();

    /* 方框里的名字变了，宽度也就变了——人数是对齐到方框的，得跟着重排。 */
    if (s_title) {
        lv_label_set_text(s_title, wire_name());
        lv_obj_align(s_title, LV_ALIGN_RIGHT_MID, -6, 0);
    }
    if (s_peers && s_title) lv_obj_align_to(s_peers, s_title, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    apply_status();
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
}

/*
 * 顶栏标题可点，弹出选择。用列表而不是让标题循环切换：循环切换没有可发现性，
 * 用户不点一下不知道有哪些选项、也不知道点下去会去哪。
 */
static void title_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    s_picker = overlay;
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

    lv_obj_t *c = ui_cell(dlg, "取消", UI_FONT_CN, picker_close, NULL);
    lv_obj_set_pos(c, 0, 168);
    lv_obj_set_size(c, 236, 56);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_NONE, 0);
}

/* 收报区那个「设置呼号」按钮。填完回到本页，那时呼号有了，自然就联网了。 */
static void go_callsign(lv_event_t *e)
{
    (void)e;
    ui_callsign_set_return(ui_net_create);
    ui_nav_show(ui_callsign_create);
}

/* ---------------------------------------------------------------- 历史 */

/*
 * 全屏看收报记录。做成遮罩而不是独立页面：换页会走 ui_nav_show()，对练页的
 * on_delete 一触发就 kob_net_stop()，看一眼历史等于把线路断了再连回来。
 *
 * 遮罩是从收报区（当前页上的对象）的事件回调里弹的，所以 lv_scr_act() 就是
 * 对练页本身，这里用它是对的——和呼号那次踩的坑不是一回事，那次是在
 * screen builder 里弹，当时的 lv_scr_act() 还是上一屏。
 */
static lv_obj_t *s_hist;

static void hist_close(lv_event_t *e)
{
    (void)e;
    if (s_hist) { lv_obj_del_async(s_hist); s_hist = NULL; }
}

static void hist_open(lv_event_t *e)
{
    (void)e;
    if (s_hist || s_need_callsign || s_row_n == 0) return;

    s_hist = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_hist);
    lv_obj_set_size(s_hist, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_hist, UI_WHITE, 0);
    lv_obj_set_style_bg_opa(s_hist, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_hist, LV_OBJ_FLAG_SCROLLABLE);

    ui_topbar(s_hist, "收报记录", hist_close);

    /* 顶栏之下全给记录：212px，约 8 行，比页内那 100px 多得多。 */
    lv_obj_t *box = lv_obj_create(s_hist);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, 0, UI_TOPBAR_H);
    lv_obj_set_size(box, 320, 240 - UI_TOPBAR_H);
    lv_obj_set_style_pad_hor(box, 6, 0);
    lv_obj_set_style_pad_top(box, 2, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(box, UI_BLACK, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(box, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(box, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(box, 2, LV_PART_SCROLLBAR);

    /* 和页内用同一个 row_make()，呼号牌的样子不会两处走样。这里是一次性
       建完就不再动的，不需要像页内那样维护下标对应关系。 */
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < s_row_n; i++)
        row_make(box, s_rows[i].who, s_rows[i].txt, NULL);

    /* 打开就停在最新那一条 */
    lv_obj_update_layout(box);
    lv_obj_scroll_to_y(box, LV_COORD_MAX, LV_ANIM_OFF);
}

/* 只在 WiFi 连不上时有效——其他状态下点状态带没有意义，也不该有反应。 */
static void band_cb(lv_event_t *e)
{
    (void)e;
    if (kob_net_status()->state != KOB_NET_ERR_WIFI) return;
    ui_wifi_set_return(ui_net_create);
    ui_nav_show(ui_wifi_create);
}

/* 断网、拆定时器、把页面指针全部作废。离开本页和从本页回到本页都要走一遍。 */
static void teardown(void)
{
    if (s_hist) { lv_obj_del_async(s_hist); s_hist = NULL; }
    kob_net_stop();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_scr = NULL;
    s_pad[0] = s_pad[1] = NULL;
    s_peers = s_box = s_band = s_title = NULL;
    for (int i = 0; i < LOG_ROWS; i++) { s_rowobj[i] = NULL; s_rowtxt[i] = NULL; }
}

static void on_delete(lv_event_t *e)
{
    /*
     * 只有「被删的确实是当前这一页」才收摊。
     *
     * ui_nav_show() 是先建新页、再 lv_obj_del_async() 删旧页——旧页的删除发生
     * 在**下一拍**，那时新页早已建好、网络也起来了。所以从对练页导航回对练页
     * （换 wire、定完呼号）时，这个回调会把新页刚起的网络和刚建的定时器一起
     * 关掉：s_timer 被删、s_scr 置空，poll() 从此直接返回，界面冻在最后画上去
     * 的那一帧再也不动。表现就是「进来了但不连接，只显示一个状态」。
     *
     * 判据用 s_scr：新页在自己的 ui_net_create() 里已经把 s_scr 指向自己，所以
     * 旧页这次迟到的删除会 target != s_scr，直接跳过。真正离开本页时（回主页、
     * 去配网页）没人改过 s_scr，target == s_scr，该收的照收。
     */
    if (lv_event_get_target(e) != s_scr) return;
    teardown();
}

/* ---------------------------------------------------------------- 构建 */

lv_obj_t *ui_net_create(void)
{
    /* 没配过网就先去配网——对练页在这种状态下什么都显示不了。配完回到这里。 */
    if (!net_store_has_wifi()) {
        ui_wifi_set_return(ui_net_create);
        return ui_wifi_create();
    }

    /* 整页的形态由它决定：没呼号就不联网，收报区放按钮。见声明处的说明。 */
    s_need_callsign = !cw_net_callsign_ok();

    /* cw_player 和本页都要用 tone_gen。ui_nav_show 会先建新页再删旧页，
       旧页可能还在播，用 stop_if 只掐属于它自己的那次播放。 */
    cw_player_stop_if(NULL);

    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "", back);

    /*
     * 顶栏右侧从左到右是「人数」「wire 方框」。
     *
     * wire 是这一页唯一的切换入口（设置里只剩数字输入，用于接入没有陪练的
     * 自定义 wire），所以它必须看得出来能点。原来是靠标题后面缀一个 " >"，
     * 沿用项目里「更多练习 >」的约定——但那是列表行，整行都能点，箭头只是
     * 个方向暗示；顶栏里孤零零一个 ">" 太轻，看不出可点。改成描边方框：
     * 边框本身就是「这是个按钮」的意思，不依赖任何字形，也不占额外的高度。
     *
     * 顺序上人数在左、wire 在右：wire 是可操作的，放在最边上手指最好够到；
     * 人数只是读数，挨着它放就行。
     */
    s_title = lv_label_create(bar);
    lv_obj_set_style_text_font(s_title, UI_FONT_CN, 0);
    lv_label_set_text(s_title, wire_name());
    lv_obj_set_style_pad_hor(s_title, 8, 0);
    lv_obj_set_style_pad_ver(s_title, 2, 0);
    lv_obj_set_style_border_width(s_title, 1, 0);
    lv_obj_set_style_border_color(s_title, UI_BLACK, 0);
    lv_obj_set_style_radius(s_title, 3, 0);
    lv_obj_align(s_title, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_CLICKABLE);
    /* 电阻屏漏点常见，扩大可点区域（视觉大小不变） */
    lv_obj_set_ext_click_area(s_title, 12);
    lv_obj_add_event_cb(s_title, title_cb, LV_EVENT_CLICKED, NULL);

    s_peers = lv_label_create(bar);
    lv_obj_set_style_text_font(s_peers, UI_FONT_CN, 0);
    /* 贴在 wire 方框左边。方框宽度随 wire 名字变（「QSO 陪练」和「wire 10」
       差不少），所以对齐到它而不是写死一个 x。 */
    lv_obj_align_to(s_peers, s_title, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_label_set_text(s_peers, "");

    /* 收报区：可滚动容器 + 自动折行的标签。行数由字体和宽度自己决定，
       不再定死列数——定死就会像初版那样把超出的部分直接裁掉。 */
    s_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_pos(s_box, 0, UI_TOPBAR_H);
    lv_obj_set_size(s_box, 320, LOG_H);
    lv_obj_set_style_pad_left(s_box, 6, 0);
    lv_obj_set_style_pad_right(s_box, 6, 0);
    lv_obj_set_style_pad_top(s_box, 2, 0);
    lv_obj_set_scroll_dir(s_box, LV_DIR_VER);
    /*
     * 滚动条常显，不是 AUTO。
     *
     * 这块区域能滚，但没有任何迹象——用户不会去试一个看不出可以滚的地方。
     * AUTO 只在滚动过程中才出现，对「让人知道有更多内容」这件事毫无帮助。
     *
     * 样式要自己写：上面 lv_obj_remove_style_all() 把默认样式连滚动条一起
     * 去掉了，只设 mode 是画不出来的。
     */
    lv_obj_set_scrollbar_mode(s_box, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(s_box, UI_BLACK, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_box, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_box, 2, LV_PART_SCROLLBAR);

    /* 页面重建，记录和行控件一起从头来。控件的宿主 s_box 是刚建的，旧的那些
       已经随上一屏被删掉了，这里只清指针数组。 */
    s_row_n = 0;
    for (int i = 0; i < LOG_ROWS; i++) { s_rowobj[i] = NULL; s_rowtxt[i] = NULL; }

    if (s_need_callsign) {
        /* 收报区本来就是空的，正好用来放这个按钮——用户被挡在哪儿、该做什么，
           都在他正看着的地方。 */
        lv_obj_t *b = ui_button(s_box, "设置呼号", go_callsign, NULL);
        lv_obj_set_size(b, 200, 48);
        lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);
    } else {
        /* 记录一条一行，纵向排。每条是「深底浅字的呼号牌 + 内容」，见 row_make()。 */
        lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);

        /* 点收报区看全部记录。页内只有三行，抄漏一句想回头对的时候不够翻。 */
        lv_obj_add_flag(s_box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_box, hist_open, LV_EVENT_SHORT_CLICKED, NULL);
    }

    /* 状态带 28px：必须 >= UI_FONT_CN 的行高 26，否则文字上下被裁 */
    lv_obj_t *bandbg = lv_obj_create(s_scr);
    lv_obj_remove_style_all(bandbg);
    lv_obj_set_pos(bandbg, 0, UI_TOPBAR_H + LOG_H);
    lv_obj_set_size(bandbg, 320, BAND_H);
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
    int py = UI_TOPBAR_H + LOG_H + BAND_H;
    ui_keyer_paddles(s_scr, py, 240 - py, s_pad);

    if (!s_need_callsign) kob_net_start();
    apply_status();
    s_timer = lv_timer_create(poll, 100, NULL);
    return s_scr;
}
