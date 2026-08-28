#ifndef UI_CALLSIGN_H
#define UI_CALLSIGN_H
#include "ui_common.h"

/*
 * 呼号编辑页。两个入口共用一份：设置页的「我的呼号」，和联网对练在还没有
 * 呼号时显示的那个按钮。
 *
 * 返回目标可设，用法同 ui_wifi_set_return()：先设，再 ui_nav_show()。
 * 「确定」和「返回」去同一个地方——对练页不会因为没呼号就把人挡回来（它只是
 * 在收报区显示一个按钮），所以不存在返回被再次拦截、用户出不去的问题。
 *
 * 这一页曾经做成盖在当前页上的弹窗，是个错误，值得记下来：对练页的准入拦截
 * 在 ui_net_create() 里触发，而那是 ui_nav_show() 的 builder——
 * builder() -> lv_scr_load() -> del_async(old) 的顺序意味着此刻 lv_scr_act()
 * 还是**正要被删掉的上一屏**。弹窗挂上去就随它一起消失（表现为「进对练页
 * 什么都没弹、也不联网」），留下的野指针下一次去删就把堆搞坏，LVGL 在
 * lv_obj_set_style_bg_color 里空转到任务看门狗复位。页面没有这个问题：
 * 它自己就是一屏，不寄生在别人身上。
 *
 * 呼号少于 3 个字符不给保存，停在本页并给出提示，不会静默丢弃。
 */
lv_obj_t *ui_callsign_create(void);
void      ui_callsign_set_return(ui_screen_builder_t back_to);

#endif
