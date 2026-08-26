#include "cw_player.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "tone_gen.h"
#include "morse_table.h"

static const char *TAG = "cw_player";
#define TEXT_MAX 64
#define LEAD_IN_MS 500

static esp_timer_handle_t s_timer;
static SemaphoreHandle_t s_mutex;   /* 保护 play()/stop() 与 timer_cb() 之间的互斥 */
static morse_timing_t s_t = { 60, 180, 60, 180, 420 };
static char s_text[TEXT_MAX];
static int s_idx;               /* 当前字符下标 */
static const char *s_pat;       /* 当前字符点划串 */
static int s_pat_idx;
static bool s_key_on;
static volatile bool s_playing;
static cw_player_cb_t s_cb;
static void *s_user;

/* 待触发的一次用户回调：状态转换在锁内完成并把要上报的内容写进这里，
 * 真正调用 s_cb() 则推迟到调用方释放 s_mutex 之后。这样用户回调里同步调用
 * cw_player_play()/cw_player_stop() 就不会对同一把非递归锁自锁死——DONE 是
 * 在 esp_timer 服务任务上触发的，那里的死锁会拖住系统里所有其他定时器。 */
typedef struct {
    cw_player_cb_t cb;
    void           *user;
    cw_player_event_t ev;
    char            ch;
    bool            fire;
} pending_cb_t;

static void fire_cb(const pending_cb_t *p)
{
    if (p->fire) p->cb(p->ev, p->ch, p->user);
}

/* 先停后启：确保对一个已装表的定时器重新 arm 不会因为
 * esp_timer_start_once() 在定时器已激活时返回 ESP_ERR_INVALID_STATE
 * 而被静默忽略——旧计划一次残留的调度。加上警告日志，把潜在的静默失败
 * 变成看得见的失败。 */
static void arm(uint32_t ms)
{
    esp_timer_stop(s_timer);   /* 未装表时返回错误也无妨，忽略即可 */
    esp_err_t err = esp_timer_start_once(s_timer, (uint64_t)ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "arm(%u ms) failed: %s", (unsigned)ms, esp_err_to_name(err));
    }
}

/* 状态收尾（关音、标记不在播放），并把待触发的回调快照进 *out——调用方必须
 * 已经持有 s_mutex，且必须在释放锁之后才能调用 fire_cb(out)。 */
static void finish_locked(cw_player_event_t ev, pending_cb_t *out)
{
    tone_gen_set(false);
    s_playing = false;
    out->fire = (s_cb != NULL);
    out->cb   = s_cb;
    out->user = s_user;
    out->ev   = ev;
    out->ch   = 0;
}

/* 前进到下一个可发字符；返回 false 表示文本结束。调用方必须持有锁；如果
 * 遇到需要上报 CHAR_START 的字符，把待触发的回调写进 *out（不在这里调用，
 * 同样必须留到调用方释放锁之后再触发）。 */
