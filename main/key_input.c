#include "key_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board_config.h"
#include "cw_settings.h"
#include "keyer.h"
#include "key_merge.h"
#include "cw_decoder.h"
#include "tone_gen.h"

#define TICK_MS 5

static const char *TAG = "key_input";

static TaskHandle_t s_task;
static volatile bool s_running;
static keyer_t s_keyer;
static cw_decoder_t s_dec;
static volatile bool s_touch[2];
static bool s_ext_last[2], s_ext_stable[2], s_ext_armed[2];
/* 屏幕上第 i 块的按下状态（触摸与外接合并后、左右对调之前），给 UI 高亮用。
 * 由 5ms 任务写、LVGL 任务读，所以是 volatile。 */
static volatile bool s_in[2];
/* volatile：task() 里读它、key_input_stop() 从另一个任务清它，见 task() 里
 * 拷贝到局部变量再用的注释。 */
static key_input_char_cb_t volatile s_cb;
static void *s_user;
static bool s_last_out;
static key_input_edge_cb_t volatile s_edge_cb;
static void *s_edge_user;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void read_inputs(bool *dit, bool *dah)
{
    const cw_settings_t *c = cw_settings_get();
    const bool raw[2] = {
        gpio_get_level(BOARD_KEY_DIT_GPIO) == 0,
        gpio_get_level(BOARD_KEY_DAH_GPIO) == 0,
    };
    for (int i = 0; i < 2; i++) {
        /* 10ms 去抖：只有连续两次 tick 读到同一电平，才把它采纳为稳定值。
         * 单个 tick 的毛刺这一次读数会和上一次不一致，stable 保持不变，不会
         * 被采纳；只有当毛刺后紧跟着的下一次读数和它自己一致时才会被接受——
         * 也就是说信号必须真的稳定维持满两个 tick 才算数，单 tick 抖动会被
         * 彻底挡在外面，而不是仅仅被延迟采纳。 */
        if (raw[i] == s_ext_last[i]) s_ext_stable[i] = raw[i];
        s_ext_last[i] = raw[i];
        /* 门闩：见 key_input.h。判据必须用去抖后的值，不能用 raw[i]——原始电平
         * 上一个方向相反的毛刺就足以给一根真正卡住的线开闸，而开闸之后去抖值
         * 仍然是"按下"，键照样被钉死，门闩就白设了。去抖要求连续两拍一致，
         * 毛刺过不去。 */
        if (!s_ext_stable[i]) s_ext_armed[i] = true;
    }

    /* 直键不参与左右对调：它只有一个触点，"左右"无从谈起。真按对调处理的话，
     * 屏幕上那一整块（走 touch[0]）会被换到 dah 上，而 keyer 的直键分支只看
     * dit——结果是开了对调之后屏幕上的直键按了没有任何反应。外接的直键同理，
     * 固定留在点线（IO18 / TRS 的 Tip），这也正是单声道插头的接法。 */
    const bool swap = c->paddle_swap && c->keyer_mode != KEYER_MODE_STRAIGHT;

    const bool touch[2] = { s_touch[0], s_touch[1] };
    key_merge_out_t m;
    key_merge(touch, s_ext_stable, s_ext_armed, swap, &m);
    s_in[0] = m.in[0];
    s_in[1] = m.in[1];
    *dit = m.dit;
    *dah = m.dah;
}

