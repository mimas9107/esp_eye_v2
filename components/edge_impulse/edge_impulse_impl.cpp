/*
 * Edge Impulse Wake Word Detection with INMP441 I2S Microphone
 * 
 * Hardware: ESP32 DevKit V1 + INMP441
 * Model:    esp-miao-mfcc (MFCC-based, 3 classes: heymiaomiao / noise / unknown)
 * 
 * I2S Configuration:
 *   - Pins: BCK=32, WS=25, DIN=33 (Matching inmp441_recorder configuration)
 *   - Mode: Stereo Read + Software Left Channel Extraction
 *     (Required for ESP32 HW V1 compatibility with I2S std driver)
 *   - Clock: APLL enabled for accurate 16kHz sample rate
 * 
 * Inference:
 *   - Continuous sliding window (16000 samples, 4 slices)
 *   - Wake word: "heymiaomiao" (Threshold > 0.6)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "edge_impulse.h"
#include "edge_impulse_events.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h" // For NVS functions
#include "sdkconfig.h" // For Kconfig values
#include "esp_idf_version.h"
#include "cJSON.h" // Added back
#include "mbedtls/base64.h"
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_websocket_client.h"
#endif
#include "esp_http_client.h"

/* ---------- Configuration ---------- */

// Removed hardcoded WIFI_SSID and WIFI_PASS, now from NVS/Kconfig
#define SERVER_URL      "ws://192.168.1.16:8000/ws/esp32_01" // Still hardcoded for now, could also be NVS/Kconfig
#define ACK_URL         "http://192.168.1.16:8000/ack"

/* --- Time & NTP Support --- */
#define TZ_OFFSET (8 * 3600) // UTC+8

uint64_t get_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

void init_ntp() {
    ESP_LOGI("NTP", "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    // Set timezone to Taipei (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    // Wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI("NTP", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI("NTP", "Time synchronized: %d-%02d-%02d %02d:%02d:%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

/* --- ACK Sound Support --- */
void send_ack_request() {
    esp_http_client_config_t config = {};
    config.url = ACK_URL;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 800; // 快速逾時，避免錄音延遲

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE("ACK", "Failed to initialize HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("ACK", "Sent successfully, status_code=%d", (int)esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE("ACK", "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

#define LED_PIN         GPIO_NUM_2

#define I2S_BCK_GPIO    GPIO_NUM_32
#define I2S_WS_GPIO     GPIO_NUM_25
#define I2S_DIN_GPIO    GPIO_NUM_33

/* ---------- WiFi & WS Globals ---------- */

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
static esp_websocket_client_handle_t ws_client = NULL;
static bool ws_connected = false;

static const char *TAG = "ESP-MIAO";

// NVS handles
static nvs_handle_t nvs_wifi_handle;

// WiFi Credentials
static char s_wifi_ssid[32];
static char s_wifi_password[64];

/* ---------- NVS Helper Functions ---------- */

esp_err_t get_wifi_credentials_from_nvs(void) {
    esp_err_t err;
    size_t ssid_len = sizeof(s_wifi_ssid);
    size_t pass_len = sizeof(s_wifi_password);

    err = nvs_get_str(nvs_wifi_handle, "wifi_ssid", s_wifi_ssid, &ssid_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to get WiFi SSID from NVS (%s)", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_str(nvs_wifi_handle, "wifi_pass", s_wifi_password, &pass_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to get WiFi Password from NVS (%s)", esp_err_to_name(err));
        return err;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "WiFi credentials not found in NVS. Using Kconfig defaults.");
        strncpy(s_wifi_ssid, CONFIG_ESP_MIAO_WIFI_SSID, sizeof(s_wifi_ssid) - 1);
        s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
        strncpy(s_wifi_password, CONFIG_ESP_MIAO_WIFI_PASSWORD, sizeof(s_wifi_password) - 1);
        s_wifi_password[sizeof(s_wifi_password) - 1] = '\0';
        
        // Save Kconfig defaults to NVS for future use
        ESP_LOGI(TAG, "Saving Kconfig defaults to NVS.");
        nvs_set_str(nvs_wifi_handle, "wifi_ssid", s_wifi_ssid);
        nvs_set_str(nvs_wifi_handle, "wifi_pass", s_wifi_password);
        nvs_commit(nvs_wifi_handle);
    } else {
        ESP_LOGI(TAG, "WiFi credentials loaded from NVS.");
    }
    return ESP_OK;
}

esp_err_t save_wifi_credentials_to_nvs(const char *ssid, const char *password) {
    esp_err_t err;
    err = nvs_set_str(nvs_wifi_handle, "wifi_ssid", ssid);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs_wifi_handle, "wifi_pass", password);
    if (err != ESP_OK) return err;
    err = nvs_commit(nvs_wifi_handle);
    ESP_LOGI(TAG, "WiFi credentials saved to NVS.");
    return err;
}


/* ---------- WiFi Event Handler ---------- */

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "[TS: %llu] got ip:" IPSTR, get_timestamp_ms(), IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void init_wifi(void)
{
    wifi_event_group = xEventGroupCreate();

    // Open NVS for WiFi credentials
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_wifi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    // Get WiFi credentials from NVS or Kconfig
    ESP_ERROR_CHECK(get_wifi_credentials_from_nvs());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            // Use s_wifi_ssid and s_wifi_password
            .ssid = "", // Will be copied below
            .password = "", // Will be copied below
        },
    };
    strncpy((char *)wifi_config.sta.ssid, s_wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, s_wifi_password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", (char *)wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "wifi_init_sta connected.");
}

