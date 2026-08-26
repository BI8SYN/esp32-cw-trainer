#ifndef CW_PLAYER_H
#define CW_PLAYER_H
#include <stdbool.h>
#include "esp_err.h"
#include "morse_timing.h"

typedef enum { CW_PLAYER_EV_CHAR_START, CW_PLAYER_EV_DONE, CW_PLAYER_EV_STOPPED } cw_player_event_t;
typedef void (*cw_player_cb_t)(cw_player_event_t ev, char c, void *user);

esp_err_t cw_player_init(void);
void      cw_player_set_timing(const morse_timing_t *t);
esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user);
void      cw_player_stop(void);
bool      cw_player_is_playing(void);

#endif
