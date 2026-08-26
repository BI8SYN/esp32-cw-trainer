#include "cw_player.h"
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "tone_gen.h"
#include "morse_table.h"

static const char *TAG = "cw_player";
#define TEXT_MAX 64
#define LEAD_IN_MS 500

static esp_timer_handle_t s_timer;
static morse_timing_t s_t = { 60, 180, 60, 180, 420 };
static char s_text[TEXT_MAX];
static int s_idx;               /* 当前字符下标 */
static const char *s_pat;       /* 当前字符点划串 */
static int s_pat_idx;
static bool s_key_on;
static volatile bool s_playing;
static cw_player_cb_t s_cb;
static void *s_user;

static void arm(uint32_t ms) { esp_timer_start_once(s_timer, (uint64_t)ms * 1000); }

static void finish(cw_player_event_t ev)
{
    tone_gen_set(false);
    s_playing = false;
    if (s_cb) s_cb(ev, 0, s_user);
}

/* 前进到下一个可发字符；返回 false 表示文本结束 */
static bool next_char(void)
{
    while (s_text[s_idx]) {
        char c = s_text[s_idx];
        if (c == ' ') {
            s_idx++;
            arm(s_t.word_gap_ms - s_t.char_gap_ms);
            return true;
        }
        s_pat = morse_table_lookup(c);
        if (s_pat) {
            s_pat_idx = 0;
            if (s_cb) s_cb(CW_PLAYER_EV_CHAR_START, c, s_user);
            s_key_on = true;
            tone_gen_set(true);
            arm(s_pat[0] == '.' ? s_t.dit_ms : s_t.dah_ms);
            s_idx++;
            return true;
        }
        s_idx++;   /* 未知字符跳过 */
    }
    return false;
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (!s_playing) return;

    if (s_key_on) {
        /* 元素结束 → 关音，安排间隔 */
        tone_gen_set(false);
        s_key_on = false;
        s_pat_idx++;
        if (s_pat && s_pat[s_pat_idx]) arm(s_t.intra_gap_ms);
        else { s_pat = NULL; arm(s_t.char_gap_ms); }
        return;
    }
    if (s_pat) {
        /* 字符内下一个元素 */
        s_key_on = true;
        tone_gen_set(true);
        arm(s_pat[s_pat_idx] == '.' ? s_t.dit_ms : s_t.dah_ms);
        return;
    }
    if (!next_char()) finish(CW_PLAYER_EV_DONE);
}

esp_err_t cw_player_init(void)
{
    const esp_timer_create_args_t a = { .callback = timer_cb, .name = "cw_player" };
    return esp_timer_create(&a, &s_timer);
}

void cw_player_set_timing(const morse_timing_t *t) { s_t = *t; }

esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user)
{
    cw_player_stop();
    strncpy(s_text, text, TEXT_MAX - 1);
    s_text[TEXT_MAX - 1] = '\0';
    s_idx = 0; s_pat = NULL; s_pat_idx = 0; s_key_on = false;
    s_cb = cb; s_user = user;
    s_playing = true;
    ESP_LOGD(TAG, "play \"%s\"", s_text);
    arm(LEAD_IN_MS);
    return ESP_OK;
}

void cw_player_stop(void)
{
    if (!s_playing) return;
    esp_timer_stop(s_timer);
    finish(CW_PLAYER_EV_STOPPED);
}

bool cw_player_is_playing(void) { return s_playing; }