/* ---------- Hardware Control ---------- */

static void handle_server_action(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "time_sync") == 0) {
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload) {
            long seconds = (long)cJSON_GetObjectItem(payload, "seconds")->valuedouble;
            int ms = cJSON_GetObjectItem(payload, "ms")->valueint;
            
            struct timeval tv = {
                .tv_sec = seconds,
                .tv_usec = ms * 1000
            };
            settimeofday(&tv, NULL);
            
            struct tm timeinfo;
            localtime_r(&tv.tv_sec, &timeinfo);
            ESP_LOGI("NTP", "RTC synchronized via Server: %d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    } else if (strcmp(type->valuestring, "action") == 0) {
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload) {
            const char *action = cJSON_GetObjectItem(payload, "action")->valuestring;
            const char *target = cJSON_GetObjectItem(payload, "target")->valuestring;
            const char *value = cJSON_GetObjectItem(payload, "value")->valuestring;

            ESP_LOGI(TAG, "[TS: %llu] Executing Action: %s on %s -> %s", get_timestamp_ms(), action, target, value);

            int gpio_num = -1;
            // 簡單的映射邏輯 (應與 server 端的 device_table 對應)
            if (strcmp(target, "light") == 0) gpio_num = 26; // 假設繼電器在 26
            else if (strcmp(target, "fan") == 0) gpio_num = 27;   // 假設風扇在 27
            else if (strcmp(target, "led") == 0) gpio_num = LED_PIN;

            if (gpio_num != -1) {
                int level = (strcmp(value, "on") == 0) ? 1 : 0;
                gpio_set_direction((gpio_num_t)gpio_num, GPIO_MODE_OUTPUT);
                gpio_set_level((gpio_num_t)gpio_num, level);
                ESP_LOGI(TAG, "[TS: %llu] GPIO %d set to %d", get_timestamp_ms(), gpio_num, level);
            }
            edge_event_publish(EDGE_EVT_ACTION, 0.0f);
        }
    } else if (type && strcmp(type->valuestring, "play") == 0) {
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload) {
            const char *audio = cJSON_GetObjectItem(payload, "audio")->valuestring;
            ESP_LOGI(TAG, "[TS: %llu] Server requests playing audio: %s", get_timestamp_ms(), audio);
            // TODO: 若有喇叭可在此實作播放
            edge_event_publish(EDGE_EVT_ACTION, 0.0f);
        }
    }

    cJSON_Delete(root);
}

