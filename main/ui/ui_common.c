#include "ui_common.h"
#include "ui_assets.h"

/* 默认主题除了 grow（已在 make_btn/home 卡片清零 transform_width/height）以外，还
   无条件给每个 lv_btn_class 挂了两份 transition：
   - transition_normal，挂在 selector = LV_STATE_PRESSED，负责按下瞬间的渐变；
   - transition_delayed，挂在 selector = 0（默认态，任何方向的迁移都会扫到它），
     负责松开后延迟一小段再渐变回默认态。
   两份都覆盖 BG_COLOR/BG_OPA/COLOR_FILTER_*（按下态的变暗滤镜）等一串属性，只要
   这些属性在两个状态间取值不同（我们的按下态背景色就是不同的），就会渐变而不是
   瞬间切换——这仍然是 phase 1 禁止的动画。
   `_lv_obj_style_create_transition` 是按“本对象样式数组里第一个把这个属性列进
   transition 描述符的条目”来决定用谁的时长（本地样式永远排在主题样式前面，
   见 lv_obj_add_style 的插入逻辑），所以只要在同样的两个 selector（0 和
   LV_STATE_PRESSED）上，用本地样式挂一份覆盖同一组属性、但 time=0 的描述符，
   就能连着按下和松开两个方向一起顶掉主题那两份、变成瞬时切换（不是删掉整个
   transition 属性——设成 NULL 会在 lv_obj_set_state 内部被当成非空描述符解引用，
   直接崩溃；0 时长的合法描述符则被 lv_map 的边界分支直接返回终值，没有除零，
   已用设备实测确认过）。 */
static const lv_style_prop_t s_no_fade_props[] = {
    LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR,
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
    LV_STYLE_TRANSLATE_Y, LV_STYLE_TRANSLATE_X,
    LV_STYLE_TRANSFORM_ZOOM, LV_STYLE_TRANSFORM_ANGLE,
    LV_STYLE_COLOR_FILTER_OPA, LV_STYLE_COLOR_FILTER_DSC,
    0
};

static const lv_style_transition_dsc_t *no_fade_transition(void)
{
    static lv_style_transition_dsc_t dsc;
    static bool inited;
    if (!inited) {
        lv_style_transition_dsc_init(&dsc, s_no_fade_props, lv_anim_path_linear, 0, 0, NULL);
        inited = true;
    }
    return &dsc;
}

void ui_nav_show(ui_screen_builder_t builder)
{
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *scr = builder();
    lv_scr_load(scr);
    /* 通常在旧屏幕某个子对象的事件回调里被调用，必须异步删除 */
    if (old && old != scr) lv_obj_del_async(old);
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_PAPER, 0);
    /* 纸纤维。32x32 平铺，2KB 素材铺满整屏——这是这块屏唯一能可靠表现的
       「材质」：连续色调做不出来（有色带），噪点反而正合适。 */
    lv_obj_set_style_bg_img_src(scr, &ui_grain_paper, 0);
    lv_obj_set_style_bg_img_tiled(scr, true, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(scr, UI_INK, 0);
    lv_obj_set_style_text_font(scr, UI_FONT_CN, 0);
    return scr;
}

