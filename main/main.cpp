/*
 * main.cpp  –  Eye animation demo
 *
 * ESP-IDF v5.x + arduino-esp32 component + TFT_eSPI
 *
 * This file is intentionally kept as close as possible to the original
 * Arduino .ino source.  The only structural changes are:
 *   • #include "Arduino.h" and initArduino() added
 *   • setup() + loop() wrapped inside extern "C" app_main()
 *   • PROGMEM / pgm_read_word removed (not needed on ESP32)
 *
 * Original: Gilchrist / Bodmer / intellar.ca
 */

#include "Arduino.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "image.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// ── TFT instance ──────────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

// ── UI config ───────────────────────────────────────────────────────────────
#define UI_TEST_MODE 1
#define UI_TEST_STEP_MS 5000
static const char *TAG_UI = "UI";

// ── RGB565 colour macro (unchanged from original) ─────────────────────────────
#define RGB(r, g, b) (((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3))

// ── Eye geometry constants ────────────────────────────────────────────────────
const int eye_pitch           = 50;
const int eye_radius_x_ref    = 25;
const int eye_radius_y_ref    = eye_radius_x_ref * 1.2;
const int eye_pos_x_ref       = 0;
const int eye_pos_y_ref       = 0;
const int eye_offset_happy_ref = -1;
const int eye_center_x_ref    = 95;

const uint16_t c_bgnd = RGB(0,0,0);

int eye_radius_x     = eye_radius_x_ref;
int eye_radius_y     = eye_radius_y_ref;
int eye_pos_x        = eye_pos_x_ref;
int eye_pos_y        = eye_pos_y_ref;
int eye_offset_happy = eye_offset_happy_ref;
int eye_center_x     = eye_center_x_ref;

void eye_reset() {
    eye_radius_x     = eye_radius_x_ref;
    eye_radius_y     = eye_radius_y_ref;
    eye_pos_x        = eye_pos_x_ref;
    eye_pos_y        = eye_pos_y_ref;
    eye_offset_happy = eye_offset_happy_ref;
    eye_center_x     = eye_center_x_ref;
}

// ── Thin wrapper (identical to original) ─────────────────────────────────────
void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.drawFastHLine(x, y, w, color);
}

