# Arduino IDE 背後的真實結構**

Arduino 的 `main()` 長這樣（可以在 arduino-esp32 的源碼裡找到）：

```cpp
// cores/esp32/main.cpp（arduino-esp32 的真實源碼）
extern "C" void app_main() {
    initArduino();
    
#if CONFIG_AUTOSTART_ARDUINO
    // 在獨立的 FreeRTOS task 裡跑 Arduino 的 setup/loop
    xTaskCreateUniversal(loopTask, "loopTask", ...);
#endif
}

void loopTask(void *pvParameters) {
    setup();
    while (true) {
        loop();
        // 還有處理 yield、串口事件等
        if (serialEventRun) serialEventRun();
    }
}
```

## 所以 Arduino IDE 背後其實是：

```
app_main()
  └─ initArduino()
  └─ xTaskCreateUniversal(loopTask)   ← FreeRTOS task
       └─ setup()
       └─ while(true) loop()
```

---

## **我們的版本 vs Arduino IDE 的差異**

| | Arduino IDE 背後 | 我們的移植 |
|--|--|--|
| setup/loop 執行方式 | 獨立 FreeRTOS task | 由 display task（Core 1）執行 |
| `CONFIG_AUTOSTART_ARDUINO` | `y`（自動啟動） | `n`（手動控制） |
| yield / serialEvent | 有處理 | 沒有 |

我們把 `CONFIG_AUTOSTART_ARDUINO=n` 設在 `sdkconfig.defaults` 裡，就是為了**不讓 arduino-esp32 自動幫我們跑 loopTask**，改由自己的 `app_main` 手動控制流程。

---

## **為什麼要這樣做**

`AUTOSTART_ARDUINO=y` 的話，arduino-esp32 會自動去找你的 `setup()` 和 `loop()`，但你失去了對 FreeRTOS task 優先權、stack size、啟動時序的控制權。設成 `n` 之後自己包，你可以在 `setup()` 之前做任何 ESP-IDF 層的初始化，這是做複雜專案時更穩健的做法。

---

## 顯示任務與狀態機（低負載）

- 顯示由獨立 `display` task 執行並固定在 Core 1，避免影響主要流程。
- UI 狀態透過 Queue 驅動：非 Idle 狀態只畫一次文字；Idle 狀態以低 FPS 跑眼睛動畫。
- Idle FPS 會在序列埠輸出（log tag: `UI`），非 Idle 不計算 FPS 以避免額外負擔。
- 測試模式為編譯期開關（`UI_TEST_MODE`）；開啟後會自動輪播狀態方便檢視。
