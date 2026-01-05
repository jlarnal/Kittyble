#ifndef HX711SCALE_HPP
#define HX711SCALE_HPP

#include "HX711.h"
#include "DeviceState.hpp"
#include "ConfigManager.hpp"

// The HX711 can be set to 80Hz mode, but accounting for timing drifts, 
// we'll use a slightly more conservative value for timeout calculations.
#define FAST_MODE_SAMPLING_PERIOD_MS ((uint32_t)(1000/75)) 

/**
 * @file HX711Scale.hpp
 * @brief Manages the load cell and HX711 amplifier in a thread-safe manner.
 */

class HX711Scale {
public:
    HX711Scale(DeviceState& deviceState, SemaphoreHandle_t& mutex, ConfigManager& configManager);
    bool begin(uint8_t dataPin, uint8_t clockPin);
    void tare();
    float getWeight();
    long getRawReading();
    void startTask();
    
    // New method for calibration based on the API schema
    float calibrateWithKnownWeight(float knownWeight);

    void setCalibrationFactor(float factor);
    float getCalibrationFactor();
    long getZeroOffset();
    void saveCalibration();

private:
    HX711 _scale;
    DeviceState& _deviceState;
    SemaphoreHandle_t& _mutex; // Mutex for the shared DeviceState
    ConfigManager& _configManager;

    // A dedicated mutex to protect access to the _scale object and HX711 hardware
    SemaphoreHandle_t _scaleMutex; 

    float _calibrationFactor;
    long _zeroOffset;

    static void _scaleTask(void *pvParameters);
};

#endif // HX711SCALE_HPP
