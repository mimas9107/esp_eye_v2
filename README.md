# esp_eye_v2

ESP32 眼睛動畫 Demo — Arduino TFT_eSPI 專案移植到 ESP-IDF v5.x

原始來源：Gilchrist / Bodmer / intellar.ca

---

## 硬體規格

| 項目 | 規格 |
|------|------|
| MCU | ESP32 |
| 螢幕 | ST7735 128×160 BLACKTAB（BGR） |
| MOSI | GPIO 23 |
| SCLK | GPIO 18 |
| CS | GPIO 16 |
| DC | GPIO 5 |
| RST | GPIO 17 |
| SPI | SPI2_HOST (VSPI)，27 MHz |

---

## 專案結構

```
esp_eye_v2/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       ← 宣告 arduino-esp32 managed component
│   ├── main.cpp                ← 主程式
│   └── image.h                 ← 臉部圖片陣列
└── components/
    └── TFT_eSPI/               ← 精簡版（僅保留 ST7735 + ESP32）
        ├── CMakeLists.txt
        ├── Kconfig
        ├── User_Setup.h
        ├── TFT_eSPI.cpp / .h
        ├── TFT_config.h
        ├── User_Setup_Select.h
        ├── Extensions/
        ├── Processors/         ← 僅保留 TFT_eSPI_ESP32.c/h
        └── TFT_Drivers/        ← 僅保留 ST7735_*.h
```

> `managed_components/` 和 `build/` 已加入 `.gitignore`，執行 `idf.py build` 時會自動重建。

---

## 快速開始

### 環境需求

- ESP-IDF v5.5+（已測試 v5.5.2）
- Python 3.x

### 建置與燒錄

```bash
# 1. clone 專案
git clone <repo-url> esp_eye_v2
cd esp_eye_v2

# 2. 設定目標晶片
idf.py set-target esp32

# 3. 硬體 pin 設定（首次或換板子時）
idf.py menuconfig
# 路徑：Component config → TFT_eSPI → 設定 ST7735 及各 pin 腳

# 4. 編譯
idf.py build

# 5. 燒錄並監看
idf.py flash monitor
```

---

## 移植重點：Arduino → ESP-IDF

這是本專案最核心的部分，也是最容易卡關的地方。

### 1. Build System：手寫 CMakeLists.txt

Arduino IDE 有自己的隱藏編譯流程，ESP-IDF 使用 CMake + Ninja，TFT_eSPI 原本沒有提供 `CMakeLists.txt`，必須自己寫。

**關鍵陷阱**：TFT_eSPI 的 `Extensions/` 目錄下的 `.cpp`（Button、Sprite、Smooth_font 等）**不是獨立的 compilation unit**，它們是被 `TFT_eSPI.cpp` 用 `#include` 直接拉進去的。如果用 `GLOB_RECURSE` 把所有 `.cpp` 都加進 `SRCS`，這些檔案會被編譯兩次，第二次因為沒有獨立的 header 而大量報錯。

**正確寫法**：`SRCS` 只列 `TFT_eSPI.cpp` 一個檔案：

```cmake
idf_component_register(
    SRCS "TFT_eSPI.cpp"          # 只有這一個！Extensions 由它內部 #include
    INCLUDE_DIRS "." "Extensions" "Fonts" "Processors"
    REQUIRES arduino-esp32
)

target_compile_options(__idf_TFT_eSPI PRIVATE
    -Wno-error
    -Wno-address
    -Wno-narrowing
    -Wno-deprecated-declarations
)
```

---

### 2. 硬體設定：Kconfig 取代 User_Setup.h

Arduino 版本靠 `User_Setup.h` 裡的 `#define` 來設定硬體 pin 腳，屬於「設定寫死在程式碼裡」的方式。

ESP-IDF 有自己的設定系統：**Kconfig → menuconfig → sdkconfig**。新版 TFT_eSPI 已提供 `Kconfig` 檔，因此可以直接用 `idf.py menuconfig` 設定驅動型號和 pin 腳，設定值會寫進 `sdkconfig`，編譯時自動帶入。

