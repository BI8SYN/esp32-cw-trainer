#ifndef UI_PREVIEW_H
#define UI_PREVIEW_H
#include <stdbool.h>
#include <stdint.h>
/* 打开第 lesson 课的课前预告；review=true（巩固练习）时跳过预告直接进练习 */
void ui_preview_open(uint8_t lesson, bool review);
#endif
