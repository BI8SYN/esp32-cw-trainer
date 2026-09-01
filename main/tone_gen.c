#include "tone_gen.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "board_config.h"

static const char *TAG = "tone_gen";

#define SAMPLE_RATE     32000
#define BLOCK_SAMPLES   64          /* 2ms */
#define DMA_BUF_COUNT   3
#define RAMP_SAMPLES    160         /* 5ms */
#define AMP_OFF_DELAY_BLOCKS 100    /* 200ms */
#define SINE_N          256
/*
 * 静音门闩 150ms：管电键侧音和收报回放这类没有结构化长间隔的路径。
 * 听写那种字符间的长静音由 tone_gen_hold() 管，见头文件里的说明。
 */
#define QUIET_GATE_BLOCKS   75      /* 150ms / 2ms */
/*
 * 功放关着时，第一声会破——这正是 tone_gen_amp_hold() 那段注释描述的现象。
 * 提示音可能发生在任何页面，包括没有要求功放待命的主页和设置页，所以在真正
 * 出声之前先垫一段静音，让功放上电稳定。已经开着就不垫，点击反馈不该白等。
 */
#define SEQ_LEADIN_MS       50

static int16_t s_sine[SINE_N];
static volatile bool s_on = false;
static volatile uint32_t s_phase_inc = 0;   /* Q16.16 表索引步进 */
static volatile uint16_t s_amp = 0;         /* 0..32767 */
static bool s_ok = false;
static volatile bool s_amp_hold = false;
static volatile bool s_hold = false;              /* CW 会话占用 */
static tone_step_t   s_pend[TONE_SEQ_MAX];        /* 待播的提示音 */
static volatile uint8_t s_pend_n = 0;

static uint32_t hz_to_inc(uint16_t hz)
{
    return (uint32_t)(((uint64_t)hz * SINE_N << 16) / SAMPLE_RATE);
}

static void amp_enable(bool en)
{
    gpio_set_level(BOARD_AUDIO_EN_GPIO, en ? BOARD_AUDIO_EN_ACTIVE : BOARD_AUDIO_MUTE_LEVEL);
}

static void tone_task(void *arg)
{
    (void)arg;
    static uint16_t buf[BLOCK_SAMPLES * 2];   /* L/R 交错 */
    uint32_t phase = 0;
    int32_t env = 0;                          /* 0..RAMP_SAMPLES */
    int off_blocks = AMP_OFF_DELAY_BLOCKS;
    bool amp_on = false;

    /* 提示音的播放状态。全部是任务的局部量——只有这一个任务碰它们，
       和外面交接的只有 s_pend/s_pend_n 那一次拷贝。 */
    tone_step_t seq[TONE_SEQ_MAX];
    int      seq_n = 0;
    int      seq_cur = -1;        /* 当前步下标，-1 = 没在播 */
    int32_t  seq_rem = 0;         /* 当前步剩余样本 */
    int32_t  seq_lead = 0;        /* 功放上电的垫场静音，剩余样本 */
    uint32_t save_inc = 0;        /* 提示音借走振荡器前的侧音频率 */
    uint32_t quiet = 0;           /* 已经安静了多少个块 */

    while (true) {
        bool on = s_on;

        /*
         * 抢占：CW 路径一开口，正在播的提示音立即让出并还原频率。
         *
         * 提示音是四个生产者里优先级最低的那个，永远不该赢。这个判断放在块的
         * 开头、每 2ms 一次，最坏情况下 CW 的第一个码元被提示音污染 2ms——
         * 而包络本身就有 5ms 的淡入，听不出来。
         */
        if (on && seq_cur >= 0) { seq_cur = -1; s_phase_inc = save_inc; }

        if (!on && env == 0) { if (quiet < 0xFFFF) quiet++; }
        else quiet = 0;

        if (s_pend_n) {
            if (seq_cur < 0 && !on && env == 0 && !s_hold && quiet >= QUIET_GATE_BLOCKS) {
                seq_n = s_pend_n > TONE_SEQ_MAX ? TONE_SEQ_MAX : s_pend_n;
                for (int k = 0; k < seq_n; k++) seq[k] = s_pend[k];
                save_inc = s_phase_inc;
                seq_lead = amp_on ? 0 : (int32_t)SEQ_LEADIN_MS * (SAMPLE_RATE / 1000);
                seq_cur = 0;
                seq_rem = (int32_t)seq[0].ms * (SAMPLE_RATE / 1000);
                if (seq[0].hz) s_phase_inc = hz_to_inc(seq[0].hz);
            }
            /* 播了或丢了都清掉：整段丢弃，不排队。见头文件说明。 */
            s_pend_n = 0;
        }

        /* play 取代 on 驱动包络：提示音期间由序列说了算，其余时候跟 s_on */
        bool play = on;
        if (seq_cur >= 0) {
            if (seq_lead > 0) {
                seq_lead -= BLOCK_SAMPLES;
                play = false;
            } else {
                seq_rem -= BLOCK_SAMPLES;
                while (seq_cur >= 0 && seq_rem <= 0) {
                    seq_cur++;
                    if (seq_cur >= seq_n) { seq_cur = -1; s_phase_inc = save_inc; }
                    else {
                        seq_rem += (int32_t)seq[seq_cur].ms * (SAMPLE_RATE / 1000);
                        if (seq[seq_cur].hz) s_phase_inc = hz_to_inc(seq[seq_cur].hz);
                    }
                }
                play = seq_cur >= 0 && seq[seq_cur].hz != 0;
            }
        }

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            if (play && env < RAMP_SAMPLES) env++;
            else if (!play && env > 0) env--;
            int32_t v = 0;
            if (env > 0) {
                int32_t s = s_sine[(phase >> 16) & (SINE_N - 1)];
                v = (s * (int32_t)s_amp / 32767) * env / RAMP_SAMPLES;
                phase += s_phase_inc;
            }
            uint16_t w = (uint16_t)(v + 32768);
            buf[i * 2] = w;
            buf[i * 2 + 1] = w;
        }
        /* s_amp_hold：页面提前要求功放待命，见 tone_gen_amp_hold()。
           它让功放在用户按键之前就完成上电稳定，第一声才不会破。 */
        /* 提示音也要让功放开着：seq_cur >= 0 覆盖了垫场静音那一段，
           功放正是在那段时间里上电稳定的。 */
        bool active = play || env > 0 || s_amp_hold || seq_cur >= 0;
        if (active) { off_blocks = 0; if (!amp_on) { amp_enable(true); amp_on = true; } }
        else if (amp_on && ++off_blocks >= AMP_OFF_DELAY_BLOCKS) { amp_enable(false); amp_on = false; }

        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, sizeof buf, &written, portMAX_DELAY);
    }
}

