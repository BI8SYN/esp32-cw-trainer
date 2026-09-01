#ifndef SCREENSHOT_H
#define SCREENSHOT_H

/* 画面指纹工具。只在 -DCW_SCREENSHOT=ON 的构建里编进来。
 *
 * 装上钩子（必须在 lv_port_disp_init() 之后，此时 flush_cb 已经注册）。 */
void screenshot_install(void);

/* 把每个无参页面建一遍、强制整屏重绘、对真正刷向屏幕的像素算指纹，
 * 每页一行打到串口。跑两遍：两遍一致的页面才是可比对的。 */
void screenshot_tour(void);

#endif
