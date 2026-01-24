# Kittyble Functional Specification

**Version:** 1.1.0-stable
**Platform:** ESP32
**Document Date:** January 2026

---

## 1. Overview

Kittyble is an IoT-enabled automated pet kibble dispenser system built on the ESP32 microcontroller. It dispenses multiple types of food from separate tank containers using precise weight control, with remote monitoring and control via WiFi and REST APIs.

### 1.1 Key Capabilities

- Multi-tank food storage with individual tank identification
- Precise gram-level weight measurement for portion control
- Recipe-based feeding with multi-ingredient support
- Remote control and monitoring via REST API
- E-paper display for local status feedback
- Over-the-air firmware updates
- Safety systems for motor stall and overfill detection

---

## 2. Hardware Architecture

### 2.1 Core Components

| Component | Description | Interface |
|-----------|-------------|-----------|
| ESP32 | Main microcontroller | - |
| HX711 | Load cell amplifier for weight measurement | GPIO 15 (data), GPIO 14 (clock) |
| PCA9685 | 16-channel PWM servo driver | I2C |
| SSD1680 | 2.6" E-paper display (296x152 pixels) | SPI |
| CH32V003 (SwiMux) | 1-Wire bus multiplexer (legacy name) | UART2 (57600 baud) |
| DS28E07 / DS2431+ | 1Kb (128-byte) EEPROM per tank | Dallas 1-Wire via SwiMux |

### 2.2 Pin Assignments

| Function | GPIO Pin |
|----------|----------|
| HX711 Data | 15 |
| HX711 Clock | 14 |
| Servo Power Enable | 33 |
| Battery Voltage (ADC) | 35 |
| Buzzer/Tweeter | 25 |
| SwiMux TX | 27 |
| SwiMux RX | 13 |
| E-Paper MOSI | 23 |
| E-Paper CLK | 18 |
| E-Paper DC | 17 |
| E-Paper CS | 5 |
| E-Paper RST | 16 |
| E-Paper BUSY | 4 |

### 2.3 Servo Configuration

- **Hopper Servo (Channel 0):** Continuous rotation servo
  - Open position: ~2000 PWM
  - Closed position: ~1000 PWM
  - Stop/Idle: ~1500 PWM
- **Tank Servos (Channels 1-15):** Individual dispenser motors per tank

---

## 3. Tank System

### 3.1 Tank Identification

Each tank contains a Dallas 1-Wire EEPROM (DS28E07 or DS2431+) accessible via the SwiMux multiplexer. The SwiMux handles all 1-Wire protocol addressing, presenting each tank as a simple 1Kb (128-byte) random-access memory to Kittyble. The system supports up to 6 independent 1-Wire buses.

> **Note:** "SwiMux" is a legacy name from when the system used SWI (Single Wire Interface) EEPROMs. The current implementation uses Dallas 1-Wire protocol.

### 3.2 Tank Data Structure

Each tank's Dallas 1-Wire EEPROM provides a 64-bit UID from its ROM (read-only, factory-programmed). The EEPROM data area stores:

| Field | Description | Format |
|-------|-------------|--------|
| Name | User-defined tank name | Up to 80 characters |
| Capacity | Tank capacity | uint16_t (milliliters) |
| Kibble Density | Food density | uint16_t (grams per liter) |
| Servo Idle PWM | Calibrated stop position | 16-bit integer |
| Remaining Weight | Estimated remaining food | Grams |
| Last Base MAC | Last connected device MAC | 6 bytes |
| Bus Index | Last known bus position | 8-bit integer |
| ECC | Reed-Solomon error correction | 32 bytes |

### 3.3 Tank Detection

The TankManager continuously scans all 6 buses for connected tanks. Detection occurs:
- At system boot
- Every 1 second during normal operation (3 seconds after a change is detected)
- On-demand via API request

---

## 4. Weight Measurement

### 4.1 Scale Specifications

- **Sensor:** HX711 load cell amplifier
- **Sampling Rate:** ~75 Hz (fast mode)
- **Resolution:** Gram-level precision
- **Averaging:** Fixed 10-sample calibration, adaptive averaging during operation

### 4.2 Calibration

The scale supports two-point calibration:
1. **Tare:** Zero the scale with empty bowl
2. **Calibrate:** Set scale factor using known weight