/* ---------- WebSocket Event Handler ---------- */

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[TS: %llu] WEBSOCKET_EVENT_CONNECTED", get_timestamp_ms());
            ws_connected = true;
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            ws_connected = false;
            edge_event_publish(EDGE_EVT_ERROR, 0.0f);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data->data_len > 0) {
                char *buf = (char *)malloc(data->data_len + 1);
                if (buf) {
                    memcpy(buf, data->data_ptr, data->data_len);
                    buf[data->data_len] = '\0';
                    handle_server_action(buf);
                    free(buf);
                }
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR: op_code=%d", data->op_code);
            ws_connected = false;
            edge_event_publish(EDGE_EVT_ERROR, 0.0f);
            break;
    }
}

static void init_websocket(void)
{
    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = SERVER_URL;
    ws_cfg.reconnect_timeout_ms = 5000;   // 縮短重連等待時間至 5 秒
    ws_cfg.network_timeout_ms = 5000;
    ws_cfg.ping_interval_sec = 5;         // 每 5 秒發送一次 Ping，主動探測連線狀態
    ws_cfg.disable_auto_reconnect = false;

    ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);

    esp_websocket_client_start(ws_client);
}

/* ---------- Audio Configuration ---------- */

#define SAMPLE_RATE     16000
#define I2S_PORT_NUM    I2S_NUM_0
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     256

/* ---------- VAD (FFT) 參數 ---------- */
#define FFT_SIZE            512
#define FFT_FREQ_MIN        300     // 人聲最低頻率 (Hz)
#define FFT_FREQ_MAX        3400    // 人聲最高頻率 (Hz)
#define FFT_ENERGY_THRESHOLD 25000.0f  // 閾值 (根據 2026-03-03 測試建議)
#define FFT_GAIN            1.0f    // 增益因子

#define VAD_FFT_DEBUG       0       // 設置為 1 以開啟 VAD 統計與 Debug 日誌，0 則關閉以節省資源

/* ---------- 統計數據 ---------- */
typedef struct {
    uint32_t fft_triggers;        // FFT 觸發次數
    uint32_t ml_triggered;        // ML 辨識成功次數
    float   rms_avg;              // RMS 平均值 (調試用)
    float   fft_avg;              // FFT 能量平均值
} vad_stats_t;

static vad_stats_t vad_stats = {0};

#define PRINT_STATS_INTERVAL  30  // 每多少幀打印一次統計

/* ---------- Recording Configuration ---------- */

#define RECORD_DURATION_SEC  3
#define AUDIO_SAMPLES_3S     (SAMPLE_RATE * RECORD_DURATION_SEC)
#define AUDIO_BUFFER_SIZE_3S (AUDIO_SAMPLES_3S * sizeof(int16_t))

// Set to 1 to test Binary Transfer, 0 for Base64
#define USE_BINARY_STREAM    1

/* ---------- Globals ---------- */

static i2s_chan_handle_t rx_chan = NULL;
static float ei_slice_buffer[EI_CLASSIFIER_SLICE_SIZE];

/* ---------- Dynamic Audio Buffer for Recording (3 seconds, 96KB) ---------- */

static int16_t *recording_buffer = NULL;
static size_t recording_samples = 0;
static bool recording_complete = false;

/* ---------- I2S Initialization ---------- */

static void init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;
    chan_cfg.auto_clear    = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg;
    memset(&std_cfg, 0, sizeof(std_cfg));

    std_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
    std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_APLL; 
    std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_256;
    std_cfg.clk_cfg.bclk_div       = 8;

    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.slot_mode      = I2S_SLOT_MODE_STEREO;
    std_cfg.slot_cfg.slot_mask      = I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg.ws_width       = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_pol         = false;
    std_cfg.slot_cfg.bit_shift      = true;
#if SOC_I2S_HW_VERSION_1
    std_cfg.slot_cfg.msb_right      = false;
#else
    std_cfg.slot_cfg.left_align     = true;
    std_cfg.slot_cfg.big_endian     = false;
    std_cfg.slot_cfg.bit_order_lsb  = false;
