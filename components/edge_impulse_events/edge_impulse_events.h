#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EDGE_EVT_NONE = 0,
    EDGE_EVT_VAD_PASSED,
    EDGE_EVT_WAKE_WORD,
    EDGE_EVT_RECORD_START,
    EDGE_EVT_SEND_START,
    EDGE_EVT_SEND_OK,
    EDGE_EVT_SEND_FAIL,
    EDGE_EVT_ACTION,
    EDGE_EVT_ERROR
} edge_event_type_t;

typedef struct {
    edge_event_type_t type;
    uint32_t ts_ms;
    float confidence;
} edge_event_t;

void edge_events_init(void);
void edge_event_publish(edge_event_type_t type, float confidence);
bool edge_event_pop(edge_event_t *ev, TickType_t ticks);

#ifdef __cplusplus
}
#endif
