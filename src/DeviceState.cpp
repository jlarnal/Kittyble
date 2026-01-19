#include "DeviceState.hpp"
#include "esp_log.h"
#include "ArduinoJson.h"
#include <SPIFFS.h>

static const char* TAG           = "DeviceSettings";
static const char* SETTINGS_FILE = "/settings.json";

// --- Private Methods for SPIFFS Interaction ---

/**
 * @brief Saves the current settings from the in-memory struct to the SPIFFS file.
 * This is a private helper function called by the public setters.
 */
static void _saveSettingsToFile(const DeviceState::Settings_t& settings)
{
    JsonDocument doc;
    doc["dispenseWeightChangeThreshold"]      = settings.getDispensingWeightChangeThreshold();
    doc["dispensingNoWeightChangeTimeout_ms"] = settings.getDispensingNoWeightChangeTimeout_ms();
    doc["scaleSamplesCount"]                  = settings.getScaleSamplesCount();

    File file = SPIFFS.open(SETTINGS_FILE, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open settings file for writing");
        return;
    }

    if (serializeJson(doc, file) == 0) {
        ESP_LOGE(TAG, "Failed to write to settings file");
    } else {
        ESP_LOGI(TAG, "Settings successfully saved to %s", SETTINGS_FILE);
    }
    file.close();
}

/**
 * @brief Loads settings from the SPIFFS file into the in-memory struct.
 * Called once at startup. If the file doesn't exist, it initializes with defaults.
 */
static bool _loadSettingsFromFile(DeviceState::Settings_t& settings)
{
    if (!SPIFFS.exists(SETTINGS_FILE)) {
        ESP_LOGW(TAG, "Settings file not found. Initializing with defaults and creating file.");
        settings.resetToDefaults(); // This will also trigger the first save.
        return false;
    }

    File file = SPIFFS.open(SETTINGS_FILE, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open settings file for reading. Using defaults.");
        settings.resetToDefaults();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        ESP_LOGE(TAG, "Failed to parse settings file. Using defaults. Error: %s", error.c_str());
        settings.resetToDefaults();
        return false;
    }

    // Load values from JSON, using defaults as a fallback if a key is missing.
    settings.setDispensingWeightChangeThreshold(doc["dispenseWeightChangeThreshold"] | 3.0f);
    settings.setDispensingNoWeightChangeTimeout_ms(doc["dispensingNoWeightChangeTimeout_ms"] | 10000);
    settings.setScaleSamplesCount(doc["scaleSamplesCount"] | 5);

    ESP_LOGI(TAG, "Settings loaded successfully from %s", SETTINGS_FILE);
    return true;
}


// --- Public Method Implementations ---

// Constructor: Initializes settings to their default values and then tries to load from SPIFFS.
bool DeviceState::Settings_t::begin()
{
    resetToDefaults(false); // Initialize without saving
    return _loadSettingsFromFile(*this);
}

// Resets all settings to their default values and optionally saves to file.
void DeviceState::Settings_t::resetToDefaults(bool save)
{
    _dispensingWeightChangeThreshold    = 3.0f;
    _dispensingNoWeightChangeTimeout_ms = 10000;
    _scaleSamplesCount                  = 5;
    if (save) {
        _saveSettingsToFile(*this);
    }
    ESP_LOGI(TAG, "Settings reset to default values.");
}


// --- Getters ---

float DeviceState::Settings_t::getDispensingWeightChangeThreshold() const
{
    return _dispensingWeightChangeThreshold;
}

uint32_t DeviceState::Settings_t::getDispensingNoWeightChangeTimeout_ms() const
{
    return _dispensingNoWeightChangeTimeout_ms;
}

uint8_t DeviceState::Settings_t::getScaleSamplesCount() const
{
    return _scaleSamplesCount;
}

// --- Setters ---

void DeviceState::Settings_t::setDispensingWeightChangeThreshold(float newValue)
{
    if (_dispensingWeightChangeThreshold != newValue) {
        _dispensingWeightChangeThreshold = newValue;
        _saveSettingsToFile(*this);
    }
}

void DeviceState::Settings_t::setDispensingNoWeightChangeTimeout_ms(uint32_t value)
{
    if (_dispensingNoWeightChangeTimeout_ms != value) {
        _dispensingNoWeightChangeTimeout_ms = value;
        _saveSettingsToFile(*this);
    }
}

void DeviceState::Settings_t::setScaleSamplesCount(uint8_t value)
{
    if (_scaleSamplesCount != value) {
        _scaleSamplesCount = value > 0 ? value : 1; // Ensure at least 1 sample
        _saveSettingsToFile(*this);
    }
}