#endif

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = I2S_BCK_GPIO;
    std_cfg.gpio_cfg.ws   = I2S_WS_GPIO;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din  = I2S_DIN_GPIO;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    printf("I2S initialized: %d Hz, Stereo Mode (Left Extracted), Pins: 32/25/33\r\n", SAMPLE_RATE);
}

/* ---------- Read Audio Slice (Stereo -> Mono Left) ---------- */

static bool read_audio_slice(float *out_buffer, size_t num_mono_samples, float *out_rms)
{
    const size_t bytes_per_sample = sizeof(int32_t);
    const size_t chunk_frames = 256;
    
    int32_t i2s_buf[chunk_frames * 2];

    size_t samples_read = 0;
    float sum_sq = 0.0f;

    while (samples_read < num_mono_samples) {
        size_t frames_to_read = num_mono_samples - samples_read;
        if (frames_to_read > chunk_frames) frames_to_read = chunk_frames;

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_chan, i2s_buf,
                                         frames_to_read * 2 * bytes_per_sample,
                                         &bytes_read, 1000);
        if (ret != ESP_OK) {
            printf("ERR: I2S read failed: %d\r\n", ret);
            return false;
        }

        size_t got_frames = bytes_read / (2 * bytes_per_sample);

        for (size_t i = 0; i < got_frames; i++) {
            int32_t raw_l = i2s_buf[i * 2];
            int32_t s = raw_l >> 11;
            
            if (s > 32767)  s = 32767;
            if (s < -32768) s = -32768;
            
            out_buffer[samples_read + i] = (float)s;
            sum_sq += (float)(s * s);
        }
        samples_read += got_frames;
    }

    if (out_rms) {
        *out_rms = sqrtf(sum_sq / num_mono_samples);
    }

    return true;
}

/* ---------- Read Audio to Int16 Buffer (Stereo -> Mono Left) ---------- */

static bool read_audio_to_buffer(int16_t *out_buffer, size_t num_samples)
{
    const size_t bytes_per_sample = sizeof(int32_t);
    const size_t chunk_frames = 256;
    
    int32_t i2s_buf[chunk_frames * 2];

    size_t samples_read = 0;

    while (samples_read < num_samples) {
        size_t frames_to_read = num_samples - samples_read;
        if (frames_to_read > chunk_frames) frames_to_read = chunk_frames;

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_chan, i2s_buf,
                                         frames_to_read * 2 * bytes_per_sample,
                                         &bytes_read, 1000);
        if (ret != ESP_OK) {
            printf("ERR: I2S read failed: %d\r\n", ret);
            return false;
        }

        size_t got_frames = bytes_read / (2 * bytes_per_sample);

        for (size_t i = 0; i < got_frames; i++) {
            int32_t raw_l = i2s_buf[i * 2];
            int32_t s = raw_l >> 11;
            
            if (s > 32767)  s = 32767;
            if (s < -32768) s = -32768;
            
            out_buffer[samples_read + i] = (int16_t)s;
        }
        samples_read += got_frames;
    }

    return true;
}

/* ============================================================
 * VAD 方法: FFT 頻域能量分析
 * 計算特定頻率範圍內的能量，可過濾非人聲頻率
 * ============================================================ */

/* ============================================================
 * VAD 方法: FFT 頻域能量分析 (優化版)
 * 計算特定頻率範圍內的能量，可過濾非人聲頻率
 * ============================================================ */

// 預計算的旋轉因子查表 (針對 FFT_SIZE 512)
static float twiddle_r[FFT_SIZE / 2];
static float twiddle_i[FFT_SIZE / 2];
static bool twiddle_initialized = false;

static void init_twiddle_factors() {
    if (twiddle_initialized) return;
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float angle = -2.0f * (float)M_PI * i / FFT_SIZE;
        twiddle_r[i] = cosf(angle);
        twiddle_i[i] = sinf(angle);
    }
    twiddle_initialized = true;
}

/**
 * 優化版 FFT 計算 (基 2 DIT，查表法)
 */
