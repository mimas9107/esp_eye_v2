#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SLEEPING = 0,
    UI_IDLE,
    UI_WAKE,
    UI_LISTENING,
    UI_THINKING,
    UI_ACTION,
    UI_ERROR
} ui_state_t;

typedef struct {
    ui_state_t state;
    uint32_t ts_ms;
} ui_event_t;

void ui_state_init(void);
void ui_publish_state(ui_state_t state);
bool ui_pop_state(ui_event_t *ev, TickType_t ticks);

#ifdef __cplusplus
}
#endif
