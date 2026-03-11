# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [Unreleased]
### Added
- Optional UI test cycle (compile-time) and idle FPS logging.

### Changed
- Run the display loop in a dedicated task pinned to Core 1; app_main now spawns the task and idles.
- Added a UI state queue to render text-only status screens and a lightweight idle animation.

### Deprecated
- 

### Removed
- 

### Fixed
- Yield during long draw loops to reduce task starvation and WDT risk.

### Security
- 

## [0.1.0] - 2026-03-11
### Added
- Initial ESP-IDF port of the Arduino TFT_eSPI demo.

[Unreleased]: https://github.com/<org>/<repo>/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/<org>/<repo>/releases/tag/v0.1.0