static void vad_fft_compute_optimized(float *real, float *imag, int n)
{
    init_twiddle_factors();
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            float temp_r = real[i]; float temp_i = imag[i];
            real[i] = real[j]; imag[i] = imag[j];
            real[j] = temp_r; imag[j] = temp_i;
        }
        int k = n / 2;
        while (k <= j) { j -= k; k /= 2; }
        j += k;
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; k++) {
                int twiddle_idx = k * step;
                float wr = twiddle_r[twiddle_idx];
                float wi = twiddle_i[twiddle_idx];
                
                float tr = wr * real[i + k + half] - wi * imag[i + k + half];
                float ti = wr * imag[i + k + half] + wi * real[i + k + half];
                
                real[i + k + half] = real[i + k] - tr;
                imag[i + k + half] = imag[i + k] - ti;
                real[i + k] += tr;
                imag[i + k] += ti;
            }
        }
    }
}

/**
 * 優化版 VAD 判定 (移除頻繁 sqrtf)
 */
static bool vad_new_fft_detect(const float *buffer, size_t length, float *fft_energy_out)
{
    if (length < FFT_SIZE) {
        if (fft_energy_out) *fft_energy_out = 0;
        return false;
    }
    
    static float real[FFT_SIZE];
    static float imag[FFT_SIZE];
    
    // Windowing & Copy
    for (int i = 0; i < FFT_SIZE; i++) {
        float window = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1));
        real[i] = buffer[i] * window;
        imag[i] = 0.0f;
    }
    
    vad_fft_compute_optimized(real, imag, FFT_SIZE);
    
    int start_bin = (FFT_FREQ_MIN * FFT_SIZE) / SAMPLE_RATE;
    int end_bin = (FFT_FREQ_MAX * FFT_SIZE) / SAMPLE_RATE;
    
    float sum_power = 0.0f; 
    int bin_count = 0;
    
    for (int i = start_bin; i < end_bin && i < FFT_SIZE/2; i++) {
        // 使用 Magnitude Squared (Power)，省去迴圈內的 sqrtf
        sum_power += (real[i] * real[i] + imag[i] * imag[i]);
        bin_count++;
    }
    
    // 最終只計算一次平方根
    float band_rms = (bin_count > 0) ? sqrtf(sum_power / bin_count) : 0.0f;
    
    if (fft_energy_out) {
        *fft_energy_out = band_rms;
    }
    
    return (band_rms > FFT_ENERGY_THRESHOLD);
}

/**
 * VAD 偵測接口 (含統計顯示)
 * @param buffer 音訊資料
 * @param length 樣本數量
 * @param rms_val 輸出 RMS 值 (可為 NULL)
 * @param fft_energy 輸出 FFT 能量值 (可為 NULL)
 * @return true = 偵測到語音, false = 安靜
 */
static bool vad_detect(const float *buffer, size_t length, 
                       float *rms_val, float *fft_energy)
{
    // 計算 RMS (僅用於調試顯示與基準比較)
    if (rms_val) {
        float sum_sq = 0.0f;
        for (size_t i = 0; i < length; i++) {
            sum_sq += buffer[i] * buffer[i];
        }
        *rms_val = sqrtf(sum_sq / length);
    }
    
    // FFT 偵測
    bool fft_result = vad_new_fft_detect(buffer, length, fft_energy);
    
    // 統計數據更新
    static uint32_t frame_count = 0;
    frame_count++;
    
    if (fft_result) vad_stats.fft_triggers++;
    
    // 更新平均值 (滑動平均)
    if (fft_energy) vad_stats.fft_avg = vad_stats.fft_avg * 0.99f + (*fft_energy) * 0.01f;
    if (rms_val) vad_stats.rms_avg = vad_stats.rms_avg * 0.99f + (*rms_val) * 0.01f;
    
#if VAD_FFT_DEBUG
    // 定期打印統計數據
    if (frame_count % PRINT_STATS_INTERVAL == 0) {
        printf("\r\n[========== VAD 統計 (Frame: %lu) ==========]\n", (unsigned long)frame_count);
        printf("  FFT 觸發次數:             %lu\n", (unsigned long)vad_stats.fft_triggers);
        printf("  ML 辨識成功次數:          %lu\n", (unsigned long)vad_stats.ml_triggered);
        printf("  RMS 平均值:               %.2f\n", vad_stats.rms_avg);
        printf("  FFT 能量平均值:           %.2f (閾值: %.0f)\n", vad_stats.fft_avg, FFT_ENERGY_THRESHOLD);
        printf("  [========================================]\n\n");
    }
#endif
    
    return fft_result;
}

