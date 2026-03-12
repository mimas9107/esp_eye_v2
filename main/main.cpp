#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eye_ui.h"

extern "C" void app_main() {
    initArduino();
    eye_ui_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
