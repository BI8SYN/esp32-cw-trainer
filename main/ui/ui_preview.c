#include "ui_preview.h"
#include "ui_practice.h"
void ui_preview_open(uint8_t lesson, bool review) { (void)lesson; (void)review; ui_practice_open(TRAINER_MODE_KOCH); }
