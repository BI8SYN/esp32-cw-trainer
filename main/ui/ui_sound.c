#include "ui_sound.h"
#include "tone_gen.h"
#include "cw_settings.h"

void ui_sound(tone_seq_id_t id)
{
    tone_step_t steps[TONE_SEQ_MAX];
    int n = tone_seq_build(id, cw_settings_get()->tone_hz, steps, TONE_SEQ_MAX);
    if (n > 0) tone_gen_seq(steps, (uint8_t)n);
}