Calibration values (factor and zero offset) are persisted in NVS flash.

### 4.3 Weight Stability

The system tracks weight stability to ensure accurate readings during dispensing. A reading is considered stable when consecutive samples vary by less than the configured threshold.

---

## 5. Recipe System

### 5.1 Recipe Structure

```json
{
  "uid": 1,
  "name": "string",
  "ingredients": [
    {
      "tankUid": "string",
      "percentage": 60
    },
    {
      "tankUid": "string",
      "percentage": 40
    }
  ],
  "dailyWeight": 200,
  "servings": 2,
  "created": "timestamp",
  "lastUsed": "timestamp"
}
```

> **Note:** Recipe `uid` is a `uint32_t` unique identifier that auto-increments when new recipes are created. The UID is never reused and provides stable identification across recipe operations.

### 5.2 Recipe Execution

1. Recipe is validated (all tanks present, percentages sum to 100)
2. Total portion weight calculated: `dailyWeight / servings`
3. Per-tank weights calculated based on percentages
4. Hopper opens
5. Each tank dispenses its portion sequentially
6. Weight monitored continuously with feedback loop
7. Hopper closes when complete
8. Feeding history logged

### 5.3 Immediate Feeding

Single-tank dispensing without a recipe:
- Specify tank UID and target weight
- Direct dispensing with weight monitoring

---

## 6. Safety Systems

### 6.1 Motor Stall Detection

- Monitors weight changes during active feeding
- Triggers if no weight change > 0.2g detected over 5-second window
- Immediately stops all servos on detection

### 6.2 Bowl Overfill Protection

- Weight threshold: 500g
- Halts dispensing if exceeded
- Prevents food waste and spillage

### 6.3 Emergency Stop

- Available via API endpoint
- Immediately halts all servo operations
- Sets `safetyModeEngaged` flag
- Requires explicit clear before resuming operations

### 6.4 Safety Task

Dedicated FreeRTOS task runs at 10 Hz (priority 10) performing continuous safety monitoring.

---

## 7. Connectivity

### 7.1 WiFi Modes

**Station Mode (STA)**
- Connects to configured home network
- Credentials stored in NVS
- Auto-reconnect on connection loss

**Access Point Mode (AP)**
- Activated when no WiFi credentials or connection fails
- Captive portal for WiFi configuration
- Displays QR code on E-paper for easy setup

### 7.2 Network Services

| Service | Port | Description |
|---------|------|-------------|
| HTTP REST API | 80 | Primary control interface |
| Server-Sent Events | 80 | Real-time push notifications (`/api/events`) |
| mDNS | 5353 | Device discovery (kibblet5.local) |
| OTA Updates | 3232 | ArduinoOTA protocol |
| NTP | 123 | Time synchronization |

---

## 8. REST API Reference

### 8.1 System Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | Device operational status |
| GET | `/api/system/info` | System info (uptime, version, build) |
| POST | `/api/system/reboot` | Reboot device |
| POST | `/api/system/factory-reset` | Factory reset |
| POST | `/api/system/time` | Set system time |

### 8.2 Settings Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/settings` | Get all settings |
| PUT | `/api/settings` | Update settings |
| GET | `/api/settings/export` | Export settings as JSON |

### 8.3 Tank Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/tanks` | List all connected tanks |
| PUT | `/api/tanks/{uid}` | Update tank info (name, density, capacity) |
| GET | `/api/tanks/{uid}/history` | Tank consumption history |

### 8.4 Scale Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/scale/current` | Current weight and status |
| POST | `/api/scale/tare` | Tare the scale |
| POST | `/api/scale/calibrate` | Calibrate with known weight |

### 8.5 Feeding Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/feed/immediate/{uid}` | Dispense specific weight from tank |
| POST | `/api/feed/recipe/{uid}` | Execute recipe (optional servings param) |
| GET | `/api/feeding/history` | Feeding history with timestamps |
| POST | `/api/feeding/stop` | Emergency stop |

### 8.6 Recipe Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/recipes` | List all recipes |
| POST | `/api/recipes` | Create new recipe |
| PUT | `/api/recipes/{uid}` | Update recipe |
| DELETE | `/api/recipes/{uid}` | Delete recipe |

