/*
 * 素材样机（style probe）——不是产品页面，是一把尺子。
 *
 * 拟物风格的成败在这块屏上由三件事决定，而这三件事**在桌面模拟器里全都测不
 * 出来**：SDL 模拟器跑在 32 位色的好显示器上，既没有 RGB565 的色阶损失，也
 * 没有电阻触摸那层雾面膜，更没有这块 ST7789 的 gamma 和可视角。所以在投入
 * 做素材之前，先把这几个问题烧到真板子上问清楚：
 *
 *   1. 实时 2-stop 渐变的色带到底有多明显？离线抖动能救回多少？（页 2）
 *   2. 1px 的高光/暗线浮雕，隔着雾面膜还看得见吗？（页 3）
 *   3. 纸纹噪点到多大强度会从「质感」变成「脏」？压着字还读得动吗？（页 4）
 *   4. 定下的九个色 token 上屏后还剩多少对比度？（页 1、页 5）
 *   5. LVGL 实时阴影（LV_SHADOW_CACHE_SIZE=0，每帧重算）真实代价多少？（页 6）
 *
 * 第 5 项是唯一一个能给出硬数字的：lv_refr_now() 是同步重绘，用 esp_timer
 * 掐它两头就是这一帧的真实耗时，不用猜。
 *
 * 全页文案刻意只用 ASCII 和 font20_cwtrn 字符集里已有的中文（见
 * tools/check_charset.py）——样机不该为了自己去动字库。
 */
#include "ui_probe.h"
#include <stdio.h>
#include "esp_timer.h"
#include "ui_common.h"
#include "ui_home.h"
#include "probe_assets.h"

/* 待验证的调色板，来自 docs/design/cw-trainer-visual-direction.html。
   刻意不写进 ui_common.h：样机的职责是「质疑这套值」，不是「使用这套值」，
   等真机校色定稿后才轮到它进共享头文件。 */
#define P_PAPER   lv_color_hex(0xEFE6D4)
#define P_RAISE   lv_color_hex(0xF7F1E4)
#define P_SUNK    lv_color_hex(0xE4D9C3)
#define P_INK     lv_color_hex(0x2A2118)
#define P_INK2    lv_color_hex(0x6B5D4A)
#define P_INK3    lv_color_hex(0x8F7F67)
#define P_RULE    lv_color_hex(0xD3C6AC)
#define P_COPPER  lv_color_hex(0x945418)
#define P_DANGER  lv_color_hex(0x9C2B1B)
/* 浮雕用的两条线：比纸底亮一档、暗一档 */
#define P_HILITE  lv_color_hex(0xFFFBF0)
#define P_SHADE   lv_color_hex(0xB5A88C)

#define PAGE_CNT 6

static int      s_page;
static lv_obj_t *s_scr;
static lv_obj_t *s_shadow_box[3];
static lv_obj_t *s_shadow_out;

static lv_obj_t *build(void);

/* ── 小工具 ───────────────────────────────────────────────── */

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, int x, int y, const char *s,
                      const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/*
 * 浮雕：两条 1px 线，亮的在受光侧、暗的在背光侧。凸起是「上左亮、下右暗」，
 * 凹陷反过来。
 *
 * 要两个对象才做得出来：LVGL 一份样式只能有一个 border_color，而浮雕的本质
 * 就是同一个矩形的四条边分成两种颜色。所以外层画一组边，内层叠一个同尺寸、
 * 背景透明的对象画另一组边。这比用 shadow 便宜到没法比——两条线而已。
 */
