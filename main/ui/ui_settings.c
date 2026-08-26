#include "ui_settings.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
lv_obj_t *ui_settings_create(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "设置（桩）", back); return s; }
