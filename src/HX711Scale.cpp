#include "HX711Scale.hpp"
#include "esp_log.h"

static const char* TAG = "HX711Scale";

HX711Scale::HX711Scale(DeviceState& deviceState, SemaphoreHandle_t& mutex, ConfigManager& configManager)
    : _deviceState(deviceState), _mutex(mutex), _configManager(configManager), _scaleMutex(NULL), _calibrationFactor(400.0f), _zeroOffset(0)
{}

bool HX711Scale::begin(uint8_t dataPin, uint8_t clockPin)
{
    _scaleMutex = xSemaphoreCreateMutex();
    if (_scaleMutex == NULL) {
        ESP_LOGE(TAG, "Fatal: Could not create scale mutex.");
        return false;
    }

    _scale.begin(dataPin, clockPin);
    _configManager.loadScaleCalibration(_calibrationFactor, _zeroOffset);
    _scale.set_scale(_calibrationFactor);
    _scale.set_offset(_zeroOffset);
    ESP_LOGI(TAG, "Scale initialized with factor: %.2f, offset: %ld", _calibrationFactor, _zeroOffset);
    return true;
}

void HX711Scale::tare()
{
    // Tare takes a fixed number of samples (20), so we can calculate a generous timeout.
    TickType_t timeout = pdMS_TO_TICKS(20 * FAST_MODE_SAMPLING_PERIOD_MS + 150);

    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        ESP_LOGI(TAG, "Taring scale...");
        // Ensure HX711 is powered up for blocking read
        _scale.power_up();
        vTaskDelay(pdMS_TO_TICKS(55)); // Wait for settling
        if (_scale.tare(20)) {
            _zeroOffset = _scale.get_offset();
            ESP_LOGI(TAG, "Tare complete. New offset: %ld", _zeroOffset);
            saveCalibration();
        } else {
            ESP_LOGE(TAG, "Tare failed due to unresponsiveness of the HX711.");
        }
        xSemaphoreGive(_scaleMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire scale mutex for tare().");
    }
}

float HX711Scale::getWeight()
{
    float weight       = 0.0f;
    TickType_t timeout = pdMS_TO_TICKS(CALIBRATION_SAMPLES * FAST_MODE_SAMPLING_PERIOD_MS + 50);
    uint8_t failures   = 0;
    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        // Ensure HX711 is powered up for blocking read
        _scale.power_up();
        vTaskDelay(pdMS_TO_TICKS(55)); // Wait for settling
        weight = _scale.get_units(CALIBRATION_SAMPLES, failures);
        xSemaphoreGive(_scaleMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire scale mutex for getWeight().");
    }
    if (failures)
        return NAN;
    return weight;
}

long HX711Scale::getRawReading()
{
    long rawValue      = 0;
    TickType_t timeout = pdMS_TO_TICKS(CALIBRATION_SAMPLES * FAST_MODE_SAMPLING_PERIOD_MS + 50);
    uint8_t failures   = 0;
    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        // Ensure HX711 is powered up for blocking read
        _scale.power_up();
        vTaskDelay(pdMS_TO_TICKS(55)); // Wait for settling
        rawValue = _scale.read_average(CALIBRATION_SAMPLES, failures);
        xSemaphoreGive(_scaleMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire scale mutex for getRawReading().");
    }
    if (failures)
        return 0;
    return rawValue;
}

float HX711Scale::calibrateWithKnownWeight(float knownWeight)
{
    if (knownWeight <= 0) {
        ESP_LOGE(TAG, "Calibration failed: Known weight must be positive.");
        return _calibrationFactor;
    }
    // getRawReading() is already thread-safe.
    long reading = getRawReading();

    // setCalibrationFactor() is also thread-safe.
    float new_factor = (float)(reading - _zeroOffset) / knownWeight;
    setCalibrationFactor(new_factor);

    saveCalibration();
    ESP_LOGI(TAG, "Scale calibrated with new factor: %.4f", new_factor);
    return new_factor;
}