void ui_plain(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_rule(lv_obj_t *o, lv_border_side_t side, lv_color_t color)
{
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_side(o, side, 0);
    lv_obj_set_style_border_color(o, color, 0);
}

/*
 * 浮雕要四条边分两种颜色，而 LVGL 一个对象在一个状态下只能有一个 border_color。
 * 所以拆成两层：对象自己画上左，再叠一个背景透明的子对象画下右。
 *
 * 一个元素多一个对象——不是四个。最坏情况是练习页 41 个键，41 个对象量级完全
 * 吃得下（这块板没有 PSRAM，可用堆约 100KB，必须掐着算）。
 *
 * 子对象的尺寸这样对齐：父对象 border_width=1 会让内容区四周各缩 1px，所以
 * LV_PCT(100) 的子对象是 (w-2, h-2) 放在 (1,1)；再用 transform_width/height
 * 各向外扩 1px，绘制范围正好回到 (0,0)-(w-1,h-1)，与父对象的盒子严丝合缝。
 * 直接写死尺寸不行——建的时候布局还没跑，宽高还取不到。
 */
/*
 * 廉价版浮雕：只画背光的那两条边。
 *
 * 为什么需要两个版本：ui_bevel() 的子对象用 LV_PCT(100) 贴合父对象，这要求
 * 父对象有确定的尺寸。而界面里有一批**看着像按钮、其实是加了背景色的 label**
 * （首页的「继续练习 >」、顶栏返回箭头、对练页的呼号 badge），它们是
 * LV_SIZE_CONTENT 自适应的，子对象贴不准。
 *
 * 少一条受光边，立体感弱一点，但「下右有一条暗边」本身就是光从左上来时该有的
 * 样子，读作凸起没有问题。凹陷则反过来画上左。
 */
void ui_bevel_edge(lv_obj_t *o, bool raised)
{
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_side(o, raised ? (LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT)
                                           : (LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), 0);
    lv_obj_set_style_border_color(o, UI_SHADE, 0);
}

/*
 * 按下时把浮雕翻过来：凸起变凹陷。
 *
 * 当初写死「按下不翻转、只塌背景色」，理由是 LVGL 的状态不向子对象传播——
 * 那个理由站不住，挂个事件回调就行。而「按下去要陷下去」正是压印风格里最该
 * 有的一下反馈，不该省。
 *
 * 基础朝向存在子对象的 user_data 里：一个浮雕的两条边分属父子两个对象，
 * 回调里得知道「原本是凸还是凹」才能算出翻转后的样子。
 */
static void bevel_state_cb(lv_event_t *e)
{
    lv_obj_t *o  = lv_event_get_target(e);
    lv_obj_t *in = lv_event_get_user_data(e);
    bool raised  = (bool)(uintptr_t)lv_obj_get_user_data(in);
    bool down    = lv_event_get_code(e) == LV_EVENT_PRESSED;
    bool up      = raised != down;      /* 按下就反过来 */
    lv_obj_set_style_border_color(o,  up ? UI_HILITE : UI_SHADE, 0);
    lv_obj_set_style_border_color(in, up ? UI_SHADE  : UI_HILITE, 0);
}

/*
 * 浮雕要四条边分两种颜色，而 LVGL 一个对象在一个状态下只能有一个 border_color。
 * 所以拆成两层：对象自己画上左，再叠一个背景透明的子对象画下右。
 *
 * 一个元素多一个对象——不是四个。最坏是练习页 41 个键，这个量级完全吃得下
 * （这块板没有 PSRAM，可用堆约 100KB，必须掐着算）。
 *
 * **必须在设好 padding 之后调用。** 子对象用 LV_PCT(100)，而 LVGL 的百分比是
 * 相对**内容区**的：父对象有 pad 时内容区会缩进去，子对象跟着缩，浮雕线就画在
 * 距离边缘 pad 那么远的地方。首页那张 pad_all(8) 的大卡片就是这么错的——按下
 * 变色时那个内缩的框一下就暴露了。这里按调用时的实际 pad 反推 transform，
 * 所以顺序不能反。要求 padding 左右对称、上下对称（transform 是对称扩张）。
 */
void ui_bevel(lv_obj_t *o, bool raised)
{
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(o, raised ? UI_HILITE : UI_SHADE, 0);

    lv_obj_t *in = lv_obj_create(o);
    lv_obj_remove_style_all(in);
    lv_obj_set_size(in, LV_PCT(100), LV_PCT(100));
    lv_obj_align(in, LV_ALIGN_TOP_LEFT, 0, 0);
    /* 内容区比盒子四周各小「边框 1px + pad」，往外扩这么多才贴回盒子边缘 */
    lv_obj_set_style_transform_width(in, 1 + lv_obj_get_style_pad_left(o, LV_PART_MAIN), 0);
    lv_obj_set_style_transform_height(in, 1 + lv_obj_get_style_pad_top(o, LV_PART_MAIN), 0);
    lv_obj_set_style_border_width(in, 1, 0);
    lv_obj_set_style_border_side(in, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(in, raised ? UI_SHADE : UI_HILITE, 0);
    /* lv_obj_create 默认带 CLICKABLE，不清掉会把父按钮的点击吃掉 */
    lv_obj_clear_flag(in, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(in, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(in, (void *)(uintptr_t)raised);

    lv_obj_add_event_cb(o, bevel_state_cb, LV_EVENT_PRESSED, in);
    lv_obj_add_event_cb(o, bevel_state_cb, LV_EVENT_RELEASED, in);
    /* 手指划出对象外时 LVGL 只发 PRESS_LOST、收不到 RELEASED——不接这个事件，
       浮雕会永远卡在按下的样子。 */
    lv_obj_add_event_cb(o, bevel_state_cb, LV_EVENT_PRESS_LOST, in);
}

lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    ui_plain(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_PAPER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);  /* 透出页面底纹，别在顶栏切一刀 */
    /* 顶栏不再画整条黑线：有了纸底和浮雕之后，一条 ink 实线过重。
       降到 rule（#D3C6AC），分隔仍在，但不再抢注意力。 */
    ui_rule(bar, LV_BORDER_SIDE_BOTTOM, UI_RULE);

    int x = 6;
    if (back_cb) {
        lv_obj_t *back = lv_label_create(bar);
        lv_label_set_text(back, "<");
        /* 必须用 UI_FONT_CN（行高 26），不能用 UI_FONT_MID（行高 37）：
           对象只有 UI_TOPBAR_H=28 高，比 37 矮，LVGL 会把内容当成溢出——
           既会把文字往下偏移，又会给这个 label 打开滚动，箭头因此显得偏低、
           还能被垂直拖动。26 < 28，装得下，两个症状都消失。 */
        lv_obj_set_style_text_font(back, UI_FONT_CN, 0);
        lv_obj_set_size(back, 36, UI_TOPBAR_H);
        lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_left(back, 6, 0);
        /* 原来它就是一行光秃秃的「<」——在每一页都存在的主导航控件，却是全屏
           唯一一个没有任何「可按」外观的东西。给它一个抬高面加浮雕，和别的
           按钮讲同一种话。 */
        lv_obj_set_style_bg_color(back, UI_RAISE, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        ui_bevel_edge(back, true);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        /* 电阻屏漏点比电容屏更常见，返回箭头在每一页都存在；扩大到短边 >=48px
           （视觉上的 36x28 不变，只是可点区域向外扩） */
        lv_obj_set_ext_click_area(back, 14);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
        x = 40;
    }
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return bar;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    ui_plain(b);
    /* 默认主题（CONFIG_LV_THEME_DEFAULT_GROW）给按下状态加了 transform_width/height
       放大 + 80ms 缓动动画；phase 1 禁止任何动画，这里在本地样式里显式清零覆盖，
       不依赖 Kconfig（谁都可能把那个选项改回去） */
    lv_obj_set_style_transform_width(b, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(b, 0, LV_STATE_PRESSED);
    /* 同一件事的另一半：顶掉主题按下/松开两份 transition，见文件顶部注释 */
    lv_obj_set_style_transition(b, no_fade_transition(), LV_STATE_PRESSED);
    lv_obj_set_style_transition(b, no_fade_transition(), 0);
    lv_obj_set_style_bg_color(b, UI_RAISE, 0);
    lv_obj_set_style_bg_color(b, UI_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, UI_RAISE, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, UI_INK, 0);
    lv_obj_set_style_text_color(b, UI_DISABLED, LV_STATE_DISABLED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    /*
     * 凸起浮雕代替原来的整圈黑框。纸面上一圈 ink 实线太重，而「抬起来」本来就
     * 该靠边缘明暗表达、不靠描边——这是压印质感和描边风格的分界。
     *
     * **必须加在标签之后**：ui_bevel 会往按钮里塞一个子对象，而 ui_callsign.c、
     * ui_practice.c、ui_settings.c、ui_ota.c 共 6 处都在拿 lv_obj_get_child(btn, 0)
     * 当标签用。加在前面会让它们全部错位，且编译不报错。
     *
     * 按下时浮雕会翻成凹陷（ui_bevel 自己挂的事件回调），配合背景塌成
     * UI_PRESSED，「按进去了」这件事有两重表达。
     */
    ui_bevel(b, true);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = make_btn(parent, text, UI_FONT_CN, cb, user);
    lv_obj_set_height(b, 36);
    return b;
}

lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    return make_btn(parent, text, font, cb, user);
}

lv_obj_t *ui_notice(lv_obj_t *parent, const char *text)
{
    lv_obj_t *n = lv_obj_create(parent);
    ui_plain(n);
    lv_obj_set_size(n, LV_PCT(100), UI_NOTICE_H);
    lv_obj_set_pos(n, 0, UI_TOPBAR_H);
    lv_obj_set_style_bg_color(n, UI_SUNK, 0);
    ui_bevel(n, false);   /* 提示条是压进纸里的一条，不是浮起来的 */
    lv_obj_t *l = lv_label_create(n);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    return n;
}

static void confirm_close(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_del_async(overlay);
}

void ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user)
{
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    ui_plain(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, UI_PAPER, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);

    lv_obj_t *dlg = lv_obj_create(overlay);
    ui_plain(dlg);
    lv_obj_set_size(dlg, 260, 110);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, UI_RAISE, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, UI_INK, 0);

    lv_obj_t *l = lv_label_create(dlg);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *no = ui_button(dlg, btn_no, confirm_close, overlay);
    lv_obj_set_width(no, 110);
    lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 12, -12);

    lv_obj_t *yes = ui_button(dlg, btn_yes, yes_cb, user);
    lv_obj_set_width(yes, 110);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
}
