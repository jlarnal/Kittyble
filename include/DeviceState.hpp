#ifndef DEVICE_STATE_HPP
#define DEVICE_STATE_HPP

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <string>
#include <time.h>
#include "TankManager.hpp" // For TankInfo struct definition
#include "ConfigManager.hpp" // For Recipe struct definition

/**
 * @file DeviceState.hpp
 * @brief Defines the central, thread-safe data structure for the device's state.
 */

// Enum for different feeding commands initiated by the user/API
enum class FeedCommandType
{
    NONE,
    IMMEDIATE,
    RECIPE,
    EMERGENCY_STOP,
    TARE_SCALE
};

// Struct to hold feeding command details from the API
struct FeedCommand {
    FeedCommandType type = FeedCommandType::NONE;
    uint64_t tankUid     = 0;
    float amountGrams    = 0.0;
    int recipeId         = 0;
    int servings         = 1; // Add the servings member
    bool processed       = true;
};

// Expanded to match the API schema for feeding history
struct FeedingHistoryEntry {
    time_t timestamp;
    std::string type; // "recipe" or "immediate"
    int recipeId;
    bool success;
    float amount;
    std::string description; // e.g., Recipe Name or "Immediate Feed"

    // Constructor to allow for direct initialization.
    FeedingHistoryEntry(time_t ts, const std::string& t, int rId, bool s, float a, const std::string& d)
        : timestamp(ts), type(t), recipeId(rId), success(s), amount(a), description(d)
    {}
};

enum DeviceOperationState_e : uint8_t
{
    DOPSTATE_IDLE,
    DOPSTATE_FEEDING,
    DOPSTATE_ERROR,
    DOPSTATE_CALIBRATING,
};

// The central volatile state structure for the entire application.
struct DeviceState {
    // System Status
    bool operational                      = true;
    DeviceOperationState_e operationState = DeviceOperationState_e::DOPSTATE_IDLE;
    std::string lastError                 = "";
    bool safetyModeEngaged                = false;
    uint32_t uptime_s                     = 0;
    int8_t wifiStrength                   = 0;
    uint8_t batteryLevel                  = 100;
    long long lastFeedTime                = 0;
    Recipe lastRecipe                     = Recipe::EMPTY;
    IPAddress ipAddress;
    std::string deviceName      = "Kittyble";
    std::string firmwareVersion = "1.1.0-stable";
    std::string buildDate       = __DATE__;

    // Time
    time_t currentTime     = 0;
    char formattedTime[20] = "TIME_NOT_SET";

    // Scale
    float currentWeight    = 0.0;
    long currentRawValue   = 0;
    bool isWeightStable    = false;
    bool isScaleResponding = false;

    // Tanks
    std::vector<TankInfo> connectedTanks;

    // Feeding
    FeedCommand feedCommand;
    std::string currentFeedingStatus = "Idle";
    std::vector<FeedingHistoryEntry> feedingHistory;

    // Servo Power
    bool servoPower = false;

    // Nested class for managing persistent device settings.
    class Settings_t {
      public:
        bool begin();
        void resetToDefaults(bool save = true);

        // Getters
        float getDispensingWeightChangeThreshold() const;
        uint32_t getDispensingNoWeightChangeTimeout_ms() const;
        uint8_t getScaleSamplesCount() const;

        // Setters (These automatically save changes to SPIFFS)
        void setDispensingWeightChangeThreshold(float newValue);
        void setDispensingNoWeightChangeTimeout_ms(uint32_t value);
        void setScaleSamplesCount(uint8_t value);

      private:
        float _dispensingWeightChangeThreshold;
        uint32_t _dispensingNoWeightChangeTimeout_ms;
        uint8_t _scaleSamplesCount;
    };

    Settings_t Settings;

    const char* getStateString()
    {
        switch (operationState) {

            case DOPSTATE_FEEDING:
                return "DOPSTATE_FEEDING";
            case DOPSTATE_ERROR:
                return "DOPSTATE_ERROR";
            case DOPSTATE_CALIBRATING:
                return "DOPSTATE_CALIBRATING";
            default:
                break;
        }
        return "IDLE";
    }
};

extern DeviceState globalDeviceState;
extern SemaphoreHandle_t xDeviceStateMutex;

#endif // DEVICE_STATE_HPP