### 8.7 Diagnostics & Logs Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/diagnostics/sensors` | Sensor status |
| GET | `/api/diagnostics/servos` | Servo diagnostics |
| GET | `/api/network/info` | WiFi/network info |
| GET | `/api/logs/system` | System logs from SPIFFS |
| GET | `/api/logs/feeding` | Feeding operation logs |

### 8.8 OTA Update Endpoint

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/update` | HTTP OTA firmware upload |

### 8.9 Server-Sent Events (SSE) Endpoint

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/events` | SSE stream for real-time notifications |

The SSE endpoint provides a persistent HTTP connection for server-initiated push notifications. Clients subscribe by opening an `EventSource` connection.

#### Event Types

| Event | Trigger | Payload | Status |
|-------|---------|---------|--------|
| `tanks_changed` | Tank population changes (connect/disconnect) | `{}` | ✓ Implemented |
| `weight` | Scale weight update (~4 Hz) | `{weight: number, raw: number}` | ✓ Implemented |
| `status_changed` | System state transition | `{state: string}` | Planned |
| `feeding_progress` | Weight update during feeding | `{weight: number, target: number}` | Planned |
| `feeding_complete` | Feeding operation finished | `{success: boolean, dispensed: number}` | Planned |
| `error` | Error condition detected | `{code: string, message: string}` | Planned |

#### Message Format

```
event: tanks_changed
data: {}

event: weight
data: {"weight": 123.45, "raw": 12345678}

event: status_changed
data: {"state": "FEEDING"}

event: feeding_progress
data: {"weight": 45.2, "target": 100}
```

#### Implementation Notes

- Uses `AsyncEventSource` from ESPAsyncWebServer library
- Heartbeat sent every 30 seconds to maintain connection
- Clients should implement reconnection on disconnect
- Maximum concurrent connections: 4 (ESP32 memory constraint)

---

## 9. Display System

### 9.1 E-Paper Display

- **Size:** 2.6 inches
- **Resolution:** 296 x 152 pixels
- **Controller:** SSD1680
- **Orientation:** Portrait

### 9.2 Display Modes

| Mode | Description |
|------|-------------|
| Boot Screen | Shown during startup |
| WiFi Setup | QR code with AP SSID for configuration |
| Status Display | Real-time operational status |
| Error Display | Error messages and alerts |
| System Info | Version and diagnostic information |

### 9.3 Update Behavior

Display updates are handled by a dedicated task (priority 4) to prevent blocking main operations. E-paper refresh is optimized with custom lookup tables for faster updates.

---

## 10. Power Management

### 10.1 Battery Monitoring

- **ADC Input:** GPIO 35 (half-voltage divider)
- **Voltage Range:** 3300-4200 mV (Li-ion)
- **Sampling:** Every 500ms
- **Averaging:** Rolling window (10 samples)
- **Mapping:** Sigmoidal function for realistic percentage

### 10.2 Power Control

- Servo power is gated via GPIO 33
- Servos powered only during active dispensing
- Reduces standby power consumption

---

## 11. Data Persistence

### 11.1 NVS Flash Storage

| Data | Description |
|------|-------------|
| WiFi Credentials | SSID and password |
| Scale Calibration | Factor and zero offset |
| Hopper Calibration | Open/close PWM values |
| Device Settings | Operational parameters |
| Timezone | Time zone preference |

### 11.2 SPIFFS Storage

| File | Description |
|------|-------------|
| `/log.txt` | Rolling system log (max 64KB) |
| `/recipes.json` | Primary recipe storage (JSON with CRC32) |
| `/recipes.bak1.json` | Backup copy 1 |
| `/recipes.bak2.json` | Backup copy 2 |

**Recipe File Redundancy:** Recipes are stored with triple redundancy to protect against flash memory errors. All three files contain identical content with a CRC32 checksum for integrity validation. On load, the system tries each file in order and automatically repairs corrupted files from valid backups.

### 11.3 Tank EEPROM

Each tank's EEPROM stores its own metadata with Reed-Solomon error correction for data integrity.

---

## 12. Multitasking Architecture

### 12.1 FreeRTOS Tasks