// ── Ellipse primitives (unchanged from original) ─────────────────────────────
void drawFilledInscribedEllipse(int x_center, int y_center,
    float a0, float b0, float a1, float b1,
    uint16_t color_ellipse_0, uint16_t color_ellipse_1,
    bool fill_ellipse0,
    int second_ellipse_offset = -1,
    int happy_eye_offset = -1)
{
    float factorHE = 1.4;
    for (int y = y_center - b1; y <= y_center + b1; y++) {
        if ((y & 0x0F) == 0) {
            delay(0);
        }
        float y_ = y;
        float disc0   = (1 - (y_ - y_center)*(y_ - y_center) / (b0*b0));
        float disc1   = (1 - (y_ - y_center)*(y_ - y_center) / (b1*b1));
        float disc_he = -1;
        if (happy_eye_offset > -1) {
            disc_he = (1 - (y_ - y_center - happy_eye_offset)*(y_ - y_center - happy_eye_offset)
                           / (factorHE*b1*factorHE*b1));
        }
        if (disc1 >= 0) {
            if (disc0 <= 0) {
                float sqrt_disc = sqrt(disc1);
                float x1 = x_center + a1 * sqrt_disc;
                float x2 = x_center - a1 * sqrt_disc;
                drawFastHLine(x2, y, x1 - x2 + 1, color_ellipse_1);
                if (second_ellipse_offset > 0)
                    drawFastHLine(x2 + second_ellipse_offset, y, x1 - x2 + 1, color_ellipse_1);
            } else {
                float sqrt_disc0 = sqrt(disc0);
                float x_e0_0 = x_center - a0 * sqrt_disc0;
                float x_e0_1 = x_center + a0 * sqrt_disc0;
                float sqrt_disc1 = sqrt(disc1);
                float x_e1_0 = x_center - a1 * sqrt_disc1;
                float x_e1_1 = x_center + a1 * sqrt_disc1;

                drawFastHLine(x_e1_0, y, x_e0_0 - x_e1_0 + 1, color_ellipse_1);
                drawFastHLine(x_e0_1, y, x_e1_1 - x_e0_1 + 1, color_ellipse_1);
                if (second_ellipse_offset > 0) {
                    drawFastHLine(x_e1_0 + second_ellipse_offset, y, x_e0_0 - x_e1_0 + 1, color_ellipse_1);
                    drawFastHLine(x_e0_1 + second_ellipse_offset, y, x_e1_1 - x_e0_1 + 1, color_ellipse_1);
                }
                if (fill_ellipse0) {
                    if (disc_he <= 0) {
                        drawFastHLine(x_e0_0, y, x_e0_1 - x_e0_0 + 1, color_ellipse_0);
                        if (second_ellipse_offset > 0)
                            drawFastHLine(x_e0_0 + second_ellipse_offset, y, x_e0_1 - x_e0_0 + 1, color_ellipse_0);
                    } else {
                        float sqrt_disc_HE = sqrt(disc_he);
                        float x_eHE_0 = x_center - factorHE*a1 * sqrt_disc_HE;
                        float x_eHE_1 = x_center + factorHE*a1 * sqrt_disc_HE;
                        if (x_e0_0 <= x_eHE_0) {
                            drawFastHLine(x_e0_0,  y, x_eHE_0 - x_e0_0  + 1, color_ellipse_0);
                            drawFastHLine(x_eHE_0, y, x_eHE_1 - x_eHE_0 + 1, color_ellipse_1);
                            drawFastHLine(x_eHE_1, y, x_e0_1  - x_eHE_1 + 1, color_ellipse_0);
                            if (second_ellipse_offset > 0) {
                                drawFastHLine(x_e0_0  + second_ellipse_offset, y, x_eHE_0 - x_e0_0  + 1, color_ellipse_0);
                                drawFastHLine(x_eHE_0 + second_ellipse_offset, y, x_eHE_1 - x_eHE_0 + 1, color_ellipse_1);
                                drawFastHLine(x_eHE_1 + second_ellipse_offset, y, x_e0_1  - x_eHE_1 + 1, color_ellipse_0);
                            }
                        } else {
                            drawFastHLine(x_e0_0, y, x_e0_1 - x_e0_0 + 1, color_ellipse_1);
                            if (second_ellipse_offset > 0)
                                drawFastHLine(x_e0_0 + second_ellipse_offset, y, x_e0_1 - x_e0_0 + 1, color_ellipse_1);
                        }
                    }
                }
            }
        }
    }
}

// ── Eye drawing ───────────────────────────────────────────────────────────────
void draw_eyes(int x, int y, int offset_second_eye, int offset_happy_eye) {
   /* drawFilledInscribedEllipse(x, y, 0.9*eye_radius_x, 0.9*eye_radius_y,
        1.0*eye_radius_x, 1.2*eye_radius_y,
        RGB(1,3,86),    RGB(0,0,0),    false, offset_second_eye);
    drawFilledInscribedEllipse(x, y, 0.7*eye_radius_x, 0.7*eye_radius_y,
        0.9*eye_radius_x, 0.9*eye_radius_y,
        RGB(1,4,172),   RGB(1,3,86),   false, offset_second_eye);*/ 
    drawFilledInscribedEllipse(x, y, 0.5*eye_radius_x, 0.5*eye_radius_y,
        0.7*eye_radius_x, 0.7*eye_radius_y,
        RGB(1,178,226), RGB(0,1,4), true,  offset_second_eye, offset_happy_eye);
}

// ── Animations ────────────────────────────────────────────────────────────────
void eye_blink() {
    int cx = eye_center_x_ref;
    float ratio[] = {1.0, 0.85, 0.75, 0.55, 0.4, 0.3, 0.2, 0.1, 0.05};
    int nb_ratio = 9;
    for (int i = 0; i < nb_ratio; i++) {
        eye_radius_y = eye_radius_y_ref * ratio[i];
        draw_eyes(cx - eye_pitch, 72, eye_pitch, -1);
    }
    for (int i = 0; i < nb_ratio; i++) {
        eye_radius_y = eye_radius_y_ref * ratio[nb_ratio - i - 1];
        draw_eyes(cx - eye_pitch, 72, eye_pitch, -1);
    }
}