/* ---------- Send Audio Stream via WebSocket (Base64 Chunked) ---------- */

static bool send_audio_stream_b64(const int16_t *audio_data, size_t sample_count, float confidence)
{
    if (!ws_client || !ws_connected || !esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGE(TAG, "WebSocket not connected or stale");
        ws_connected = false;
        return false;
    }

    // 1. Send audio_start
    char start_json[256];
    snprintf(start_json, sizeof(start_json), 
             "{\"device_id\":\"esp32_01\",\"timestamp\":%llu,\"type\":\"audio_start\",\"payload\":{\"total_samples\":%zu,\"confidence\":%.3f,\"transfer_mode\":\"base64\"}}",
             get_timestamp_ms(), sample_count, confidence);
    
    if (esp_websocket_client_send_text(ws_client, start_json, strlen(start_json), portMAX_DELAY) < 0) {
        ws_connected = false;
        return false;
    }

    // 2. Send chunks
    const size_t CHUNK_SAMPLES = 2048; // 4096 bytes per raw chunk
    size_t samples_sent = 0;
    int chunk_idx = 0;

    // Pre-calculate b64 size for the chunk
    size_t raw_chunk_size = CHUNK_SAMPLES * sizeof(int16_t);
    size_t b64_max_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_max_len, NULL, raw_chunk_size);
    
    char *b64_buffer = (char *)malloc(b64_max_len + 1);
    char *json_buffer = (char *)malloc(b64_max_len + 512);

    if (!b64_buffer || !json_buffer) {
        ESP_LOGE(TAG, "Failed to allocate streaming buffers");
        if (b64_buffer) free(b64_buffer);
        if (json_buffer) free(json_buffer);
        return false;
    }

    while (samples_sent < sample_count) {
        size_t to_send = sample_count - samples_sent;
        if (to_send > CHUNK_SAMPLES) to_send = CHUNK_SAMPLES;

        size_t b64_out_len = 0;
        mbedtls_base64_encode((unsigned char *)b64_buffer, b64_max_len, &b64_out_len, 
                              (const unsigned char *)(audio_data + samples_sent), 
                              to_send * sizeof(int16_t));
        b64_buffer[b64_out_len] = '\0';

        bool is_last = (samples_sent + to_send >= sample_count);
        int written = snprintf(json_buffer, b64_max_len + 512,
                               "{\"device_id\":\"esp32_01\",\"timestamp\":%llu,\"type\":\"audio_chunk\",\"payload\":{\"chunk_index\":%d,\"is_last\":%s,\"data_base64\":\"%s\"}}",
                               get_timestamp_ms(), chunk_idx, is_last ? "true" : "false", b64_buffer);

        if (esp_websocket_client_send_text(ws_client, json_buffer, written, portMAX_DELAY) < 0) {
            ESP_LOGE(TAG, "Failed to send chunk %d", chunk_idx);
            ws_connected = false;
            break;
        }

        samples_sent += to_send;
        chunk_idx++;
        vTaskDelay(pdMS_TO_TICKS(10)); // Prevent flooding
    }

    free(b64_buffer);
    free(json_buffer);
    
    return (samples_sent >= sample_count);
}

/* ---------- Send Audio Stream via WebSocket (Binary Chunked) ---------- */

