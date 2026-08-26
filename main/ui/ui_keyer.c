#include "ui_keyer.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
lv_obj_t *ui_keyer_create(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "电键（桩）", back); return s; }