void eye_make_happy(bool reverse_to_normal = false) {
    float offset_temp, offset_happy_temp;
    for (eye_offset_happy = 40; eye_offset_happy >= 20; eye_offset_happy -= 2) {
        offset_temp       = reverse_to_normal ? (40 - eye_offset_happy) + 20 : eye_offset_happy;
        offset_happy_temp = offset_temp;
        offset_temp       = (40 - offset_temp) / 20 * 6;
        eye_radius_x      = eye_radius_x_ref + offset_temp;
        eye_radius_y      = eye_radius_y_ref - offset_temp;
        draw_eyes(eye_center_x_ref - eye_pitch, 72, eye_pitch, offset_happy_temp);
    }
}

void demo_draw_eyes() {
    eye_reset();
    draw_eyes(eye_center_x - eye_pitch, 72, eye_pitch, eye_offset_happy);
    delay(2000);
    eye_blink();
    eye_reset();
    delay(500);
    eye_blink();
    eye_make_happy();
    delay(500);
    eye_make_happy(true);
    delay(500);
}

// ── Image display (PROGMEM removed – ESP32 reads flash directly) ───────────────
void draw_image() {
    for (uint16_t i = 0; i < FACE2_SM_HEIGHT; i++) {
        if ((i & 0x07) == 0) {
            delay(0);
        }
        for (uint16_t j = 0; j < FACE2_SM_WIDTH; j++) {
            uint16_t v = face2_sm[i * FACE2_SM_WIDTH + j];
            tft.drawPixel(2*j,   2*i,   v);
            tft.drawPixel(2*j,   2*i+1, v);
            tft.drawPixel(2*j+1, 2*i,   v);
            tft.drawPixel(2*j+1, 2*i+1, v);
        }
    }
}

// ── UI state and display control ─────────────────────────────────────────────
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

static QueueHandle_t ui_queue = NULL;

static void ui_set_state(ui_state_t state) {
    if (!ui_queue) return;
    ui_event_t ev = { state, (uint32_t)millis() };
    xQueueOverwrite(ui_queue, &ev);
}

static void ui_draw_text(const char *text) {
    tft.fillScreen(ST7735_BLACK);
    tft.setCursor(0, 50, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.println(text);
}

static void ui_apply_state(ui_state_t state) {
    switch (state) {
        case UI_SLEEPING:
            ui_draw_text("SLEEP");
            break;
        case UI_IDLE:
            tft.fillScreen(ST7735_BLACK);
            break;
        case UI_WAKE:
            ui_draw_text("WAKE");
            break;
        case UI_LISTENING:
            ui_draw_text("LISTENING");
            break;
        case UI_THINKING:
            ui_draw_text("THINKING");
            break;
        case UI_ACTION:
            ui_draw_text("ACTION");
            break;
        case UI_ERROR:
            ui_draw_text("ERROR");
            break;
        default:
            ui_draw_text("UNKNOWN");
            break;
    }
    ui_log_metrics(state);
}

static void ui_log_fps(int fps) {
    ESP_LOGI(TAG_UI, "Idle FPS: %d", fps);
}

static void ui_log_metrics(ui_state_t state) {
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG_UI, "State=%d free_heap=%u", (int)state, (unsigned)free_heap);

#if (configUSE_TRACE_FACILITY == 1) && (configGENERATE_RUN_TIME_STATS == 1)
    char stats[512];
    stats[0] = '\0';
    vTaskGetRunTimeStats(stats);
    ESP_LOGI(TAG_UI, "Task CPU%% (since boot):\n%s", stats);
#endif
}

// ── Idle animation (lightweight, frame-based) ────────────────────────────────
static void idle_anim_reset(void) {
    eye_reset();
}