void DeviceState::printTo(DeviceState& state, Stream& stream)
{
    stream.println("=== DEVICE STATE ===");
    stream.println();

    // --- System Status Section ---
    stream.println("--- System Status ---");
    stream.printf("  Operational:           %s\r\n", state.operational ? "true" : "false");
    stream.printf("  Operation State:       %s\r\n", state.getStateString());
    stream.printf("  Last Error:            %s\r\n", state.lastError.c_str());
    stream.printf("  Safety Mode Engaged:   %s\r\n", state.safetyModeEngaged ? "true" : "false");
    stream.printf("  Uptime (s):            %u\r\n", state.uptime_s);
    stream.printf("  WiFi Strength:         %d dBm\r\n", state.wifiStrength);
    stream.printf("  Battery Level:         %u %%\r\n", state.batteryLevel);
    stream.printf("  Last Feed Time:        %lld\r\n", state.lastFeedTime);
    stream.printf("  Device Name:           %s\r\n", state.deviceName.c_str());
    stream.printf("  Firmware Version:      %s\r\n", state.firmwareVersion.c_str());
    stream.printf("  Build Date:            %s\r\n", state.buildDate.c_str());
    stream.printf("  Current Time:          %s\r\n", state.formattedTime);
    stream.flush();

    // --- Scale Section ---
    stream.println();
    stream.println("--- Scale ---");
    stream.printf("  Current Weight:        %.2f g\r\n", state.currentWeight);
    stream.printf("  Raw Scale Value:       %ld\r\n", state.currentRawValue);
    stream.printf("  Is Weight Stable:      %s\r\n", state.isWeightStable ? "true" : "false");
    stream.printf("  Is Scale Responding:   %s\r\n", state.isScaleResponding ? "true" : "false");
    stream.printf("  Servo Power:           %s\r\n", state.servoPower ? "true" : "false");
    stream.flush();

    // --- Connected Tanks Section ---
    stream.println();
    stream.println("--- Connected Tanks ---");
    if (state.connectedTanks.empty()) {
        stream.println("  (no tanks detected)");
    } else {
        stream.printf("  Count: %d\r\n", (int)state.connectedTanks.size());
        for (size_t i = 0; i < state.connectedTanks.size(); i++) {
            const TankInfo& tank = state.connectedTanks[i];
            stream.println();
            stream.printf("  [Tank %d]\r\n", (int)i);
            stream.printf("    UID:              %llX\r\n", (unsigned long long)tank.uid);
            stream.printf("    Name:             %s\r\n", tank.name.c_str());
            stream.printf("    Bus Index:        %d\r\n", (int)tank.busIndex);
            stream.printf("    Full Info:        %s\r\n", tank.isFullInfo ? "yes" : "no");
            if (tank.isFullInfo) {
                stream.printf("    Capacity (L):     %.3f\r\n", tank.capacityLiters);
                stream.printf("    Density (kg/L):   %.3f\r\n", tank.kibbleDensity);
                stream.printf("    Remaining (kg):   %.3f\r\n", tank.remaining_weight_kg);
                stream.printf("    Servo Idle PWM:   %u\r\n", tank.servoIdlePwm);
            }
            stream.flush();
        }
    }

    // --- Last Recipe Section ---
    stream.println();
    stream.println("--- Last Recipe ---");
    if (state.lastRecipe.id == 0 && state.lastRecipe.name.empty()) {
        stream.println("  (none)");
    } else {
        stream.printf("  ID:           %d\r\n", state.lastRecipe.id);
        stream.printf("  Name:         %s\r\n", state.lastRecipe.name.c_str());
        stream.printf("  Daily Weight: %.2f g\r\n", state.lastRecipe.dailyWeight);
        stream.printf("  Servings:     %d\r\n", state.lastRecipe.servings);
        stream.printf("  Enabled:      %s\r\n", state.lastRecipe.isEnabled ? "yes" : "no");
        stream.printf("  Created:      %lld\r\n", state.lastRecipe.created);
        stream.printf("  Last Used:    %lld\r\n", state.lastRecipe.lastUsed);
        stream.flush();

        if (state.lastRecipe.ingredients.empty()) {
            stream.println("  Ingredients:  (none)");
        } else {
            stream.printf("  Ingredients (%d):\r\n", (int)state.lastRecipe.ingredients.size());
            for (size_t j = 0; j < state.lastRecipe.ingredients.size(); j++) {
                const RecipeIngredient& ing = state.lastRecipe.ingredients[j];
                stream.printf("    - Tank UID: %llX, Percentage: %.1f%%\r\n",
                              (unsigned long long)ing.tankUid, ing.percentage);
            }
        }
    }
    stream.flush();

    // --- Feeding History Section ---
    stream.println();
    stream.println("--- Feeding History ---");
    if (state.feedingHistory.empty()) {
        stream.println("  (no history)");
    } else {
        stream.printf("  Entries: %d\r\n", (int)state.feedingHistory.size());
        for (size_t i = 0; i < state.feedingHistory.size(); i++) {
            const FeedingHistoryEntry& entry = state.feedingHistory[i];
            stream.printf("  [%d] ts=%lld type=%s recipe=%d success=%s amt=%.2fg desc=\"%s\"\r\n",
                          (int)i,
                          (long long)entry.timestamp,
                          entry.type.c_str(),
                          entry.recipeId,
                          entry.success ? "Y" : "N",
                          entry.amount,
                          entry.description.c_str());
            if ((i + 1) % 5 == 0) {
                stream.flush();
            }
        }
    }

    stream.println();
    stream.println("=== END DEVICE STATE ===");
    stream.flush();
}
// Global definitions for the state object and its mutex handle.
// The mutex handle is initialized to NULL and created in main.cpp's setup().
DeviceState globalDeviceState;
SemaphoreHandle_t xDeviceStateMutex = NULL;
