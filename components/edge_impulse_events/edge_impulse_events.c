#include "edge_impulse_events.h"

static QueueHandle_t edge_queue = NULL;

void edge_events_init(void) {
    if (edge_queue) return;
    edge_queue = xQueueCreate(1, sizeof(edge_event_t));
}

void edge_event_publish(edge_event_type_t type, float confidence) {
    if (!edge_queue) return;
    edge_event_t ev = { type, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS), confidence };
    xQueueOverwrite(edge_queue, &ev);
}

bool edge_event_pop(edge_event_t *ev, TickType_t ticks) {
    if (!edge_queue || !ev) return false;
    return xQueueReceive(edge_queue, ev, ticks) == pdTRUE;
}