static void idle_anim_step(void) {
    static const float blink_ratio[] = {1.0f, 0.85f, 0.75f, 0.55f, 0.4f, 0.3f, 0.2f, 0.1f, 0.05f};
    static const int blink_len = (int)(sizeof(blink_ratio) / sizeof(blink_ratio[0]));
    static bool blinking = false;
    static int blink_step = 0;
    static uint32_t next_blink_ms = 0;

    uint32_t now = (uint32_t)millis();
    if (!blinking && now >= next_blink_ms) {
        blinking = true;
        blink_step = 0;
    }

    if (blinking) {
        int total = blink_len * 2 - 2;
        int idx = (blink_step < blink_len) ? blink_step : (total - blink_step);
        eye_radius_y = (int)(eye_radius_y_ref * blink_ratio[idx]);
        draw_eyes(eye_center_x_ref - eye_pitch, 72, eye_pitch, eye_offset_happy_ref);
        blink_step++;
        if (blink_step >= total) {
            blinking = false;
            eye_radius_y = eye_radius_y_ref;
            next_blink_ms = now + 2500;
        }
    } else {
        eye_radius_y = eye_radius_y_ref;
        draw_eyes(eye_center_x_ref - eye_pitch, 72, eye_pitch, eye_offset_happy_ref);
    }
}

// ── Arduino setup / loop (called from display task below) ────────────────────
void setup() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
}

void loop() {
    idle_anim_step();
}

#if UI_TEST_MODE
static void ui_test_task(void *arg) {
    (void)arg;
    const ui_state_t seq[] = {
        UI_SLEEPING, UI_IDLE, UI_WAKE, UI_LISTENING, UI_THINKING, UI_ACTION, UI_ERROR
    };
    const int seq_len = (int)(sizeof(seq) / sizeof(seq[0]));
    int idx = 0;
    while (true) {
        ui_set_state(seq[idx]);
        idx = (idx + 1) % seq_len;
        vTaskDelay(pdMS_TO_TICKS(UI_TEST_STEP_MS));
    }
}
#endif

// ── Display task (runs on Core 1) ───────────────────────────────────────────
static void display_task(void *arg) {
    (void)arg;
    setup();
    idle_anim_reset();

    ui_state_t current_state = UI_IDLE;
    ui_apply_state(current_state);

    const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / 12);
    TickType_t last_wake = xTaskGetTickCount();
    ui_event_t ev;
    uint32_t fps_last_ms = (uint32_t)millis();
    int fps_frames = 0;

    while (true) {
        if (current_state == UI_IDLE) {
            if (xQueueReceive(ui_queue, &ev, 0) == pdTRUE) {
                current_state = ev.state;
                ui_apply_state(current_state);
                continue;
            }
            loop();
            delay(0);
            fps_frames++;
            uint32_t now_ms = (uint32_t)millis();
            if (now_ms - fps_last_ms >= 1000U) {
                ui_log_fps(fps_frames);
                fps_frames = 0;
                fps_last_ms = now_ms;
            }
            vTaskDelayUntil(&last_wake, frame_ticks);
        } else {
            if (xQueueReceive(ui_queue, &ev, portMAX_DELAY) == pdTRUE) {
                current_state = ev.state;
                ui_apply_state(current_state);
                if (current_state == UI_IDLE) {
                    idle_anim_reset();
                    last_wake = xTaskGetTickCount();
                }
            }
        }
    }
}

// ── ESP-IDF entry point ───────────────────────────────────────────────────────
extern "C" void app_main() {
    // Initialise Arduino runtime (Serial, millis, SPI, etc.)
    initArduino();

    ui_queue = xQueueCreate(1, sizeof(ui_event_t));
    if (ui_queue) {
        ui_set_state(UI_IDLE);
    }

    const uint32_t display_stack = 8192;
    const UBaseType_t display_priority = 2;
    const BaseType_t display_core = 1;

    xTaskCreatePinnedToCore(display_task, "display", display_stack, NULL,
                            display_priority, NULL, display_core);

#if UI_TEST_MODE
    const uint32_t test_stack = 2048;
    const UBaseType_t test_priority = 1;
    const BaseType_t test_core = 0;
    xTaskCreatePinnedToCore(ui_test_task, "ui_test", test_stack, NULL,
                            test_priority, NULL, test_core);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