| Task | Priority | Stack (bytes) | Purpose |
|------|----------|---------------|---------|
| Feeding | 10 | 4096 | Executes feed commands |
| Battery Monitor | 10 | 3192 | Voltage monitoring, OTA |
| Scale | 5 | 4096 | Load cell sampling |
| TankManager | 11 | 5120 | Tank detection and control |
| Safety | 10 | 4096 | Motor stall/overfill detection |
| TimeKeeping | 3 | 4096 | NTP sync, time updates |
| Display | 4 | 4096 | E-paper updates |
| Main Loop | 1 | - | Serial console handler |

### 12.2 Synchronization

- **DeviceState Mutex:** Protects global state (recursive)
- **Scale Mutex:** Protects HX711 hardware access
- **SwiMux Mutex:** Protects UART bus to multiplexer
- **Command Queue:** FeedCommand structure in DeviceState

---

## 13. Device States

### 13.1 Operational States (`DeviceOperationState_e`)

| Value | State | Description |
|-------|-------|-------------|
| 0 | DOPSTATE_IDLE | Ready for commands |
| 1 | DOPSTATE_FEEDING | Actively dispensing food |
| 2 | DOPSTATE_ERROR | Fault condition detected |
| 3 | DOPSTATE_CALIBRATING | Scale calibration in progress |

### 13.2 Device Events (`DeviceEvent_e`)

| Value | Event | Description |
|-------|-------|-------------|
| 0 | DEVEVENT_NONE | No event |
| 1 | DEVEVENT_NO_TANK_SPECIFIED | No tank specified for feed |
| 2 | DEVEVENT_RECIPE_NOT_FOUND | Recipe not found |
| 3 | DEVEVENT_INVALID_RECIPE_SERVINGS | Invalid recipe: servings is zero |
| 4 | DEVEVENT_USER_STOPPED | Feeding stopped by user |
| 5 | DEVEVENT_TANK_NOT_FOUND | Tank not found during dispensing |
| 6 | DEVEVENT_DISPENSE_TIMEOUT | Dispense operation timed out |
| 7 | DEVEVENT_MOTOR_STALL | SAFETY: Motor stall detected |
| 8 | DEVEVENT_BOWL_OVERFILL | SAFETY: Bowl overfill detected |
| 9 | DEVEVENT_TANK_EMPTY | Tank is empty |

### 13.3 State Transitions

```
IDLE -> FEEDING      (on feed command)
FEEDING -> IDLE      (on feed complete)
FEEDING -> ERROR     (on safety trigger)
ERROR -> IDLE        (on error clear)
IDLE -> CALIBRATING  (on calibrate command)
CALIBRATING -> IDLE  (on calibration complete)
```

---

## 14. Time Management

### 14.1 NTP Synchronization

- **Primary Server:** pool.ntp.org
- **Secondary Server:** time.nist.gov
- **Sync Interval:** Hourly
- **Default Timezone:** Europe/Paris

### 14.2 Time Usage

- Feeding history timestamps
- Recipe last-used tracking
- System uptime calculation
- Log entry timestamps

---

## 15. Logging System

### 15.1 Log Destinations

1. **SPIFFS File:** `/log.txt` with rolling buffer (max 64KB)
2. **Serial Console:** Fallback when SPIFFS unavailable

### 15.2 Log Levels

| Level | Description |
|-------|-------------|
| ERROR | Critical failures |
| WARN | Potential issues |
| INFO | Operational events |
| DEBUG | Detailed diagnostics |

### 15.3 Serial Console Commands

| Key | Action |
|-----|--------|
| `s` | Dump current device state |
| `m` | Show mutex holder information |

---

## 16. Error Handling

### 16.1 Hardware Errors

- **HX711 Unresponsive:** Timeout after configurable period, logs error
- **Tank EEPROM Corruption:** Reed-Solomon ECC attempts recovery
- **SwiMux Communication Failure:** Logged, tank marked unavailable

### 16.2 Operational Errors

- **Feeding Timeout:** Stops if target weight not reached within limit
- **Tank Not Found:** Recipe execution fails gracefully
- **WiFi Disconnection:** Automatic reconnection attempts

### 16.3 Error Recovery

- Safety mode can be cleared via API
- Factory reset available for unrecoverable states
- Watchdog timer prevents system lockups

---

## 17. Communication Protocols

### 17.1 Protocol Summary