這比 `User_Setup.h` 更乾淨——硬體設定與程式碼完全分離，換板子只要重跑 menuconfig，不需要動任何 source。

---

### 3. 程式進入點：app_main 取代隱藏的 main()

Arduino 框架把 `main()` 藏起來，框架幫你呼叫 `setup()` 和 `loop()`。ESP-IDF 的進入點是 `app_main()`，必須是 C linkage。

```cpp
// Arduino 原本（框架自動處理）
void setup() { ... }
void loop()  { ... }

// ESP-IDF 移植後（顯示任務固定在 Core 1）
extern "C" void app_main() {
    initArduino();   // ← 這行是關鍵
    xTaskCreatePinnedToCore(display_task, "display", 8192, NULL, 2, NULL, 1);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

`initArduino()` 是 arduino-esp32 提供的函式，負責初始化 Arduino runtime 環境（GPIO、Serial、millis、SPI 等）。沒有這行，所有 Arduino API 都無法正常運作。

顯示流程由 `display_task` 負責，並固定在 Core 1，避免與未來的主流程（喚醒詞偵測/錄音傳送）互相干擾。

---

### 4. 依賴管理：idf_component.yml

arduino-esp32 透過 ESP-IDF Component Manager 引入，在 `main/idf_component.yml` 宣告：

```yaml
dependencies:
  espressif/arduino-esp32: "^3.1.0"
```

執行 `idf.py build` 時會自動下載到 `managed_components/`，不需要手動 clone。

---

### 移植對照表

| 問題 | Arduino 原本 | ESP-IDF 解法 |
|------|-------------|-------------|
| 編譯系統 | Arduino IDE 自動處理 | 手寫 CMakeLists.txt（注意 Extensions 不能獨立編譯） |
| 硬體設定 | User_Setup.h `#define` | `idf.py menuconfig` → sdkconfig |
| 程式進入點 | 框架隱藏 | `app_main()` + `initArduino()` |
| 依賴管理 | Library Manager | `idf_component.yml` 宣告 arduino-esp32 |
| PROGMEM | `pgm_read_word()` | 直接讀取（ESP32 flash 可直接存取） |

---

## TFT_eSPI 精簡紀錄

| 移除項目 | 原因 |
|---------|------|
| `Tools/` | PC 端開發工具，與編譯無關 |
| `TFT_Drivers/` 中非 ST7735 的 58 個驅動 | 只用 ST7735 |
| `Processors/` 中非 ESP32 的平台 | 只用 ESP32 |
| `.git/`、文件雜項 | 不需要 |
| **保留** `Fonts/` | TFT_eSPI.h 內部直接 `#include`，必要 |
| **保留** `User_Setups/` | menuconfig 流程參考，必要 |

原始 clone：~15MB → 精簡後：**2.8MB**，縮減約 80%。

---

## 注意事項

編譯時會出現一些來自第三方庫的 warning（`missing initializer`、`TOUCH_CS not defined` 等），這些是 arduino-esp32 和 TFT_eSPI 與 ESP-IDF v5.5 的版本差異所致，不影響功能，可以忽略。

---

## 顯示任務與 UI 狀態（低負載）

- 顯示由 `display_task` 執行並固定在 Core 1。
- UI 狀態透過 Queue 驅動，非 Idle 狀態只畫一次英文文字提示，Idle 狀態以低 FPS 跑眼睛動畫。
- Idle FPS 會輸出到序列埠（log tag: `UI`），預設為 20（`UI_IDLE_FPS`）。
- 狀態切換時會輸出 free heap；若啟用 FreeRTOS run-time stats，會一併輸出各 task CPU 佔用。
- 測試模式為編譯期開關：`UI_TEST_MODE`，開啟後會自動輪播狀態方便檢視。
- Idle 動畫會定期顯示 happy 表情，採分幀執行以保持低負載、不阻塞。
