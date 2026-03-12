# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [Unreleased]
### Added
- Optional UI test cycle (compile-time) and idle FPS logging.
- Log free heap on state transitions; task CPU stats when FreeRTOS run-time stats are enabled.
- 新增 eye_ui 顯示元件（Core 1），提供 UI 狀態機與眼睛動畫。
- 新增 Edge Impulse 元件包裝（Kconfig 開關）與 websocket 依賴。
- 新增 ui_state 元件與喚醒流程狀態事件接線。
- 完成 Edge Impulse 整合並通過建置、WiFi/NTP/WebSocket 與喚醒流程驗證（2026-03-12）。
- 文件新增整合測試結果與資源使用摘要。
- 新增 edge_impulse_events 元件，改由事件映射 UI 狀態以降低耦合。
- 修正 edge_impulse_events 佇列長度為 1，符合 xQueueOverwrite 使用限制以避免重啟。

### Changed
- Run the display loop in a dedicated task pinned to Core 1; app_main now spawns the task and idles.
- Added a UI state queue to render text-only status screens and a lightweight idle animation.
- Bump idle animation target FPS to 20.
- Idle animation now includes happy expression via frame-based steps (non-blocking).
- 主程式改為僅啟動 eye_ui，Edge Impulse 與 UI 顯示已脫勾。
- UI 非 Idle 狀態加入逾時自動回復 Idle，避免 ACTION 卡住。
- 錄音改為分段串流以降低記憶體壓力並避免 DSP 初始化失敗。
- Idle FPS log 預設關閉（避免序列埠雜訊）。

### Deprecated
- 

### Removed
- 

### Fixed
- Yield during long draw loops to reduce task starvation and WDT risk.
- Fix build break from misplaced UI metrics logging call.
- Edge Impulse 錄音改為即時分段串流，避免大塊緩衝造成 OOM 與 FFT 初始化失敗。

### Security
- 

## [0.1.0] - 2026-03-11
### Added
- Initial ESP-IDF port of the Arduino TFT_eSPI demo.

[Unreleased]: https://github.com/<org>/<repo>/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/<org>/<repo>/releases/tag/v0.1.0
