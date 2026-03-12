// ============================================================
//  User_Setup.h  –  place this file inside components/TFT_eSPI/
//  (overwrite the default User_Setup.h that comes with the lib)
//
//  Hardware: ST7735  128×160  BGR  BLACKTAB
//  Pins:  MOSI=23  SCLK=18  CS=16  DC=5  RST=17  @ 27 MHz
// ============================================================

#define USER_SETUP_INFO "esp_eye_demo – ST7735 128x160"

// ── Driver ────────────────────────────────────────────────────
#define ST7735_DRIVER

// ── Screen size (portrait) ────────────────────────────────────
#define TFT_WIDTH  128
#define TFT_HEIGHT 160

// ── ST7735 tab colour (matches original sketch) ───────────────
#define ST7735_GREENTAB

// ── Colour order ──────────────────────────────────────────────
#define TFT_RGB_ORDER TFT_BGR

// ── SPI pins ──────────────────────────────────────────────────
#define TFT_MISO -1
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   16
#define TFT_DC    5
#define TFT_RST  17

// ── SPI clock ─────────────────────────────────────────────────
#define SPI_FREQUENCY     27000000
#define SPI_READ_FREQUENCY 20000000

// ── Fonts (keep same as original) ─────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