static bool next_char(pending_cb_t *out)
{
    while (s_text[s_idx]) {
        char c = s_text[s_idx];
        if (c == ' ') {
            s_idx++;
            arm(s_t.word_gap_ms > s_t.char_gap_ms ? s_t.word_gap_ms - s_t.char_gap_ms : 0);
            return true;
        }
        s_pat = morse_table_lookup(c);
        if (s_pat) {
            s_pat_idx = 0;
            out->fire = (s_cb != NULL);
            out->cb   = s_cb;
            out->user = s_user;
            out->ev   = CW_PLAYER_EV_CHAR_START;
            out->ch   = c;
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

/* cw_player_stop() 的无锁实现：调用方必须已经持有 s_mutex。只做状态收尾，
 * 不调用回调——回调交给调用方在释放锁之后触发（见 finish_locked 的注释）。 */
static void stop_locked(pending_cb_t *out)
{
    out->fire = false;
    if (!s_playing) return;
    esp_timer_stop(s_timer);
    finish_locked(CW_PLAYER_EV_STOPPED, out);
}

static void timer_cb(void *arg)
{
    (void)arg;
    pending_cb_t pending = { .fire = false };

    /* 用 0 超时非阻塞地抢锁：esp_timer 任务绝不能阻塞，阻塞会拖慢系统里
     * 所有其他定时器。抢不到说明 stop() 或 play() 正在运行——不管是哪一个，
     * "正确的下一步" 已经在持锁的那一方里发生了：
     *   - 如果是 stop() 持锁：这条链本来就该终止，什么都不用做。
     *   - 如果是 play() 持锁：它会自己装一个全新的调度（含 500ms 静音前导）。
     * 这里唯一要做的是把音关掉退出，绝不能碰状态或重新 arm，否则就会和
     * 持锁的一方打架，重新制造上一轮修复过的"卡音"问题。 */
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        tone_gen_set(false);
        return;
    }

    if (!s_playing) {
        /* 可能是 stop() 与本回调竞争后残留的一次触发：
         * esp_timer_stop() 不会等待正在执行的回调，所以那次回调可能已经把音开起来
         * 并重新装了定时器。这里必须把音关掉，否则会留下一个永远不停的长音。 */
        tone_gen_set(false);
        xSemaphoreGive(s_mutex);
        return;
    }

    if (s_key_on) {
        /* 元素结束 → 关音，安排间隔 */
        tone_gen_set(false);
        s_key_on = false;
        s_pat_idx++;
        if (s_pat && s_pat[s_pat_idx]) arm(s_t.intra_gap_ms);
        else { s_pat = NULL; arm(s_t.char_gap_ms); }
        xSemaphoreGive(s_mutex);
        return;
    }
    if (s_pat) {
        /* 字符内下一个元素 */
        s_key_on = true;
        tone_gen_set(true);
        arm(s_pat[s_pat_idx] == '.' ? s_t.dit_ms : s_t.dah_ms);
        xSemaphoreGive(s_mutex);
        return;
    }
    if (!next_char(&pending)) finish_locked(CW_PLAYER_EV_DONE, &pending);
    xSemaphoreGive(s_mutex);
    /* 回调必须在锁外触发：见文件顶部 pending_cb_t 的注释。 */
    fire_cb(&pending);
}

esp_err_t cw_player_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }
    const esp_timer_create_args_t a = { .callback = timer_cb, .name = "cw_player" };
    return esp_timer_create(&a, &s_timer);
}

void cw_player_set_timing(const morse_timing_t *t) { s_t = *t; }

esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user)
{
    /* 注意：不能调用 cw_player_stop()——那样会在同一个任务里对同一把非递归
     * 互斥量取两次锁，自己把自己锁死。这里直接调用无锁的 stop_locked()，
     * 全程只取一次锁。 */
    pending_cb_t pending = { .fire = false };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    stop_locked(&pending);   /* 快照上一段播放（如果有）待触发的 STOPPED */
    strncpy(s_text, text, TEXT_MAX - 1);
    s_text[TEXT_MAX - 1] = '\0';
    s_idx = 0; s_pat = NULL; s_pat_idx = 0; s_key_on = false;
    s_cb = cb; s_user = user;
    s_playing = true;
    ESP_LOGD(TAG, "play \"%s\"", s_text);
    arm(LEAD_IN_MS);
    xSemaphoreGive(s_mutex);
    /* 锁外触发：若上面确实打断了正在进行的播放，这里补发它的 STOPPED。 */
    fire_cb(&pending);
    return ESP_OK;
}

void cw_player_stop(void)
{
    pending_cb_t pending = { .fire = false };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    stop_locked(&pending);
    xSemaphoreGive(s_mutex);
    fire_cb(&pending);   /* 锁外触发 STOPPED */
}

/* 与 cw_player_stop() 同一套加锁/收尾/锁外触发流程，唯一区别是：先在锁内确认
 * "正在播放的会话确实是 user 这一个"，不是就什么都不做——不停音，不摸状态，
 * 也不触发任何回调。锁内比较、锁内收尾、锁外触发回调，三步顺序与
 * cw_player_stop()/cw_player_play() 完全一致，理由同样是 fire_cb() 必须在
 * s_mutex 释放之后才能调用，否则会在 esp_timer 任务上自锁死。 */
bool cw_player_stop_if(void *user)
{
    pending_cb_t pending = { .fire = false };
    bool stopped = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_playing && s_user == user) {
        stop_locked(&pending);
        stopped = true;
    }
    xSemaphoreGive(s_mutex);
    fire_cb(&pending);
    return stopped;
}

bool cw_player_is_playing(void) { return s_playing; }
