#include "ui_practice.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static lv_obj_t *build(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "练习（桩）", back); return s; }
void ui_practice_open(trainer_mode_t mode) { (void)mode; ui_nav_show(build); }
