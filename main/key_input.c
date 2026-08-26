#include "key_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board_config.h"
#include "cw_settings.h"
#include "keyer.h"
#include "cw_decoder.h"
#include "tone_gen.h"

#define TICK_MS 5

static const char *TAG = "key_input";

static TaskHandle_t s_task;
static volatile bool s_running;
static keyer_t s_keyer;
static cw_decoder_t s_dec;
static volatile bool s_touch[2];
static bool s_ext_last[2], s_ext_stable[2];
/* volatile：task() 里读它、key_input_stop() 从另一个任务清它，见 task() 里
 * 拷贝到局部变量再用的注释。 */
static key_input_char_cb_t volatile s_cb;
static void *s_user;
static bool s_last_out;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void read_inputs(bool *dit, bool *dah)
{
    const cw_settings_t *c = cw_settings_get();
    bool a, b;
    if (c->key_input == CW_KEY_INPUT_EXT) {
        bool r0 = gpio_get_level(BOARD_KEY_DIT_GPIO) == 0;
        bool r1 = gpio_get_level(BOARD_KEY_DAH_GPIO) == 0;
        /* 10ms 去抖：只有连续两次 tick 读到同一电平，才把它采纳为稳定值。
         * 单个 tick 的毛刺这一次读数会和上一次不一致，stable 保持不变，不会
         * 被采纳；只有当毛刺后紧跟着的下一次读数和它自己一致时才会被接受——
         * 也就是说信号必须真的稳定维持满两个 tick 才算数，单 tick 抖动会被
         * 彻底挡在外面，而不是仅仅被延迟采纳。 */
        if (r0 == s_ext_last[0]) { s_ext_stable[0] = r0; }
        s_ext_last[0] = r0;
        if (r1 == s_ext_last[1]) { s_ext_stable[1] = r1; }
        s_ext_last[1] = r1;
        a = s_ext_stable[0];
        b = s_ext_stable[1];
    } else {
        a = s_touch[0];
        b = s_touch[1];
    }
    /* paddle_swap 只在这一处应用：不管输入源是触摸桨还是外接口，到这里为止
     * a/b 都已经统一成同一种"左/右"含义，交换只做这一次，触摸和外接口不会
     * 出现相反的对调结果。 */
    if (c->paddle_swap) {
        bool t = a;
        a = b;
        b = t;
    }
    *dit = a;
    *dah = b;
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
    s_ext_last[0] = s_ext_last[1] = false;
    s_ext_stable[0] = s_ext_stable[1] = false;
    s_last_out = false;
    s_cb = cb;
    s_user = user;
    if (c->key_input == CW_KEY_INPUT_EXT) {
        /* 启动自检，只在选中外接口模式时做：引脚已经在 key_input_init() 里配置
         * 成输入+内部上拉，这里立即采样一次原始电平——不经过去抖（去抖要两个
         * tick 才能给出稳定值，这里要的是"这一刻马上能看到"的诊断信息，不是
         * 拿去驱动 keyer）。如果操作者还没碰任何东西，某根线就已经读成"按下"，
         * 说明这根线的空闲电平不是预期的高电平（例如 GPIO27，见 board_config.h
         * 里针对它的说明），键控从一开始就会是错的、且看起来毫无缘由。这里只打
         * 警告日志、不阻止外接口模式启用——换一块接线方式不同的板子上这两根线
         * 完全可能是正常的高电平，不能靠猜就拒绝启用。 */
        if (gpio_get_level(BOARD_KEY_DIT_GPIO) == 0) {
            ESP_LOGW(TAG, "external dit line (GPIO%d) reads ACTIVE before any key press - idle level is not the expected high, this pin will behave as a stuck key", (int)BOARD_KEY_DIT_GPIO);
        }
        if (gpio_get_level(BOARD_KEY_DAH_GPIO) == 0) {
            ESP_LOGW(TAG, "external dah line (GPIO%d) reads ACTIVE before any key press - idle level is not the expected high, this pin will behave as a stuck key", (int)BOARD_KEY_DAH_GPIO);
        }
    }
    s_running = true;
    xTaskNotifyGive(s_task);
}

void key_input_stop(void)
{
    /* 不在这里调用 tone_gen_set：tone_gen_set 只应该来自 key_input 自己的任务
     * (见 task() 里的说明)。这里只需要把 s_running 置 false、清掉回调，任务
     * 最迟在下一个 tick（至多 TICK_MS）之内会自己发现并关掉侧音。 */
    s_running = false;
    s_cb = NULL;
}

void key_input_touch(int paddle, bool pressed)
{
    if (paddle < 0 || paddle > 1) return;
    s_touch[paddle] = pressed;
}

const char *key_input_pattern(void) { return cw_decoder_pattern(&s_dec); }
bool key_input_key_down(void) { return s_last_out; }
