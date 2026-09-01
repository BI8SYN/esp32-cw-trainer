#ifndef UI_SOUND_H
#define UI_SOUND_H
#include "tone_seq.h"

/*
 * 界面提示音。事件枚举直接复用 core/tone_seq.h 的 tone_seq_id_t——设计稿里本来
 * 写的是另立一个 ui_sound_t，但那会是一份逐项对应的翻译表，除了多一处要同步
 * 修改之外没有任何好处。
 *
 * 这一层只做一件事：把「当前侧音频率」这个设置项喂给纯函数 tone_seq_build()，
 * 然后交给 tone_gen。频率和时长的决策全在 core/ 里，可以在主机上测。
 */
void ui_sound(tone_seq_id_t id);

#endif