void tone_gen_amp_hold(bool hold) { s_amp_hold = hold; }

esp_err_t tone_gen_init(void)
{
    for (int i = 0; i < SINE_N; i++) s_sine[i] = (int16_t)(sinf(2.0f * (float)M_PI * i / SINE_N) * 32767.0f);
    tone_gen_set_freq(600);
    tone_gen_set_volume(70);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_AUDIO_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    amp_enable(false);

    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = BLOCK_SAMPLES,
        .use_apll = false,
        .tx_desc_auto_clear = true,
    };
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_driver_install: %s", esp_err_to_name(err)); return err; }
    err = i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_set_dac_mode: %s", esp_err_to_name(err)); i2s_driver_uninstall(I2S_NUM_0); return err; }
    i2s_zero_dma_buffer(I2S_NUM_0);

    if (xTaskCreatePinnedToCore(tone_task, "tone", 3072, NULL, configMAX_PRIORITIES - 2, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_FAIL;
    }
    s_ok = true;
    ESP_LOGI(TAG, "ready: %d Hz, dma %d x %d samples", SAMPLE_RATE, DMA_BUF_COUNT, BLOCK_SAMPLES);
    return ESP_OK;
}

bool tone_gen_is_ok(void) { return s_ok; }
void tone_gen_set(bool on) { s_on = on; }

void tone_gen_set_freq(uint16_t hz)
{
    /* 这个夹取是给**侧音**定的合理音区。提示音要上到 4kHz，不走这里，
       直接用 hz_to_inc()——见 tone_task 里的序列播放。 */
    if (hz < 300) hz = 300;
    if (hz > 1200) hz = 1200;
    s_phase_inc = hz_to_inc(hz);
}

void tone_gen_hold(bool hold) { s_hold = hold; }

void tone_gen_seq(const tone_step_t *steps, uint8_t n)
{
    if (!s_ok || !steps || !n) return;
    if (n > TONE_SEQ_MAX) n = TONE_SEQ_MAX;
    /* 先填内容再置数量：tone_task 是靠 s_pend_n 非零才去读 s_pend 的，
       反过来写会让它读到半份数据。两者都是任务外唯一的写入点，且这个函数
       只从 UI 任务调用，不会自己和自己竞争。 */
    for (uint8_t i = 0; i < n; i++) s_pend[i] = steps[i];
    s_pend_n = n;
}

void tone_gen_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* 对数感：percent^2 */
    s_amp = (uint16_t)((uint32_t)32767 * percent * percent / 10000);
}