static lv_obj_t *bevel(lv_obj_t *parent, int x, int y, int w, int h,
                       lv_color_t face, bool raised, int px)
{
    lv_obj_t *o = box(parent, x, y, w, h, face);
    lv_obj_set_style_border_width(o, px, 0);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(o, raised ? P_HILITE : P_SHADE, 0);

    lv_obj_t *in = lv_obj_create(o);
    lv_obj_set_pos(in, -px, -px);          /* 抵消父对象的边框内缩，贴回四角 */
    lv_obj_set_size(in, w, h);
    lv_obj_set_style_radius(in, 0, 0);
    lv_obj_set_style_pad_all(in, 0, 0);
    lv_obj_set_style_bg_opa(in, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(in, 0, 0);
    lv_obj_set_style_border_width(in, px, 0);
    lv_obj_set_style_border_side(in, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(in, raised ? P_SHADE : P_HILITE, 0);
    lv_obj_clear_flag(in, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void img_at(lv_obj_t *parent, int x, int y, const lv_img_dsc_t *src)
{
    lv_obj_t *i = lv_img_create(parent);
    lv_img_set_src(i, src);
    lv_obj_set_pos(i, x, y);
}

/* ── 页 1：色板 ───────────────────────────────────────────── */

static void page_palette(lv_obj_t *p)
{
    static const struct { const char *n; uint32_t v; } sw[] = {
        { "PAPER",  0xEFE6D4 }, { "RAISE",  0xF7F1E4 }, { "SUNK",   0xE4D9C3 },
        { "INK",    0x2A2118 }, { "INK2",   0x6B5D4A }, { "INK3",   0x8F7F67 },
        { "RULE",   0xD3C6AC }, { "COPPER", 0x945418 }, { "DANGER", 0x9C2B1B },
    };
    /* 3x3，每格 104x56。深色块上的字翻白，否则自己看不见自己。 */
    for (int i = 0; i < 9; i++) {
        int x = (i % 3) * 106, y = (i / 3) * 58;
        lv_color_t c = lv_color_hex(sw[i].v);
        lv_obj_t *b = box(p, x, y, 104, 56, c);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, P_INK, 0);
        bool dark = i >= 3 && i <= 5;
        dark = dark || i == 7 || i == 8;
        char buf[32];
        snprintf(buf, sizeof buf, "%s", sw[i].n);
        text(b, 4, 4, buf, UI_FONT_TINY, dark ? P_PAPER : P_INK);
        snprintf(buf, sizeof buf, "%06X", (unsigned)sw[i].v);
        text(b, 4, 22, buf, UI_FONT_TINY, dark ? P_PAPER : P_INK2);
    }
}

/* ── 页 2：渐变，实时 vs 烘焙抖动 ─────────────────────────── */

static void page_gradient(lv_obj_t *p)
{
    /* 第一行：LVGL 实时 2-stop（LV_GRADIENT_MAX_STOPS=2、DITHER 关）。
       这就是不做任何处理时屏上会出现的样子。 */
    lv_obj_t *g = box(p, 0, 14, 148, 36, P_PAPER);
    lv_obj_set_style_bg_grad_color(g, lv_color_hex(0xC9BC9E), 0);
    lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
    text(p, 0, 0, "LVGL LIVE GRADIENT", UI_FONT_TINY, P_INK2);

    lv_obj_t *g2 = box(p, 160, 14, 148, 36, lv_color_hex(0xB8763A));
    lv_obj_set_style_bg_grad_color(g2, lv_color_hex(0x6E3D0E), 0);
    lv_obj_set_style_bg_grad_dir(g2, LV_GRAD_DIR_VER, 0);
    text(p, 160, 0, "SAME, COPPER RANGE", UI_FONT_TINY, P_INK2);

    /* 第二行：同样两段范围，离线量化但**不抖动**——用来确认上面那条的色带
       确实来自量化，而不是别的什么。两条应当看起来一模一样。 */
    text(p, 0, 54, "BAKED, NO DITHER", UI_FONT_TINY, P_INK2);
    img_at(p, 0, 68, &probe_grad_paper_flat);
    img_at(p, 160, 68, &probe_grad_copper_flat);

    /* 第三行：离线 Floyd-Steinberg 抖动后烘焙。色带应当被噪声打散。
       这一行和第一行的差别，就是「烘焙贴图」这条路线值不值得走。 */
    text(p, 0, 108, "BAKED + DITHER", UI_FONT_TINY, P_COPPER);
    img_at(p, 0, 122, &probe_grad_paper_dith);
    img_at(p, 160, 122, &probe_grad_copper_dith);

    text(p, 0, 162, "Q: banding visible? does dither fix it?", UI_FONT_TINY, P_INK3);
}

/* ── 页 3：浮雕 ───────────────────────────────────────────── */

static void page_bevel(lv_obj_t *p)
{
    text(p, 0, 0, "1px RAISED / SUNK, ON 3 SURFACES", UI_FONT_TINY, P_INK2);

    lv_color_t faces[3] = { P_PAPER, P_RAISE, P_SUNK };
    const char *fn[3] = { "PAPER", "RAISE", "SUNK" };
    for (int i = 0; i < 3; i++) {
        int x = i * 106;
        lv_obj_t *a = bevel(p, x, 16, 100, 40, faces[i], true, 1);
        text(a, 6, 8, "RAISED", UI_FONT_TINY, P_INK);
        lv_obj_t *b = bevel(p, x, 62, 100, 40, faces[i], false, 1);
        text(b, 6, 8, "SUNK", UI_FONT_TINY, P_INK);
        text(p, x, 104, fn[i], UI_FONT_TINY, P_INK3);
    }

    /* 2px 版本：如果 1px 在雾面膜下糊掉了，这里是退路。 */
    text(p, 0, 122, "2px VERSION", UI_FONT_TINY, P_INK2);
    lv_obj_t *c = bevel(p, 0, 136, 100, 40, P_RAISE, true, 2);
    text(c, 6, 6, "RAISED 2px", UI_FONT_TINY, P_INK);
    lv_obj_t *d = bevel(p, 106, 136, 100, 40, P_RAISE, false, 2);
    text(d, 6, 6, "SUNK 2px", UI_FONT_TINY, P_INK);

    /* 一个真按钮的样子：浮雕 + 铜色主操作文字 */
    lv_obj_t *e = bevel(p, 212, 136, 96, 40, P_RAISE, true, 1);
    text(e, 8, 4, "继续练习", UI_FONT_CN, P_COPPER);

    text(p, 0, 182, "Q: is 1px readable through the touch film?", UI_FONT_TINY, P_INK3);
}

/* ── 页 4：纸纹 ───────────────────────────────────────────── */

static void page_texture(lv_obj_t *p)
{
    const lv_img_dsc_t *n[4] = { &probe_noise0, &probe_noise1, &probe_noise2, &probe_noise3 };
    const char *lbl[4] = { "SIGMA 1", "SIGMA 2", "SIGMA 4", "SIGMA 7" };

    text(p, 0, 0, "PAPER GRAIN, 4 STRENGTHS (tiled 32x32)", UI_FONT_TINY, P_INK2);
    for (int i = 0; i < 4; i++) {
        int y = 14 + i * 42;
        /* bg_img_tiled 是 v8 里唯一的平铺方式：一张 32x32 的小图铺满整条，
           2KB 的素材覆盖任意面积。 */
        lv_obj_t *strip = box(p, 0, y, 320, 38, P_PAPER);
        lv_obj_set_style_bg_img_src(strip, n[i], 0);
        lv_obj_set_style_bg_img_tiled(strip, true, 0);
        /* 压上真实字号的正文，看噪点会不会啃掉可读性 */
        text(strip, 6, 5, "继续练习 第 7 课", UI_FONT_CN, P_INK);
        text(strip, 232, 12, lbl[i], UI_FONT_TINY, P_INK2);
    }
    text(p, 0, 186, "Q: where does grain turn into dirt?", UI_FONT_TINY, P_INK3);
}

/* ── 页 5：文字对比度 ─────────────────────────────────────── */

static void page_text(lv_obj_t *p)
{
    struct { const char *n; lv_color_t c; const char *ratio; } rows[4] = {
        { "INK",    P_INK,    "12.98:1" },
        { "INK2",   P_INK2,   " 5.19:1" },
        { "INK3",   P_INK3,   " 3.26:1" },
        { "COPPER", P_COPPER, " 4.78:1" },
    };
    text(p, 0, 0, "TEXT ON PAPER - computed contrast vs measured eye", UI_FONT_TINY, P_INK2);
    for (int i = 0; i < 4; i++) {
        int y = 16 + i * 48;
        text(p, 0, y, rows[i].n, UI_FONT_TINY, P_INK2);
        text(p, 0, y + 14, "继续练习 第 7 课", UI_FONT_CN, rows[i].c);
        text(p, 196, y, rows[i].ratio, UI_FONT_TINY, P_INK3);
        text(p, 196, y + 16, "AZ 0123", UI_FONT_TINY, rows[i].c);
    }
    text(p, 0, 212, "Q: INK3 still legible? COPPER still legible?", UI_FONT_TINY, P_INK3);
}

/* ── 页 6：实时阴影的真实代价 ─────────────────────────────── */

static void measure(lv_event_t *e)
{
    (void)e;
    /* lv_refr_now() 是同步重绘：掐两头就是这一帧的真实耗时，不用估。
       每档量三次取最后一次，避开第一次的冷缓存。 */
    static const int widths[3] = { 0, 8, 16 };
    int64_t us[3] = { 0, 0, 0 };
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 3; i++) {
            for (int b = 0; b < 3; b++)
                lv_obj_set_style_shadow_width(s_shadow_box[b], widths[w], 0);
            lv_obj_invalidate(s_scr);
            int64_t t0 = esp_timer_get_time();
            lv_refr_now(NULL);
            us[w] = esp_timer_get_time() - t0;
        }
    }
    /* 恢复成三档不同宽度，好用肉眼看质量 */
    for (int b = 0; b < 3; b++)
        lv_obj_set_style_shadow_width(s_shadow_box[b], b == 0 ? 4 : (b == 1 ? 8 : 16), 0);

    char buf[96];
    snprintf(buf, sizeof buf, "none %lldus / w8 %lldus / w16 %lldus",
             (long long)us[0], (long long)us[1], (long long)us[2]);
    lv_label_set_text(s_shadow_out, buf);
}

static void page_shadow(lv_obj_t *p)
{
    text(p, 0, 0, "LVGL REAL SHADOW (SHADOW_CACHE_SIZE=0)", UI_FONT_TINY, P_INK2);
    const int w[3] = { 4, 8, 16 };
    const char *n[3] = { "w4", "w8", "w16" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = box(p, 20 + i * 100, 30, 64, 64, P_RAISE);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, P_RULE, 0);
        lv_obj_set_style_shadow_width(b, w[i], 0);
        lv_obj_set_style_shadow_ofs_y(b, 2, 0);
        lv_obj_set_style_shadow_color(b, P_INK, 0);
        lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
        text(p, 20 + i * 100, 98, n[i], UI_FONT_TINY, P_INK3);
        s_shadow_box[i] = b;
    }
    lv_obj_t *btn = ui_button(p, "MEASURE", measure, NULL);
    lv_obj_set_size(btn, 120, 36);
    lv_obj_set_pos(btn, 0, 120);

    s_shadow_out = text(p, 0, 164, "tap MEASURE", UI_FONT_TINY, P_INK);
    text(p, 0, 186, "full-screen lv_refr_now(), microseconds", UI_FONT_TINY, P_INK3);
}

