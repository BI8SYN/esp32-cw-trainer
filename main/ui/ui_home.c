#include "ui_home.h"
#include <stdio.h>
#include <time.h>
#include "ui_common.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "ui_keyer.h"
#include "ui_net.h"
#include "ui_stats.h"
#include "ui_icons.h"
#include "ui_more.h"
#include "wifi_net.h"
#include "ota.h"
#include "ui_ota.h"
#include "ui_settings.h"
#include "cw_settings.h"
#include "koch_engine.h"
#include "backlight.h"
#include "battery.h"

#define IDLE_OFF_MS 120000

/* 主题按下态那套 grow + transition 的处理搬去了 ui_common.c 的 make_btn()——
   本页原来自己抄了一份，是因为 Koch 卡片当时是个 lv_btn。现在卡片改成了不可点
   的内容井，页面上所有的按钮都走 ui_cell()，这里不必再留一份。 */
static lv_obj_t *s_scr, *s_clock, *s_bat, *s_bat_icon, *s_wifi_icon;
static lv_timer_t *s_timer;
static bool s_dimmed;
static uint32_t s_last_idle;
static bool s_reset_notice_shown;
static bool s_ota_notice_shown;

/* 灭屏期间显示和触摸层其实一直是活的：只是背光被设成 0，控件树、点击事件都
 * 照常工作。这意味着"把屏幕点亮的那一下"本身就会被当成一次普通点击送到
 * 底下的控件——Koch 卡片占了主页大半个屏幕，几乎每次唤醒都会因此顺手点开一课、
 * 出声。各入口的点击回调开头都先调用这个函数：如果当前确实是灭屏状态，就地
 * 点亮背光、清 s_dimmed，并返回 true 让调用方直接 return、不再执行正常的跳转/
 * 播放动作——这一下点击视为"仅用于唤醒"，被吃掉。s_dimmed 在函数内部就地清零，
 * 下一次点击进来时已经是正常状态，不会出现"点第一下没反应，之后怎么点都没
 * 反应"的卡死。 */
static bool wake_swallow(void)
{
    if (!s_dimmed) return false;
    backlight_set(cw_settings_get()->brightness);
    s_dimmed = false;
    return true;
}

