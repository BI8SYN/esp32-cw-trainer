#ifndef UI_PRACTICE_H
#define UI_PRACTICE_H
#include <stdint.h>
#include "trainer.h"
void ui_practice_open(trainer_mode_t mode);       /* 呼号 / QSO；Koch 用当前课次 */
void ui_practice_open_lesson(uint8_t lesson);     /* Koch 指定课次 */
#endif
