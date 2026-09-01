#include "ui_sound.h"
#include "tone_gen.h"
#include "cw_settings.h"

void ui_sound(tone_seq_id_t id)
{
    /* 总开关在这一处判掉，而不是散在每个调用点：调用点只该说「发生了什么」，
       不该各自判断「现在该不该出声」。 */
    if (!cw_settings_get()->sound_on) return;

    tone_step_t steps[TONE_SEQ_MAX];
    int n = tone_seq_build(id, cw_settings_get()->tone_hz, steps, TONE_SEQ_MAX);
    if (n > 0) tone_gen_seq(steps, (uint8_t)n);
}