static void tick(lv_timer_t *t)
{
    if (s_wifi_icon) ui_icon_wifi_set(s_wifi_icon, wifi_net_level());
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year > 100) lv_label_set_text_fmt(s_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else lv_label_set_text(s_clock, "--:--");

    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (!s_dimmed && idle > IDLE_OFF_MS) {
        backlight_set(0);
        s_dimmed = true;
    } else if (s_dimmed && idle < s_last_idle) {
        /* 原来是 "idle < 1000" 与这个每 1000ms 才跑一次的定时器比对：这个窗口
         * 假设了"上次检查到现在必然发生在 1000ms 之内"，但定时器本身就是每
         * 1000ms 才跑一次，一次点击如果落在两次检查之间比较靠后的位置，等到
         * 下一次 tick() 真正跑到时，idle 完全可能已经涨到 1000ms 上下甚至
         * 略微超过（主循环调度抖动、lv_timer_handler 处理耗时都会往上加），
         * 这一次点击就会被判定为"太旧"而不点亮屏幕，用户得再点一下才能碰到
         * 下一次窗口。这里改成"这一次读到的 idle 比上一次检查时还要小"——
         * 只要在两次 tick() 之间发生过任意一次输入，inactive_time 就会被
         * LVGL 重置变小，与两次检查之间实际间隔了多久、有没有抖动都无关，
         * 是比固定阈值更宽、更不依赖检查周期的判定。 */
        backlight_set(cw_settings_get()->brightness);
        s_dimmed = false;
    }
    s_last_idle = idle;
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    lv_timer_del(s_timer);
    s_timer = NULL; s_scr = NULL;
    if (s_dimmed) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void go_koch(lv_event_t *e)     { (void)e; if (wake_swallow()) return; ui_preview_open(cw_settings_get()->koch_lesson, false); }
/* 呼号抄收 / 模拟 QSO / 电键 都收进了「更多练习」子页面，见 ui_more.c：
   它们是次要场景，五项挤在右列时每项只有 42px，低于 44px 触控下限。 */
static void go_more(lv_event_t *e)     { (void)e; if (wake_swallow()) return; ui_nav_show(ui_more_create); }
/* ui_net_create() 自己会在没配过网时改跳配网页，这里不用判断。 */
static void go_net(lv_event_t *e)      { (void)e; if (wake_swallow()) return; ui_nav_show(ui_net_create); }
static void go_stats(lv_event_t *e)    { (void)e; if (wake_swallow()) return; ui_nav_show(ui_stats_create); }
static void go_ota(lv_event_t *e)      { (void)e; if (wake_swallow()) return; ui_nav_show(ui_ota_create); }
static void go_settings(lv_event_t *e) { (void)e; if (wake_swallow()) return; ui_nav_show(ui_settings_create); }

lv_obj_t *ui_home_create(void)
{
    const cw_settings_t *cfg = cw_settings_get();
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "", NULL);
    s_clock = lv_label_create(bar);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 6, 0);

    /*
     * 顶栏正中：自己的呼号。没设置就整个不建——留一行占位或者写「未设置」，
     * 等于让一个空状态天天挂在主界面上，而这一格本来就是可有可无的信息。
     * 需要设置的地方（联网对练）自己会提示，不必在这里天天喊。
     *
     * 不放进 tick() 里刷：呼号只在设置页改得动，改完必然经过一次
     * ui_nav_show(ui_home_create)，整页重建时自然就是新的了。
     */
    if (cfg->callsign[0]) {
        lv_obj_t *call = lv_label_create(bar);
        lv_label_set_text(call, cfg->callsign);
        lv_obj_align(call, LV_ALIGN_CENTER, 0, 0);
    }

    /*
     * 顶栏右侧从右到左：电量图标、电量百分比、WiFi 图标。
     *
     * 设置的齿轮已经移到下面的菜单里——16px 的图标在电阻屏上既难认也难点，
     * 而按工艺规则低频操作本来就该进菜单、不该占主界面的固定位置。腾出来的
     * 空间正好给这几个状态图标。
     *
     * 图标全部用 LVGL 图元现画：font20 不含 LVGL 符号所在的 0xF000+ 私有区，
     * LV_SYMBOL_* 会是豆腐块（配网页键盘就栽在这上面）。见 ui_icons.h。
     */
    /*
     * 右侧从右到左：电量数字、电池图标、WiFi 图标——**位置按实际显示的项依次
     * 推出来，不能写死偏移**。「电量显示」可以关掉（有人根本没接电池），而原来
     * 三个偏移是 -6 / -30 / -56 三个常数，关掉电量之后 WiFi 还钉在 -56，右边会
     * 空出 50px 一大块，看着像画坏了。
     *
     * 数字在电池图标右边，读起来是"电池 78"；百分号省掉，旁边就是电池图标，
     * 那个符号是冗余的。
     *
     * 图标全部用 LVGL 图元现画：font20 不含 LVGL 符号所在的 0xF000+ 私有区，
     * LV_SYMBOL_* 会是豆腐块（配网页键盘就栽在这上面）。见 ui_icons.h。
     */
    s_bat = NULL;
    s_bat_icon = NULL;
    int x = 6;   /* 距顶栏右边缘的距离，取负后就是 LV_ALIGN_RIGHT_MID 的偏移 */
    if (cfg->battery_show == CW_BATT_NUM) {
        /* 用 Montserrat 12 而不是 UI_FONT_CN：后者行高 26，在 28px 的顶栏里
           和 16px 的图标并排显得头重脚轻。见 ui_common.h 的 UI_FONT_TINY。 */
        s_bat = lv_label_create(bar);
        lv_obj_set_style_text_font(s_bat, UI_FONT_TINY, 0);
        lv_obj_align(s_bat, LV_ALIGN_RIGHT_MID, -x, 0);
        x += 24;
    }
    if (cfg->battery_show != CW_BATT_HIDE) {
        s_bat_icon = ui_icon_battery(bar);
        lv_obj_align(s_bat_icon, LV_ALIGN_RIGHT_MID, -x, 0);
        x += 26;
    }
    /* 三项全开时 x 正好回到原来的 56，默认外观一像素都没变。 */
    s_wifi_icon = ui_icon_wifi(bar);
    lv_obj_align(s_wifi_icon, LV_ALIGN_RIGHT_MID, -x, 0);
    ui_icon_wifi_set(s_wifi_icon, wifi_net_level());

    ui_home_set_battery(battery_percent());

    int top = UI_TOPBAR_H;
    /* 开机检查发现新版本就提示一次。不弹对话框拦路——用户开机多半是想
       练一课，不是来升级的；一行提示够了，去设置里升级。 */
    const ota_check_t *ck = ota_check_result();
    if (ck->checked && ck->available && !s_ota_notice_shown) {
        char m[64];
        snprintf(m, sizeof m, "有新版本 %s，点击更新", ck->latest);
        lv_obj_t *n = ui_notice(s_scr, m);
        /* 提示条直接可点，省掉"自己去设置里翻"这一步。
           28px 不到 44px 的触控下限，用 ext_click_area 补到 52px——向下会
           盖住 Koch 卡片顶部 12px，但那张卡片有 190px 高，损失可忽略，而且
           这条提示只在真有新版本时才出现。 */
        lv_obj_add_flag(n, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(n, 12);
        lv_obj_add_event_cb(n, go_ota, LV_EVENT_CLICKED, NULL);
        s_ota_notice_shown = true;
        top += UI_NOTICE_H;
    }
    if (cw_settings_was_reset() && !s_reset_notice_shown) {
        ui_notice(s_scr, "进度已重置（数据损坏或版本不符）");
        s_reset_notice_shown = true;
        top += UI_NOTICE_H;
    }

    /* 左：Koch 卡片 */
    /*
     * Koch 卡片是**内容井**，不是按钮。
     *
     * 原来整张卡片可点、且画成凸起的。凸起在这套语言里编码「能按」，而现在
     * 可点的只有右下角那个键——卡片再凸着就是在撒谎（和呼号牌当初套容器是
     * 同一类错）。改成凹陷，和对练页的收报井、电键页的解码井同一档：
     * **内容装在井里，控件浮在页面上**。
     *
     * 左边缩进 UI_WELL_INSET、右边和入口列之间也留同样一条纸底，理由见那个
     * 常量的说明：井贴着边缘时，井壁就没有参照物了。
     */
    lv_obj_t *card = lv_obj_create(s_scr);
    ui_plain(card);
    lv_obj_set_style_bg_color(card, UI_SUNK, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    ui_bevel(card, false);
    lv_obj_set_pos(card, UI_WELL_INSET, top);
    lv_obj_set_size(card, 200 - UI_WELL_INSET * 2, 240 - top);

    lv_obj_t *l1 = lv_label_create(card);
    /* 含中文（"第...课"），必须留在屏幕默认的 UI_FONT_CN，不能设成 UI_FONT_MID/UI_FONT_BIG（无中文字形） */
    lv_obj_set_style_text_color(l1, UI_INK, 0);
    /* 主页唯一的标题，用 28px 宋体——这是全工程第一处中文能比正文大一号的
       地方，也正是「中文只有一级字号」这个瓶颈最该先被打破的位置。 */
    lv_obj_set_style_text_font(l1, UI_FONT_TITLE, 0);
    lv_label_set_text_fmt(l1, "KOCH 第 %u 课", cfg->koch_lesson);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);

    /*
     * 字符表限高两行，装不下打省略号。
     *
     * 原来是 LONG_WRAP 只给宽不给高：课次靠后时字符集有四十个字符，184 宽要排
     * 三行、画到 y=118，把固定在 96 的「上次 xx%」压在底下。
     *
     * 文案先报数量再列字符：列不全的时候，「解锁了多少个」比「解锁了哪几个」
     * 更有用——具体哪些字，进练习页的键盘上一目了然。
     */
    char cs[48]; int csn = koch_charset(cfg->koch_lesson, cs);
    lv_obj_t *l2 = lv_label_create(card);
    lv_obj_set_style_text_color(l2, UI_INK2, 0);
    lv_obj_set_size(l2, 184, 52);
    lv_label_set_long_mode(l2, LV_LABEL_LONG_DOT);
    if (cfg->koch_lesson == 1 && cfg->koch_last_pct[1] == 0xFF) lv_label_set_text_fmt(l2, "%s", cs);
    else lv_label_set_text_fmt(l2, "已解锁 %d 字  %s", csn, cs);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t *l3 = lv_label_create(card);
    lv_obj_set_style_text_color(l3, UI_INK2, 0);
    uint8_t last = cfg->koch_last_pct[cfg->koch_lesson];
    if (last == 0xFF) lv_label_set_text(l3, "未开始");
    else lv_label_set_text_fmt(l3, "上次 %u%%", last);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 0, 96);

    /* 唯一的可点元素：一个真按钮，凸起、有按下反馈。
       含中文，必须用 UI_FONT_CN（font28/EN_BIG 无中文字形）。 */
    lv_obj_t *l4 = ui_cell(card, last == 0xFF ? "开始练习 >" : "继续练习 >",
                           UI_FONT_CN, go_koch, NULL);
    lv_obj_set_size(l4, 124, 36);
    lv_obj_set_style_text_color(l4, UI_ACCENT, 0);
    /* 全屏最重要的那个操作，此前只是一行铜色的字——没有面、没有边，读不出
       「这是个可以按的东西」。给它抬高面和浮雕：整张卡片本来就可点，这个键
       是把「可点」这件事说出来。 */
    lv_obj_align(l4, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* 右：四个入口 */
    /* 四项各 (240-28)/4 = 53px，达到 44px 触控下限；原来五项只有 42px。
       设置排在最末，符合"设置永远在最后"的惯例。 */
    const char *names[4] = { "更多练习 >", "联网对练 >", "训练统计 >", "设置 >" };
    lv_event_cb_t cbs[4] = { go_more, go_net, go_stats, go_settings };
    int h = (240 - top) / 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = ui_cell(s_scr, names[i], UI_FONT_CN, cbs[i], NULL);
        lv_obj_set_pos(c, 200, top + i * h);
        lv_obj_set_size(c, 120, h);
        /* 不再手写分隔线：ui_cell 现在自带凸起浮雕，再画一条底边会把浮雕的
           上左亮线覆盖掉（一个对象一个状态只有一种 border_color）。 */
    }

    s_dimmed = false;
    s_timer = lv_timer_create(tick, 1000, NULL);
    tick(s_timer);
    return s_scr;
}

void ui_home_set_battery(uint8_t percent)
{
    /* 两个控件各自判空：「电量显示」为「图标」时只有图标、为「隐藏」时两个都
       没有，而 battery 任务照常每 10 秒回调一次。 */
    if (!s_scr) return;
    if (s_bat_icon) ui_icon_battery_set(s_bat_icon, percent);
    if (s_bat) lv_label_set_text_fmt(s_bat, "%u", percent);
}
