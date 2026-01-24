#ifndef HX711SCALE_HPP
#define HX711SCALE_HPP

#include <functional>
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
    void setOnWeightChangedCallback(std::function<void(float, long)> cb);

private:
    HX711 _scale;
    DeviceState& _deviceState;
    SemaphoreHandle_t& _mutex; // Mutex for the shared DeviceState
    ConfigManager& _configManager;

    // A dedicated mutex to protect access to the _scale object and HX711 hardware
    SemaphoreHandle_t _scaleMutex;

    float _calibrationFactor;
    long _zeroOffset;
    std::function<void(float, long)> _onWeightChangedCallback;

    // State machine for non-blocking operation
    enum class ScaleState { SAMPLING, SETTLING, IDLE };
    ScaleState _state;

    // Accumulators (reset each averaging window)
    long _rawSum;
    uint8_t _sampleCount;
    uint8_t _failureCount;

    // Timebase counters
    uint8_t _tickCounter;       // counts ticks within sampling window
    uint8_t _idleTickCounter;   // counts ticks during idle/power-down
    uint8_t _settlingCounter;   // counts ticks during settling after power-up
    uint8_t _reportCounter;     // counts averaging windows for 5s report

    // Timing constants
    static constexpr uint32_t TICK_MS = 13;              // ~77Hz task tick
    static constexpr uint8_t TICKS_PER_AVERAGE = 19;     // ~247ms sampling window
    static constexpr uint8_t IDLE_TICKS = 15;            // ~195ms idle (250-55ms for settling margin)
    static constexpr uint8_t SETTLING_TICKS = 4;         // ~52ms settling after power-up
    static constexpr uint8_t REPORTS_PERIOD = 20;        // 20×250ms = 5s
    static constexpr uint8_t CALIBRATION_SAMPLES = 10;   // Fixed sample count for calibration/tare API

    static void _scaleTask(void *pvParameters);
};

#endif // HX711SCALE_HPP