static bool send_audio_stream_binary(const int16_t *audio_data, size_t sample_count, float confidence)
{
    if (!ws_client || !ws_connected || !esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGE(TAG, "WebSocket not connected or stale");
        ws_connected = false; // 強制重置旗標
        return false;
    }

    // 1. Send audio_start (JSON)
    char start_json[256];
    snprintf(start_json, sizeof(start_json), 
             "{\"device_id\":\"esp32_01\",\"timestamp\":%llu,\"type\":\"audio_start\",\"payload\":{\"total_samples\":%zu,\"confidence\":%.3f,\"transfer_mode\":\"binary\"}}",
             get_timestamp_ms(), sample_count, confidence);
    
    if (esp_websocket_client_send_text(ws_client, start_json, strlen(start_json), portMAX_DELAY) < 0) {
        ws_connected = false; // 發送失敗，標記為斷線
        return false;
    }

    // 2. Send raw binary data in chunks
    const size_t CHUNK_SAMPLES = 4096; // 8192 bytes per binary frame
    size_t samples_sent = 0;

    while (samples_sent < sample_count) {
        size_t to_send = sample_count - samples_sent;
        if (to_send > CHUNK_SAMPLES) to_send = CHUNK_SAMPLES;

        if (esp_websocket_client_send_bin(ws_client, (const char *)(audio_data + samples_sent), 
                                         to_send * sizeof(int16_t), portMAX_DELAY) < 0) {
            ESP_LOGE(TAG, "Failed to send binary chunk");
            ws_connected = false; // 發送失敗，標記為斷線
            break;
        }

        samples_sent += to_send;
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }

    return (samples_sent >= sample_count);
}

/* ---------- Edge Impulse Signal Callback ---------- */

static int ei_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    memcpy(out_ptr, ei_slice_buffer + offset, length * sizeof(float));
    return 0;
}

/* ---------- LED Control ---------- */

static void setup_led(void)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_rom_gpio_pad_select_gpio(LED_PIN);
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
    gpio_pad_select_gpio(LED_PIN);
#endif
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);
}

/* ---------- Inference Task ---------- */

