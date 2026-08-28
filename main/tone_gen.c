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

static int16_t s_sine[SINE_N];
static volatile bool s_on = false;
static volatile uint32_t s_phase_inc = 0;   /* Q16.16 表索引步进 */
static volatile uint16_t s_amp = 0;         /* 0..32767 */
static bool s_ok = false;
static volatile bool s_amp_hold = false;

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

    while (true) {
        bool on = s_on;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            if (on && env < RAMP_SAMPLES) env++;
            else if (!on && env > 0) env--;
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
        bool active = on || env > 0 || s_amp_hold;
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
    if (hz < 300) hz = 300;
    if (hz > 1200) hz = 1200;
    s_phase_inc = (uint32_t)(((uint64_t)hz * SINE_N << 16) / SAMPLE_RATE);
}

void tone_gen_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* 对数感：percent^2 */
    s_amp = (uint16_t)((uint32_t)32767 * percent * percent / 10000);
}
