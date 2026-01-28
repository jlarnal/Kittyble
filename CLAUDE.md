# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```
# Build (the pio command path on this machine)
C:\Users\arnal\.platformio\penv\Scripts\platformio.exe run

# Upload firmware (not recommended for tests)
C:\Users\arnal\.platformio\penv\Scripts\platformio.exe run --target upload

# Upload SPIFFS filesystem (web UI, no recommended for tests)
C:\Users\arnal\.platformio\penv\Scripts\platformio.exe run --target uploadfs

# Serial monitor (115200 baud)
C:\Users\arnal\.platformio\penv\Scripts\platformio.exe device monitor
```

There are no automated tests. The `src/test.cpp` file is an interactive hardware diagnostic suite enabled by `DEBUG_MENU_ENABLED`, activated via serial console at boot.

## Architecture

**KibbleT5** is an ESP32 Arduino/FreeRTOS automated pet kibble dispenser with multi-tank mixing, load cell weighing, e-paper display, and REST API control.
/clear
### Central State

All runtime state lives in the global `DeviceState` struct (`include/DeviceState.hpp`), protected by `xDeviceStateMutex` (recursive). Every task and the web server read/write through this mutex with a 2000ms timeout.

### FreeRTOS Tasks (8 concurrent)

| Task | Priority | Purpose |
|------|----------|---------|
| Feeding | 10 | Polls `DeviceState.feedCommand` every 200ms, dispatches to RecipeProcessor |
| TankManager | 11 | Scans 6 Dallas 1-Wire buses via SwiMux UART for tank hot-swap detection |
| Safety | 10 | 10Hz motor stall and bowl overfill monitoring |
| Scale | 5 | 77Hz HX711 sampling with adaptive averaging and power cycling |
| Batt&OTA | 10 | Battery ADC + ArduinoOTA polling |
| TimeKeeping | 3 | NTP sync, uptime counter |
| Display | 4 | E-paper updates (event-driven) |
| Main loop | 1 | Serial console command handler |

### Key Components

- **TankManager** (`include/TankManager.hpp`, `src/TankManager.cpp`): Manages PCA9685 I2C servo driver (channels 0-5 = tank augers, channel 6 = hopper door) and SwiMux UART interface to 6 independent 1-Wire buses for tank EEPROM (DS28E07, 128 bytes with Reed-Solomon ECC).

- **RecipeProcessor** (`include/RecipeProcessor.hpp`, `src/RecipeProcessor.cpp`): Three-phase dispensing engine (purge→close→dispense). Handles batched multi-tank proportional mixing constrained by 10mL hopper capacity. Uses weight spike detection to calibrate hopper close position.

- **HX711Scale** (`include/HX711Scale.hpp`, `src/HX711Scale.cpp`): Non-blocking async scale with state machine (SAMPLING→IDLE→SETTLING). Powers down HX711 between averages to prevent chip crashes. Provides SSE weight streaming at ~4Hz.

- **WebServer** (`include/WebServer.hpp`, `src/WebServer.cpp`): ESPAsyncWebServer with ~48 REST endpoints under `/api/`. Supports SSE on `/api/events` for real-time weight, tank changes, and status updates. Serves gzipped web UI from SPIFFS.

- **ConfigManager** (`include/ConfigManager.hpp`, `src/ConfigManager.cpp`): NVS for settings (WiFi, scale calibration, hopper PWMs, timezone). SPIFFS for recipes with triple-redundant JSON files and CRC32 integrity; on load, files are tried in priority order and corrupted copies are repaired from the first valid one.

- **SafetySystem** (`include/SafetySystem.hpp`, `src/SafetySystem.cpp`): Monitors for motor stall (no weight change >0.2g over 5s) and bowl overfill (>500g). Triggers emergency servo stop.

### Hardware Peripherals

| Peripheral | Interface | Key Pins |
|------------|-----------|----------|
| HX711 load cell | GPIO | DATA=15, CLK=14 |
| PCA9685 servos | I2C | Default SDA/SCL |
| SSD1680 e-paper (296x152) | SPI | MOSI=23, CLK=18, DC=17, CS=5, RST=16, BUSY=4 |
| CH32V003 SwiMux (6x 1-Wire) | UART2 57600 | TX=27, RX=13 |
| Battery ADC | ADC | Pin 35 (half-voltage divider) |
| Servo power gate | GPIO | Pin 33 |

### Feed Command Flow

WebServer writes `FeedCommand` to `DeviceState.feedCommand` → Feeding task polls and dispatches → `RecipeProcessor.executeRecipeFeed()` or `executeImmediateFeed()` → Sets `processed=true` when done.

Emergency stop: Any component can set `feedCommand.type = EMERGENCY_STOP`, checked throughout dispensing phases.

### Data Persistence

- **Tank EEPROMs**: 128-byte DS28E07 with RS-FEC, stores name/capacity/density/remaining per tank
- **NVS**: WiFi creds, scale cal, hopper PWMs, timezone, dispensing thresholds
- **SPIFFS**: `/recipes.json` + 2 backups, `/log.txt` (64KB rolling), gzipped web UI

## Conventions

- PCA9685 I2C result enum uses `PCA9685::I2C_Result_e::I2C_Ok` (not `I2C_OK`)
- Tank UIDs are `uint64_t` and read-only from eeprom, recipe UIDs are `uint32_t` (auto-incrementing, generated in runtime) 
- Weights are in grams internally; tank `kibbleDensity` is stored as kg/L (multiply by 1000 for g/L) in RAM (double) and g/L in eeprom (uint32_t).
- Tank `capacity` is stored in mL in EEPROM, exposed as liters in `TankInfo` (again, in order to use integers with a modicum of precision)
- Private methods and members are prefixed with `_` (e.g., `_purgeHopper()`)
- All DeviceState access must acquire `xDeviceStateMutex` first an release it once used in scope.
- Enums for API-reported states/events use integer values, not strings
- Build flag `NUMBER_OF_BUSES` defines tanks count; hopper servo is at index `NUMBER_OF_BUSES`. `NUMBER_OF_BUSES` is defined in "./platformio.ini " .
- Custom partition table `kibble_part.csv` provides dual OTA slots + 3.8MB SPIFFS
- The base ESP32 board is a modified TTGO-T5, with 8MB of flash instead of 4MB.
- The Web UI source code is segregated from this project and only relies on the FSD for its web API dependancies (README.md).
- The hopper's idle (closed) position is ~900µs. After any feeding sequence, the hopper must be returned to the closed position for sanitary reasons.
- The tanks' feedscrew servos idle position is set to 1500µs, while the hopper's closed and open positions are approximately 900µs and 1500µs respectively, while its angular displacement from horizontal (closed) to angled down (fully open) is somehow eased in and out. The angular range is from 0 to -32° respectively.

The FSD in `./README.md` must be updated each time a change is brought to the codebase of this project.
