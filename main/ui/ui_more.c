#include "ui_more.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_keyer.h"
#include "ui_practice.h"
#include "trainer.h"

/*
 * 「更多练习」子页面：呼号抄收 / 模拟 QSO / 电键。
 *
 * 为什么把这三项从主页收进来：主页的主任务只有一个——继续 Koch 那一课
 * （见 2026-08-25 设计定稿 §1 的核心场景）。这三项都是次要场景，五个等宽
 * 等高的条目和左边的大卡片抢注意力，收成一个入口之后左卡片的主体地位才
 * 立得住。
 *
 * 顺带解决一个硬指标：主页右列原来 5 项，每项 (240-28)/5 = 42px，**低于
 * 44px 的触控下限**；有"进度已重置"提示条时更是只有 38px，电阻屏上漏点很
 * 明显。收成 4 项后每项 53px，这一页三项各 70px，两边都达标了。
 *
 * 为什么是子页面而不是弹出菜单：弹出菜单要自己发明一套关闭方式，tap-outside
 * 在电阻屏上误触风险高，加"关闭"按钮又占掉一行。而项目里每一次导航都是
 * ui_nav_show + 顶栏返回箭头，那个箭头已经做过 ext_click_area 扩大、用户在
 * 每一页都见过。别为一个已有导航模式能解决的问题发明新模式。
 */

static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

static void go_call(lv_event_t *e)  { (void)e; ui_practice_open(TRAINER_MODE_CALLSIGN); }
static void go_qso(lv_event_t *e)   { (void)e; ui_practice_open(TRAINER_MODE_QSO); }
static void go_keyer(lv_event_t *e) { (void)e; ui_nav_show(ui_keyer_create); }

lv_obj_t *ui_more_create(void)
{
    lv_obj_t *scr = ui_screen_create();
    ui_topbar(scr, "更多练习", back);

    /* 三行各 70px，远超 44px 触控下限——这三项在主页时只有 42px。
       这里不需要 wake_swallow()：息屏变暗的判断在主页，能走到本页说明
       屏幕已经是亮的。 */
    const char *names[3] = { "呼号抄收", "模拟 QSO", "电键" };
    lv_event_cb_t cbs[3] = { go_call, go_qso, go_keyer };
    int h = (240 - UI_TOPBAR_H) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = ui_cell(scr, names[i], UI_FONT_CN, cbs[i], NULL);
        lv_obj_set_pos(c, 0, UI_TOPBAR_H + i * h);
        lv_obj_set_size(c, 320, h);
        lv_obj_set_style_border_side(c, i < 2 ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_NONE, 0);
    }
    return scr;
}
