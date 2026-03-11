# AGENTS.md

## Purpose
This document defines workflow control, development contracts, and collaboration rules for humans and AI agents working on this repository.

## Project Context
- Platform: ESP32
- Build system: ESP-IDF (CMake + Ninja)
- Arduino APIs are used via arduino-esp32 managed component.
- Display: ST7735 128x160 over SPI2_HOST (VSPI).

## Required Reading Order
1. README.md
2. CHANGELOG.md
3. TECH_NOTES.md
4. SPEC.md
5. This AGENTS.md

## Workflow Control
- Each change must have a clear scope and be traceable to a requirement or issue.
- Keep changes minimal and localized to the task.
- When modifying hardware configuration, use menuconfig and capture changes via sdkconfig.defaults if they are intended to be defaults.
- Do not edit sdkconfig manually unless explicitly required.

## Development Contract
- Entry point:
  - app_main() must call initArduino() before any Arduino API usage.
  - setup()/loop() are invoked explicitly from app_main().
- Build:
  - components/TFT_eSPI must only compile TFT_eSPI.cpp.
  - Do not add Extensions/*.cpp to SRCS.
- Configuration:
  - Pin mapping and driver selection live in Kconfig via menuconfig.
- Dependencies:
  - arduino-esp32 is managed by ESP-IDF Component Manager (main/idf_component.yml).
  - Avoid adding new managed components unless necessary and documented.

## Collaboration Rules
- If you touch build, config, or component structure, update SPEC.md if behavior or contracts change.
- Add or update CHANGELOG.md entries for any functional or visible behavior changes.
- Prefer ASCII in new files unless there is a strong reason to use another language.
- Keep documentation factual and short; avoid duplicate content across files.

## Testing and Verification
- Preferred commands:
  - idf.py set-target esp32
  - idf.py menuconfig
  - idf.py build
  - idf.py flash monitor
- If you cannot run tests or flash, state that clearly in your report.

## Review Checklist
- app_main() still calls initArduino() before Arduino APIs.
- CONFIG_AUTOSTART_ARDUINO remains n unless explicitly changed.
- TFT_eSPI component build does not compile Extensions/*.cpp separately.
- README/TECH_NOTES/SPEC/CHANGELOG are consistent with code changes.
