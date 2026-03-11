#include "ui_state.h"

static QueueHandle_t ui_queue = NULL;

void ui_state_init(void) {
    if (ui_queue) return;
    ui_queue = xQueueCreate(1, sizeof(ui_event_t));
}

void ui_publish_state(ui_state_t state) {
    if (!ui_queue) return;
    ui_event_t ev = { state, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) };
    xQueueOverwrite(ui_queue, &ev);
}

bool ui_pop_state(ui_event_t *ev, TickType_t ticks) {
    if (!ui_queue || !ev) return false;
    return xQueueReceive(ui_queue, ev, ticks) == pdTRUE;
}

