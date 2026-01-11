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
    TickType_t timeout = pdMS_TO_TICKS(20 * FAST_MODE_SAMPLING_PERIOD_MS + 100);

    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        ESP_LOGI(TAG, "Taring scale...");
        if (_scale.tare(20)) {
            _zeroOffset = _scale.get_offset();
            xSemaphoreGive(_scaleMutex);
            ESP_LOGI(TAG, "Tare complete. New offset: %ld", _zeroOffset);
            saveCalibration();
        } else {
            ESP_LOGE(TAG, "Tare failed due to unresponsiveness of the HX711.");
        }
    } else {
        ESP_LOGE(TAG, "Failed to acquire scale mutex for tare().");
    }
}

float HX711Scale::getWeight()
{
    float weight       = 0.0f;
    uint8_t samples    = _deviceState.Settings.getScaleSamplesCount() > 0 ? _deviceState.Settings.getScaleSamplesCount() : 1;
    TickType_t timeout = pdMS_TO_TICKS(samples * FAST_MODE_SAMPLING_PERIOD_MS + 50);
    uint8_t failures   = 0;
    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        weight = _scale.get_units(samples, failures);
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
    uint8_t samples    = _deviceState.Settings.getScaleSamplesCount() > 0 ? _deviceState.Settings.getScaleSamplesCount() : 1;
    TickType_t timeout = pdMS_TO_TICKS(samples * FAST_MODE_SAMPLING_PERIOD_MS + 50);
    uint8_t failures   = 0;
    if (xSemaphoreTake(_scaleMutex, timeout) == pdTRUE) {
        rawValue = _scale.read_average(samples, failures);
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
    constexpr uint32_t SCALE_SAMPLING_PERIOD_MS = 250; // 4Hz
    constexpr size_t SCALE_REPORTS_PERIOD       = 5000 / SCALE_SAMPLING_PERIOD_MS;

    HX711Scale* instance = (HX711Scale*)pvParameters;
    ESP_LOGI(TAG, "Scale Task Started. Tare initiated.");
    instance->tare();


#if defined(PRINT_SCALE_STATUS)
    size_t reports = SCALE_REPORTS_PERIOD;
#endif
    for (;;) {
        // These calls are now thread-safe due to the internal mutex.
        float current_weight = instance->getWeight();
        long raw_value       = instance->getRawReading();
        if (xSemaphoreTake(instance->_mutex, portMAX_DELAY) == pdTRUE) {
            // This mutex protects the global device state, which is a different resource.
            if (isnanf(current_weight)) {
                instance->_deviceState.isWeightStable    = false;
                instance->_deviceState.isScaleResponding = false;
            } else {
                instance->_deviceState.isWeightStable    = (abs(current_weight - instance->_deviceState.currentWeight) < 0.5);
                instance->_deviceState.currentWeight     = current_weight;
                instance->_deviceState.currentRawValue   = raw_value;
                instance->_deviceState.isScaleResponding = true;                
            }
            xSemaphoreGive(instance->_mutex);
        }


#if defined(PRINT_SCALE_STATUS) && !defined(LOG_TO_SPIFFS)
        if (!reports--) {
            if (instance->_deviceState.isScaleResponding != false) {
                Serial.printf("Scale status: %s, %fg (%d)\r\n", (instance->_deviceState.isWeightStable ? "stable" : "unstable"),
                  instance->_deviceState.currentWeight, instance->_deviceState.currentRawValue);
            } else {
                Serial.print("Scale status: UNRESPONSIVE !\r\n");
            }
            reports = SCALE_REPORTS_PERIOD;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(SCALE_SAMPLING_PERIOD_MS));
    }
}