| Protocol | Purpose | Details |
|----------|---------|---------|
| WiFi 802.11 b/g/n | Network connectivity | STA and AP modes |
| HTTP/REST | API communication | Port 80, JSON format |
| I2C | Servo driver | PCA9685 |
| SPI | E-paper display | SSD1680 |
| UART (SLIP) | SwiMux communication | 57600 baud |
| Dallas 1-Wire | Tank EEPROM access | Via SwiMux |

---

## 18. Build Configuration

### 18.1 Platform

- **Framework:** Arduino
- **Board:** esp32dev
- **Flash Size:** 8MB @ 80MHz
- **Filesystem:** SPIFFS with custom partition

### 18.2 Partition Layout

Custom partition table (`kibble_part.csv`) with OTA support:

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| nvs | data | 0x9000 | 20KB | Non-volatile storage |
| otadata | data | 0xe000 | 8KB | OTA partition tracking |
| app0 | ota_0 | 0x10000 | 2MB | Application slot 0 |
| app1 | ota_1 | 0x210000 | 2MB | Application slot 1 |
| spiffs | data | 0x410000 | 3.875MB | Filesystem storage |
| coredump | data | 0x7F0000 | 64KB | Crash dump storage |

### 18.3 Key Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoJson | 7.4.2 | JSON serialization |
| Adafruit_EPD | 4.6.1 | E-paper display |
| Adafruit_PWMServo | 2.4.1 | Servo control |
| ESPAsyncWebServer | master | HTTP server |
| AsyncTCP | master | TCP handling |
| QRCode | 0.0.1 | WiFi setup QR |
| Time | 1.6.1 | NTP and timekeeping |
| Mozzi | 2.0.2 | Audio synthesis |

---

## 19. Future Considerations

The following areas are identified for potential enhancement:

- Scheduled feeding (time-based automation)
- Mobile application integration
- Voice assistant compatibility
- Multiple device coordination
- Cloud backup of recipes and settings
- Consumption analytics and reporting

---

## Appendix A: API Response Examples

### A.1 Device Status Response (`/api/status`)

```json
{
  "battery": 72,
  "state": 0,
  "lastFeedTime": 1706234400,
  "lastRecipe": "Morning Mix",
  "event": 0
}
```

| Field | Type | Description |
|-------|------|-------------|
| `battery` | Integer (0-100) | Current battery percentage |
| `state` | Integer | `DeviceOperationState_e` value (see §13.1) |
| `lastFeedTime` | Integer | Unix timestamp of last successful feed |
| `lastRecipe` | String | Name of the recipe last used |
| `event` | Integer | `DeviceEvent_e` value (see §13.2) |

### A.2 Tank Details Response (`/api/tanks`)

```json
[
  {
    "uid": "A1B2C3D4E5F6G7H8",
    "name": "Chicken Kibble",
    "busIndex": 0,
    "remainingWeightGrams": 800,
    "capacity": 2.5,
    "density": 0.65,
    "calibration": {
      "idlePwm": 1500
    },
    "lastDispensed": 0,
    "totalDispensed": 0
  }
]
```

### A.3 Recipe Response

```json
{
  "uid": 1,
  "name": "Morning Mix",
  "ingredients": [
    {"tankUid": "A1B2C3D4E5F6G7H8", "percentage": 70},
    {"tankUid": "H8G7F6E5D4C3B2A1", "percentage": 30}
  ],
  "dailyWeight": 150,
  "servings": 2,
  "created": "2026-01-10T08:00:00Z",
  "lastUsed": "2026-01-14T07:30:00Z"
}
```

---

## Appendix B: Tank EEPROM Memory Map

Dallas 1-Wire EEPROMs (DS28E07/DS2431+) provide 1Kb (128 bytes) of storage per tank. The 64-bit UID is read from the 1-Wire ROM (factory-programmed), not stored in the data area.

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 6 | Last Base MAC |
| 0x06 | 1 | Bus Index |
| 0x07 | 1 | Name Length |
| 0x08 | 2 | Capacity (mL) |
| 0x0A | 2 | Density (g/L) |
| 0x0C | 2 | Servo Idle PWM |
| 0x0E | 2 | Remaining Weight (g) |
| 0x10 | 80 | Name |
| 0x60 | 32 | Reed-Solomon ECC |

Total: 128 bytes (96 data + 32 ECC)

---

*End of Functional Specification*
