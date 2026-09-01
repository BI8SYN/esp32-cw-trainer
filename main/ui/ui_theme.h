#ifndef UI_THEME_H
#define UI_THEME_H
#include "lvgl.h"

/*
 * 设计 token 的唯一出处。视觉方案见 docs/design/cw-trainer-visual-direction.html。
 *
 * ── 为什么现在的值还是黑白灰 ──
 *
 * 这个文件是**先立名字，后换值**。方案定的是纸底墨字铜 accent，但换色是下一步；
 * 这一步只把名字从「长什么样」（WHITE / BLACK / GRAY）改成「是什么角色」
 * （PAPER / INK / SUNK），值一个不动。这样改完屏幕必须逐像素不变——而这是能被
 * screenshot.c 的指纹巡检直接验证的，不用靠肉眼。
 *
 * 反过来说，如果现在就连名带值一起换，那就同时改了两件事，出了问题分不清是
 * 「改名改错了」还是「新颜色不对」。
 *
 * ── 为什么按角色分，而不是按颜色分 ──
 *
 * 今天 UI_GRAY_DOWN(0x999999) 同时干两件事：按钮按下的背景、禁用态的字和边框。
 * 在纸面方案里这两件事是不同的 token（凹陷面 sunk vs 占位色 ink3）。现在就把它
 * 拆成 UI_PRESSED 和 UI_DISABLED 两个名字、值都仍是 0x999999，等换色时它们各自
 * 走各自的新值即可，不必再回头考古每个调用点当初是哪个意思。
 *
 * 同理，多个 token 现在指向同一个值（比如 UI_RULE 和 UI_INK 都是黑）。这不是
 * 冗余，是**把「今天恰好相同」和「本来就该相同」区分开**。
 */

/* ── 面：从下到上 ───────────────────────────────────────── */
#define UI_PAPER     lv_color_hex(0xFFFFFF)  /* 页面底                     → 纸底 */
#define UI_RAISE     lv_color_hex(0xFFFFFF)  /* 抬高面：按钮/键帽/输入格底 → 抬高面 */
#define UI_SUNK      lv_color_hex(0xEEEEEE)  /* 凹陷面：提示条底、卡片按下 → 凹陷面 */
#define UI_PRESSED   lv_color_hex(0x999999)  /* 按钮按下的底               → 凹陷面 */

/* ── 墨 ─────────────────────────────────────────────────── */
#define UI_INK       lv_color_hex(0x000000)  /* 主文、主结构线             → ink */
#define UI_INK2      lv_color_hex(0x000000)  /* 次要文：标签、单位         → ink2 */
#define UI_DISABLED  lv_color_hex(0x999999)  /* 禁用的字与边框             → ink3 */
#define UI_RULE      lv_color_hex(0x000000)  /* 组内次级分割线             → rule */

/* ── accent：今天还没有，先退化成墨色 ─────────────────────
   现在用它 = 现在不变样，换色时自动变成铜色。比等到换色那天再去
   一处处找「哪些黑其实是主操作」要可靠得多。 */
#define UI_ACCENT    lv_color_hex(0x000000)  /* 主操作、进度、当前项       → copper */
#define UI_DANGER    lv_color_hex(0x000000)  /* 危险操作、答错            → danger */

/* ── 三个今天都是 0x999999、明天各走各路的角色 ────────────
   现在它们同值，纯属巧合；把巧合和「本来就该相同」分开，是这一步的全部意义。 */
#define UI_SELECTED  lv_color_hex(0x999999)  /* 当前选中的 tab / 选项      → 待定：凹陷面或 accent */
#define UI_FILL      lv_color_hex(0x999999)  /* 数据填充：进度条、柱状图   → copper */
#define UI_GRID      lv_color_hex(0x999999)  /* 图表参考线、网格线         → rule */

/* ── 浮雕的两条 1px 线。拟物层用，今天全是白/灰，等于不可见 ── */
#define UI_HILITE    lv_color_hex(0xFFFFFF)  /* 受光边 */
#define UI_SHADE     lv_color_hex(0xEEEEEE)  /* 背光边 */

/* ── 字体 ───────────────────────────────────────────────── */
LV_FONT_DECLARE(font20);
LV_FONT_DECLARE(font28);
LV_FONT_DECLARE(EN_BIG);
#define UI_FONT_CN   (&font20)   /* 正文，行高 26，唯一含中文的一级 */
#define UI_FONT_MID  (&font28)   /* 中号，行高 37，仅 ASCII */
#define UI_FONT_BIG  (&EN_BIG)   /* 大号，行高 82，仅 ASCII */
/*
 * 顶栏状态用的小号数字（行高 17）。项目自带的三个字体行高分别是 26/37/82，
 * 在顶栏里和 16px 的图标并排都显得头重脚轻。
 *
 * 用 14 而不是更小的 12：components/lvgl/lv_conf.h 里虽然把 8/10/12/16 都
 * 置 1 了，但实际编译进来的只有 14（12 会报 undeclared），说明那份 lv_conf
 * 没有生效、走的是 LVGL 默认配置。14 是唯一确定可用的。
 *
 * 只有 ASCII，不能拿去显示中文。
 */
#define UI_FONT_TINY (&lv_font_montserrat_14)

/* ── 尺寸 ───────────────────────────────────────────────── */
#define UI_TOPBAR_H  28
/*
 * 提示条高度。必须 >= UI_FONT_CN 的行高 26——原来写的是 22，比行高还矮，
 * 文字上下各被裁掉 2px。这个坑在本项目里反复出现（配网页输入框、对练页
 * 状态带、电键页第二行），共享 token 里定一次，所有调用方都受益。
 *
 * 调用方在 ui_notice() 之后把 top 加上这个值，不要再写死 22。
 */
#define UI_NOTICE_H  28

#endif