/* ── 导航 ─────────────────────────────────────────────────── */

static void back(lv_event_t *e) { (void)e; s_page = 0; ui_nav_show(ui_home_create); }
static void next(lv_event_t *e)
{
    (void)e;
    s_page = (s_page + 1) % PAGE_CNT;
    ui_nav_show(build);
}

static lv_obj_t *build(void)
{
    static const char *titles[PAGE_CNT] = {
        "PALETTE", "GRADIENT", "BEVEL", "GRAIN", "TEXT", "SHADOW",
    };
    s_scr = ui_screen_create();
    /* 样机整屏就是纸底——所有取样都必须发生在最终底色上，白底上看着还行的
       浮雕线到纸底上可能就没了。 */
    lv_obj_set_style_bg_color(s_scr, P_PAPER, 0);

    lv_obj_t *bar = ui_topbar(s_scr, titles[s_page], back);
    lv_obj_set_style_bg_color(bar, P_PAPER, 0);
    char c[16];
    snprintf(c, sizeof c, "%d/%d", s_page + 1, PAGE_CNT);
    lv_obj_t *cnt = lv_label_create(bar);
    lv_label_set_text(cnt, c);
    lv_obj_set_style_text_font(cnt, UI_FONT_TINY, 0);
    lv_obj_align(cnt, LV_ALIGN_RIGHT_MID, -56, 0);

    lv_obj_t *nb = lv_label_create(bar);
    lv_label_set_text(nb, "NEXT >");
    lv_obj_set_style_text_font(nb, UI_FONT_TINY, 0);
    lv_obj_align(nb, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(nb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(nb, 16);   /* 电阻屏，17px 行高必须扩点击区 */
    lv_obj_add_event_cb(nb, next, LV_EVENT_CLICKED, NULL);

    /* 各页内容画在顶栏之下的容器里，坐标一律从 0 起算 */
    lv_obj_t *p = box(s_scr, 0, UI_TOPBAR_H, 320, 240 - UI_TOPBAR_H, P_PAPER);
    switch (s_page) {
        case 0: page_palette(p);  break;
        case 1: page_gradient(p); break;
        case 2: page_bevel(p);    break;
        case 3: page_texture(p);  break;
        case 4: page_text(p);     break;
        default: page_shadow(p);  break;
    }
    return s_scr;
}

lv_obj_t *ui_probe_create(void) { return build(); }