static void inference_task(void *arg)
{
    printf("\r\n=== Edge Impulse Wake Word Detection ===\r\n");
    printf("Project : %s  (ID %d)\r\n", EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_PROJECT_ID);
    printf("Window  : %d ms, Slices: %d\r\n", 
           (int)(EI_CLASSIFIER_RAW_SAMPLE_COUNT * 1000 / EI_CLASSIFIER_FREQUENCY),
           EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
    printf("Threshold: %.2f\r\n", (float)EI_CLASSIFIER_THRESHOLD);
    printf("VAD: FFT Energy Threshold = %.0f\r\n\r\n", FFT_ENERGY_THRESHOLD);

    run_classifier_init();

    printf("Warming up microphone...\r\n");
    for (int i = 0; i < 8; i++) {
        read_audio_slice(ei_slice_buffer, EI_CLASSIFIER_SLICE_SIZE, NULL);
    }
    printf("Started.\r\n");

    ei_impulse_result_t result = {};

    bool prev_vad = false;
    while (1) {
        float current_rms = 0.0f;
        float current_fft_energy = 0.0f;

        if (!read_audio_slice(ei_slice_buffer, EI_CLASSIFIER_SLICE_SIZE, NULL)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 調用統一 VAD 偵測接口 (FFT 優先)
        bool vad_passed = vad_detect(ei_slice_buffer, EI_CLASSIFIER_SLICE_SIZE, 
                                     &current_rms, &current_fft_energy);
        if (vad_passed && !prev_vad) {
            edge_event_publish(EDGE_EVT_VAD_PASSED, 0.0f);
        }
        prev_vad = vad_passed;

        signal_t signal;
        signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
        signal.get_data     = &ei_audio_signal_get_data;

        EI_IMPULSE_ERROR res = run_classifier_continuous(&signal, &result, false);
        if (res != EI_IMPULSE_OK) {
            printf("ERR: Inference failed (%d)\r\n", res);
            continue;
        }

        bool wake_word_detected = false;
        float detected_confidence = 0.0f;
        
        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            if (strcmp(ei_classifier_inferencing_categories[i], "heymiaomiao") == 0) {
                detected_confidence = result.classification[i].value;
                
#if VAD_FFT_DEBUG
                // Debug log: 顯示候選喚醒詞的 VAD 數據
                if (detected_confidence > 0.3) {
                    printf("[DEBUG] Conf: %.3f, RMS: %.2f, FFT: %.2f(>%.0f?), Pass: %s\n", 
                           detected_confidence, 
                           current_rms,
                           current_fft_energy, FFT_ENERGY_THRESHOLD,
                           vad_passed ? "YES" : "NO");
                }
#endif
                
                // 使用 FFT VAD 判定結果 + ML 信心度
                if (detected_confidence >= EI_CLASSIFIER_THRESHOLD && vad_passed) {
                    wake_word_detected = true;
                    vad_stats.ml_triggered++;  // 記錄 ML 成功觸發次數
                }
            }
        }

        if (wake_word_detected) {
            printf("\r\n>>> 🐱 WAKE WORD DETECTED! (Conf: %.3f) 🐱 <<<\r\n\r\n", detected_confidence);
            edge_event_publish(EDGE_EVT_WAKE_WORD, detected_confidence);
            
            // --- WebSocket 緊急重連機制 (v0.4.5) ---
            if (!ws_connected || !esp_websocket_client_is_connected(ws_client)) {
                ESP_LOGW(TAG, "Wake word detected but WS is DOWN. Attempting emergency reconnect...");
                esp_websocket_client_stop(ws_client);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_websocket_client_start(ws_client);
                
                // 等待最多 2 秒試圖連上
                for (int i = 0; i < 20 && (!ws_connected || !esp_websocket_client_is_connected(ws_client)); i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                
                if (ws_connected && esp_websocket_client_is_connected(ws_client)) {
                    ESP_LOGI(TAG, "[TS: %llu] Emergency reconnect SUCCESS.", get_timestamp_ms());
                } else {
                    ESP_LOGE(TAG, "Emergency reconnect FAILED.");
                }
            }
            // -------------------------------------

            // --- 發送喚醒提示音請求 (v0.5.0) ---
            send_ack_request();

            for (int k = 0; k < 3; k++) {
                gpio_set_level(LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            printf(">>> REC: Starting 3-second recording...\r\n");
            edge_event_publish(EDGE_EVT_RECORD_START, detected_confidence);
            
            // Allocate memory dynamically
            recording_buffer = (int16_t *)malloc(AUDIO_BUFFER_SIZE_3S);
            if (recording_buffer == NULL) {
                printf(">>> REC: Failed to allocate memory (OOM)!\r\n");
                continue;
            }

            recording_samples = 0;
            recording_complete = false;
            
            bool success = read_audio_to_buffer(recording_buffer, AUDIO_SAMPLES_3S);
            
            if (success) {
                recording_samples = AUDIO_SAMPLES_3S;
                recording_complete = true;
                printf(">>> REC: Completed. Samples: %zu\r\n", recording_samples);
            } else {
                printf(">>> REC: Failed during I2S read!\r\n");
            }
            
            vTaskDelay(pdMS_TO_TICKS(100));
            
            if (recording_complete && recording_samples > 0) {
                uint64_t ts = get_timestamp_ms();
                printf(">>> WAV: Sending %zu samples via WebSocket (%s) at TS: %llu...\r\n", 
                       recording_samples, USE_BINARY_STREAM ? "Binary" : "Base64 Streaming", ts);
                
                edge_event_publish(EDGE_EVT_SEND_START, detected_confidence);
                bool sent = false;
                if (USE_BINARY_STREAM) {
                    sent = send_audio_stream_binary(recording_buffer, recording_samples, detected_confidence);
                } else {
                    sent = send_audio_stream_b64(recording_buffer, recording_samples, detected_confidence);
                }
                
                if (sent) {
                    printf(">>> WAV: Sent successfully!\r\n");
                } else {
                    printf(">>> WAV: Send failed!\r\n");
                    edge_event_publish(EDGE_EVT_SEND_FAIL, detected_confidence);
                }
            }
            
            // FREE memory after sending
            free(recording_buffer);
            recording_buffer = NULL;
            
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void edge_impulse_start(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    setup_led();
    init_wifi();
    init_ntp(); // 確保 WiFi 連線後同步時間
    init_websocket();
    init_i2s();
    
    xTaskCreate(inference_task, "ei_infer", 16384, NULL, 5, NULL);
}
