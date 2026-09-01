/*
 * 画面指纹：让板子自己回答「屏幕变了没有」。
 *
 * 起因是样式层重构——那次改动的验收标准是「改完和改前一模一样」，而这是一个
 * 人眼很难判、且必须逐页判的问题。更要命的是它**不是假想风险**：ui_common.c
 * 里那段 no_fade_transition 的注释明确依赖「本地样式永远排在主题样式前面」的
 * 插入顺序，而把本地样式改成 lv_obj_add_style 恰恰会动这个顺序。这类错误不报
 * 编译错、不崩溃，只是渲染悄悄变了。
 *
 * 做法是在 flush_cb 上串一层钩子——那里流过的是**真正送进屏幕的像素**，不是
 * 我们以为会送进去的东西，所以它不会替代码撒谎。不传图只传指纹：一张 320×240
 * 的 RGB565 是 150KB，串口 115200 要走十几秒；而一个 32 位指纹就够回答
 * 「变了没有」，真变了再单独去看那一页。
 *
 * 关于「两遍」：主页上有电量、WiFi 信号、时钟这些会自己变的东西，指纹当然会
 * 跟着变。与其去一个个把它们钉死（那等于为了量它而改它），不如**连跑两遍**：
 * 同一次开机里两遍一致的页面才有资格参与前后比对，不一致的页面自己会暴露出来。
 * 这样这把尺子的可信度是它自己证明的，不是我假设的。
 */
#include "screenshot.h"
#include <stdio.h>
#include "lvgl.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_more.h"
#include "ui_net.h"
#include "ui_stats.h"
#include "ui_settings.h"
#include "ui_keyer.h"
#include "ui_wifi.h"
#include "ui_ota.h"
#include "ui_callsign.h"
#include "ui_fist.h"

/* FNV-1a 32 位。这里只要「变了没有」，不需要 CRC 的纠错性质，
   而 FNV 十行就能写完、不引入任何依赖。 */
#define FNV_OFFSET 2166136261u
#define FNV_PRIME  16777619u

static uint32_t s_hash;
static uint32_t s_px;
static bool     s_capturing;
static void (*s_orig_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);

static void hash_bytes(const uint8_t *p, size_t n)
{
    uint32_t h = s_hash;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= FNV_PRIME; }
    s_hash = h;
}

#if CW_SCREENSHOT_DUMP
/*
 * 导图模式：除了指纹，还把像素本身 base64 出来，主机端拼回 PNG。
 *
 * 平时不开——一张 320x240 的 RGB565 是 150KB，base64 之后 205KB，115200 的
 * 串口要走十八秒。指纹回答「变了没有」够用且是秒回；只有在需要**看**的时候
 * （比如换色之后确认长什么样）才值得花这个时间，而且只导几页。
 */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void emit_b64(const uint8_t *p, size_t n)
{
    /* 每 60 字节输出一行，避免主机端行缓冲被撑爆 */
    size_t col = 0;
    uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        acc = (acc << 8) | p[i]; bits += 8;
        while (bits >= 6) {
            bits -= 6;
            putchar(B64[(acc >> bits) & 0x3F]);
            if (++col == 76) { putchar('\n'); col = 0; }
        }
    }
    if (bits) {
        putchar(B64[(acc << (6 - bits)) & 0x3F]);
        col++;
    }
    if (col) putchar('\n');
}
#endif

static void cap_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c)
{
    if (s_capturing) {
        /* 连区域坐标一起进指纹：万一哪天刷新被切成了不同的块，
           指纹会变，而这本身就是值得知道的事，不该被悄悄吸收掉。 */
        int16_t box[4] = { a->x1, a->y1, a->x2, a->y2 };
        hash_bytes((const uint8_t *)box, sizeof box);
        uint32_t n = lv_area_get_size(a);
        hash_bytes((const uint8_t *)c, n * sizeof(lv_color_t));
        s_px += n;
#if CW_SCREENSHOT_DUMP
        printf("CHUNK %d %d %d %d\n", a->x1, a->y1, a->x2, a->y2);
        emit_b64((const uint8_t *)c, n * sizeof(lv_color_t));
#endif
    }
    s_orig_flush(d, a, c);
}