static void task(void *arg)
{
    (void)arg;
    while (true) {
        if (!s_running) {
            /* 任务自己发现被要求停止时，由它自己关掉侧音再阻塞等待下一次启动
             * 通知。tone_gen_set 全程只从这一个任务里调用（正常 tick 里也是），
             * key_input_stop() 本身不直接调用它——这样就不会出现"stop() 那边
             * 刚把侧音关掉，任务这边正巧还在处理没关完的那一拍又把它打开"的
             * 竞争，侧音要么已经在正常 tick 里跟随 keyer 输出，要么在这里被
             * 明确置为关闭，不存在停止之后被落下的中间状态。 */
            tone_gen_set(false);
            s_last_out = false;
            /* 用任务通知代替裸的 vTaskSuspend/vTaskResume：xTaskNotifyGive 会把
             * 通知置成"待处理"，哪怕这一刻任务还没走到下面这行也不会丢——下一次
             * ulTaskNotifyTake 会立刻消费掉它直接返回，不会真的阻塞。换成
             * vTaskResume 就不是这样：如果 resume 发生在任务真正执行到
             * vTaskSuspend(NULL) 之前，这次 resume 会因为目标任务当时还不是
             * "挂起"状态而直接被丢弃，任务随后仍然会把自己挂起，而且再也没有
             * 人能把它唤醒——是经典的丢失唤醒竞争，用通知从根本上避免。 */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        uint32_t t = now_ms();
        bool dit, dah;
        read_inputs(&dit, &dah);
        keyer_set_input(&s_keyer, dit, dah);
        bool out = keyer_tick(&s_keyer, t);
        if (out != s_last_out) {
            tone_gen_set(out);
            cw_decoder_key(&s_dec, out, t);
            s_last_out = out;
            /* 同 s_cb 的写法：先拷到局部再判空再调用。另一个任务可能正在把它
             * 清成 NULL，分两次求值会在中间那一小段窗口里经空指针跳转崩掉。 */
            key_input_edge_cb_t ecb = s_edge_cb;
            if (ecb) ecb(out, t, s_edge_user);
        }
        char c;
        /* 先把 s_cb 拷到局部变量再判断、再调用：key_input_stop() 会从另一个任务
         * 把 s_cb 清成 NULL。如果这里像原来那样分别对 s_cb 求值两次
         * （一次判空、一次调用），中间这一小段窗口里 key_input_stop() 完全可能
         * 插进来把它清空——判空时读到的还是非空的旧值，真正调用时却重新读到了
         * NULL，会经一个空函数指针跳转直接崩掉。拷到局部变量后，只要判空那一次
         * 读到非空，调用时用的就是同一个已经确认非空的值，不会再被并发的
         * key_input_stop() 影响。 */
        key_input_char_cb_t cb = s_cb;
        if (cw_decoder_poll(&s_dec, t, &c) && cb) cb(c, s_user);
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t key_input_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_KEY_DIT_GPIO) | (1ULL << BOARD_KEY_DAH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    keyer_init(&s_keyer, KEYER_MODE_IAMBIC_B, 60);
    cw_decoder_init(&s_dec, 60);
    return xTaskCreatePinnedToCore(task, "key_input", 3072, NULL, configMAX_PRIORITIES - 3, &s_task, 1) == pdPASS ? ESP_OK : ESP_FAIL;
}

void key_input_start(key_input_char_cb_t cb, void *user)
{
    const cw_settings_t *c = cw_settings_get();
    uint16_t dit_ms = (uint16_t)(1200 / (c->char_wpm ? c->char_wpm : 20));
    keyer_init(&s_keyer, (keyer_mode_t)c->keyer_mode, dit_ms);
    cw_decoder_init(&s_dec, dit_ms);
    s_touch[0] = s_touch[1] = false;
    s_in[0] = s_in[1] = false;
    /* 原始采样一次，给去抖状态播种，这样去抖值从第一拍起就有效；门闩的初值也由
     * 它决定。引脚已经在 key_input_init() 里配成输入+内部上拉，此刻读到"按下"
     * 只可能是这根线本身就被拉低了：本机 GPIO27 上的蜂鸣器是一例，成品直键的
     * TS 单声道插头插进立体声母座时套筒把 Ring 和 Sleeve 短接、划线恒为低是
     * 另一例——后者每插一次直键都会发生。这样的线不会被开闸。 */
    for (int i = 0; i < 2; i++) {
        bool r = gpio_get_level(i == 0 ? BOARD_KEY_DIT_GPIO : BOARD_KEY_DAH_GPIO) == 0;
        s_ext_last[i] = s_ext_stable[i] = r;
        s_ext_armed[i] = !r;
        if (r) ESP_LOGW(TAG, "external %s line (GPIO%d) reads ACTIVE before any key press - ignored until it is seen released once",
                        i == 0 ? "dit" : "dah",
                        (int)(i == 0 ? BOARD_KEY_DIT_GPIO : BOARD_KEY_DAH_GPIO));
    }
    s_last_out = false;
    s_cb = cb;
    s_user = user;
    /* 接管电键的人从一张白纸开始：上一个页面挂的边沿回调在这里清掉，
       免得它继续往一个已经不可见的页面的缓冲里写。要边沿的页面在
       start 之后自己再挂。 */
    s_edge_cb = NULL;
    s_edge_user = NULL;
    /* 提前给功放上电：等用户真按下键时它已经稳定，第一声才不会破音。
       见 tone_gen_amp_hold() 的说明。 */
    tone_gen_amp_hold(true);
    s_running = true;
    xTaskNotifyGive(s_task);
}

bool key_input_stop_if(void *owner)
{
    /* 归属不对说明电键已经被后建的页面接管了，这次 stop 请求属于正在被
       删除的旧页面——什么都不做才是对的。见头文件里的说明。 */
    if (!s_running || s_user != owner) return false;
    key_input_stop();
    return true;
}

void key_input_stop(void)
{
    /* 不在这里调用 tone_gen_set：tone_gen_set 只应该来自 key_input 自己的任务
     * (见 task() 里的说明)。这里只需要把 s_running 置 false、清掉回调，任务
     * 最迟在下一个 tick（至多 TICK_MS）之内会自己发现并关掉侧音。 */
    s_running = false;
    s_cb = NULL;
    s_edge_cb = NULL;
    s_edge_user = NULL;
    /* 释放后由 tone_gen 自己的 200ms 延迟关断功放，不用精确配对 */
    tone_gen_amp_hold(false);
}

void key_input_touch(int paddle, bool pressed)
{
    if (paddle < 0 || paddle > 1) return;
    s_touch[paddle] = pressed;
}

void key_input_set_edge_cb(key_input_edge_cb_t cb, void *user)
{
    /* 先挂 user 再挂 cb，先摘 cb 再摘 user：这样 5ms 任务那边无论在哪一拍
     * 读到非空的 cb，配套的 user 都已经是有效值。 */
    if (cb) {
        s_edge_user = user;
        s_edge_cb = cb;
    } else {
        s_edge_cb = NULL;
        s_edge_user = NULL;
    }
}

const char *key_input_pattern(void) { return cw_decoder_pattern(&s_dec); }
bool key_input_key_down(void) { return s_last_out; }

bool key_input_paddle_down(int paddle)
{
    if (paddle < 0 || paddle > 1) return false;
    return s_in[paddle];
}

uint8_t key_input_ext_blocked(void)
{
    /* 没在跑的时候门闩状态是上一次的残留，对调用方没有意义，报 0。 */
    if (!s_running) return 0;
    return (uint8_t)((s_ext_armed[0] ? 0 : 1) | (s_ext_armed[1] ? 0 : 2));
}