void HX711Scale::setCalibrationFactor(float factor)
{
    // This requires a very short lock as it's a quick operation.
    if (xSemaphoreTake(_scaleMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _calibrationFactor = factor;
        _scale.set_scale(factor);
        xSemaphoreGive(_scaleMutex);
        ESP_LOGI(TAG, "Calibration factor set to: %.2f", factor);
    } else {
        ESP_LOGE(TAG, "Failed to acquire scale mutex for setCalibrationFactor().");
    }
}

float HX711Scale::getCalibrationFactor()
{
    return _calibrationFactor;
}

long HX711Scale::getZeroOffset()
{
    return _zeroOffset;
}

void HX711Scale::saveCalibration()
{
    _configManager.saveScaleCalibration(_calibrationFactor, _zeroOffset);
    ESP_LOGI(TAG, "Scale calibration saved to NVS.");
}

void HX711Scale::startTask()
{
    xTaskCreate(_scaleTask, "Scale Task", 4096, this, 5, NULL);
}

void HX711Scale::_scaleTask(void* pvParameters)
{
    HX711Scale* instance = (HX711Scale*)pvParameters;
    ESP_LOGI(TAG, "Scale Task Started. Tare initiated.");
    instance->tare();

    // Initialize state machine
    instance->_state = ScaleState::SAMPLING;
    instance->_rawSum = 0;
    instance->_sampleCount = 0;
    instance->_failureCount = 0;
    instance->_tickCounter = 0;
    instance->_idleTickCounter = 0;
    instance->_settlingCounter = 0;
    instance->_reportCounter = 0;

    for (;;) {
        switch (instance->_state) {
            case ScaleState::SAMPLING: {
                // Timebase 1: collect samples at ~13ms intervals
                if (xSemaphoreTake(instance->_scaleMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    if (instance->_scale.is_ready()) {
                        long sample = instance->_scale.read();
                        if (sample != 0) {
                            instance->_rawSum += sample;
                            instance->_sampleCount++;
                        } else {
                            instance->_failureCount++;
                        }
                    }
                    xSemaphoreGive(instance->_scaleMutex);
                }
                instance->_tickCounter++;

                // Timebase 2: After ~250ms worth of ticks, compute and publish averages
                if (instance->_tickCounter >= TICKS_PER_AVERAGE) {
                    if (xSemaphoreTake(instance->_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        if (instance->_sampleCount > 0) {
                            long avgRaw = instance->_rawSum / instance->_sampleCount;
                            float avgWeight = (float)(avgRaw - instance->_zeroOffset) / instance->_calibrationFactor;

                            instance->_deviceState.isWeightStable = (abs(avgWeight - instance->_deviceState.currentWeight) < 0.5f);
                            instance->_deviceState.currentWeight = avgWeight;
                            instance->_deviceState.currentRawValue = avgRaw;
                            instance->_deviceState.isScaleResponding = true;

                            if (instance->_onWeightChangedCallback) {
                                instance->_onWeightChangedCallback(avgWeight, avgRaw);
                            }
                        } else {
                            // No valid samples collected
                            instance->_deviceState.isWeightStable = false;
                            instance->_deviceState.isScaleResponding = false;
                        }
                        xSemaphoreGive(instance->_mutex);
                    }

                    // Timebase 3: Report every 5s
                    instance->_reportCounter++;
#if defined(PRINT_SCALE_STATUS) && !defined(LOG_TO_SPIFFS)
                    if (instance->_reportCounter >= REPORTS_PERIOD) {
                        if (instance->_deviceState.isScaleResponding) {
                            ESP_LOGI(TAG, "Scale status: %s, %.2fg (%ld), samples=%u, failures=%u",
                                (instance->_deviceState.isWeightStable ? "stable" : "unstable"),
                                instance->_deviceState.currentWeight,
                                instance->_deviceState.currentRawValue,
                                instance->_sampleCount,
                                instance->_failureCount);
                        } else {
                            ESP_LOGW(TAG, "Scale status: UNRESPONSIVE!");
                        }
                        instance->_reportCounter = 0;
                    }
#else
                    if (instance->_reportCounter >= REPORTS_PERIOD) {
                        instance->_reportCounter = 0;
                    }
#endif

                    // Reset accumulators for next window
                    instance->_rawSum = 0;
                    instance->_sampleCount = 0;
                    instance->_failureCount = 0;
                    instance->_tickCounter = 0;

                    // Power down and transition to IDLE
                    if (xSemaphoreTake(instance->_scaleMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        instance->_scale.power_down();
                        xSemaphoreGive(instance->_scaleMutex);
                    }
                    instance->_state = ScaleState::IDLE;
                    instance->_idleTickCounter = 0;
                }
                break;
            }

            case ScaleState::IDLE: {
                instance->_idleTickCounter++;
                // Stay idle for ~200ms, then wake up and allow settling
                if (instance->_idleTickCounter >= IDLE_TICKS) {
                    if (xSemaphoreTake(instance->_scaleMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        instance->_scale.power_up();
                        xSemaphoreGive(instance->_scaleMutex);
                    }
                    instance->_state = ScaleState::SETTLING;
                    instance->_settlingCounter = 0;
                }
                break;
            }

            case ScaleState::SETTLING: {
                instance->_settlingCounter++;
                // Wait for settling time (~52ms) before starting to sample
                if (instance->_settlingCounter >= SETTLING_TICKS) {
                    instance->_state = ScaleState::SAMPLING;
                    instance->_tickCounter = 0;
                }
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void HX711Scale::setOnWeightChangedCallback(std::function<void(float, long)> cb) {
    _onWeightChangedCallback = cb;
}