void screenshot_install(void)
{
    lv_disp_t *d = lv_disp_get_default();
    if (!d || !d->driver || !d->driver->flush_cb) {
        printf("SNAP ERROR no display driver\n");
        return;
    }
    s_orig_flush = d->driver->flush_cb;
    d->driver->flush_cb = cap_flush;
}

static void shot(const char *name, ui_screen_builder_t build)
{
    /*
     * 顺序很讲究：**必须先把上一页彻底删掉，再建下一页**，中间垫一张空屏。
     *
     * ui_nav_show() 是反过来的——先 builder() 再 lv_obj_del_async(old)。正常
     * 使用下没问题，因为人不会从一个页面导航到同一个页面。但巡检会：app_main
     * 已经显示了主页，巡检第一站又是主页，于是「新主页」先把 ui_home.c 的模块
     * 静态量 s_timer/s_scr 指向自己，紧接着「旧主页」的 on_delete 把这两个量
     * 清成 NULL——清掉的是新主页的。下一次删就 lv_timer_del(NULL)，
     * LoadProhibited。实测就是这么崩的。
     *
     * 垫空屏是因为不能删正在显示的屏幕：先把空屏 load 上去，旧页就不再是当前
     * 屏幕，可以安全同步删；等它的 on_delete 跑完（静态量归零完毕）再 build()，
     * 新页拿到的静态量才是干净的。
     */
    lv_obj_t *blank = lv_obj_create(NULL);
    lv_obj_t *old = lv_scr_act();
    lv_scr_load(blank);
    if (old && old != blank) lv_obj_del(old);

    lv_obj_t *scr = build();
    lv_scr_load(scr);
    lv_obj_del(blank);

    lv_obj_invalidate(scr);
    s_hash = FNV_OFFSET;
    s_px = 0;
    s_capturing = true;
    lv_refr_now(NULL);          /* 同步重绘，返回时像素已经全部流过钩子 */
    s_capturing = false;

    printf("SNAP %-9s px=%6u hash=%08X\n", name, (unsigned)s_px, (unsigned)s_hash);
}

void screenshot_tour(void)
{
    static const struct { const char *name; ui_screen_builder_t fn; } pages[] = {
        { "home",     ui_home_create     },
        { "more",     ui_more_create     },
        { "settings", ui_settings_create },
        { "stats",    ui_stats_create    },
        { "keyer",    ui_keyer_create    },
        { "wifi",     ui_wifi_create     },
        { "ota",      ui_ota_create      },
        { "callsign", ui_callsign_create },
        { "fist",     ui_fist_create     },
        { "net",      ui_net_create      },
    };
    const int n = (int)(sizeof pages / sizeof pages[0]);

#if CW_SCREENSHOT_DUMP
    /* 导图只跑一遍、且只导五页：全导十页要三分钟串口时间。这五页覆盖了
       卡片 / 整宽列表 / tab+列表 / 图表 / 黄铜材质五种版式，够看长相了。 */
    printf("SNAP-PASS 1\n");
    const int want[] = { 0, 2, 4, 9 };   /* home settings keyer net */
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++)
        if (want[i] < n) shot(pages[want[i]].name, pages[want[i]].fn);
#else
    for (int pass = 1; pass <= 2; pass++) {
        printf("SNAP-PASS %d\n", pass);
        for (int i = 0; i < n; i++) shot(pages[i].name, pages[i].fn);
    }
#endif
    printf("SNAP-DONE\n");

    /* 巡完把屏幕交还给主页，否则用户拿到手是最后一页 */
    ui_nav_show(ui_home_create);
}
