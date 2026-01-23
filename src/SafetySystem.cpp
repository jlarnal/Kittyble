#include "SafetySystem.hpp"
#include "esp_log.h"

static const char* TAG = "SafetySystem";

SafetySystem::SafetySystem(DeviceState& deviceState, SemaphoreHandle_t& mutex, TankManager& tankManager)
    : _deviceState(deviceState), _mutex(mutex), _tankManager(tankManager) {}

void SafetySystem::startTask() {
    xTaskCreate(
        _safetyTask,
        "Safety Task",
        4096,
        this,
        10, // High priority to ensure it can react quickly
        NULL
    );
}

void SafetySystem::_safetyTask(void *pvParameters) {
    SafetySystem* instance = (SafetySystem*)pvParameters;
    ESP_LOGI(TAG, "Safety Task started.");
    
    float lastWeightForStallCheck = 0;
    TickType_t stallCheckStartTime = 0;
    const TickType_t STALL_TIMEOUT_MS = 5000; // 5 seconds

    for (;;) {
        // Run safety checks 10 times per second
        vTaskDelay(pdMS_TO_TICKS(100));

        bool isFeeding = false;
        float currentWeight = 0;
        bool safetyEngaged = false;

        if (xSemaphoreTake(instance->_mutex, portMAX_DELAY) == pdTRUE) {
            isFeeding = (instance->_deviceState.currentFeedingStatus != "Idle" && instance->_deviceState.currentFeedingStatus != "Error");
            currentWeight = instance->_deviceState.currentWeight;
            safetyEngaged = instance->_deviceState.safetyModeEngaged;
            xSemaphoreGive(instance->_mutex);
        }

        if (safetyEngaged) {
            continue;
        }

        if (isFeeding) {
            if (stallCheckStartTime == 0) {
                stallCheckStartTime = xTaskGetTickCount();
                lastWeightForStallCheck = currentWeight;
            } else {
                if (abs(currentWeight - lastWeightForStallCheck) > 0.2) {
                    stallCheckStartTime = xTaskGetTickCount();
                    lastWeightForStallCheck = currentWeight;
                } else {
                    if ((xTaskGetTickCount() - stallCheckStartTime) > pdMS_TO_TICKS(STALL_TIMEOUT_MS)) {
                        ESP_LOGE(TAG, "SAFETY ALERT: Motor stall detected! No weight change in %d ms. Stopping all servos.", STALL_TIMEOUT_MS);
                        instance->_tankManager.stopAllServos();
                        if (xSemaphoreTake(instance->_mutex, portMAX_DELAY) == pdTRUE) {
                            instance->_deviceState.safetyModeEngaged = true;
                            instance->_deviceState.lastEvent = DeviceEvent_e::DEVEVENT_MOTOR_STALL;
                            instance->_deviceState.currentFeedingStatus = "Error";
                            xSemaphoreGive(instance->_mutex);
                        }
                        stallCheckStartTime = 0;
                    }
                }
            }
        } else {
            stallCheckStartTime = 0;
        }

        if (currentWeight > 500.0) {
            ESP_LOGE(TAG, "SAFETY ALERT: Bowl overfill detected! Weight: %.2fg. Stopping all servos.", currentWeight);
            instance->_tankManager.stopAllServos();
            if (xSemaphoreTake(instance->_mutex, portMAX_DELAY) == pdTRUE) {
                instance->_deviceState.safetyModeEngaged = true;
                instance->_deviceState.lastEvent = DeviceEvent_e::DEVEVENT_BOWL_OVERFILL;
                instance->_deviceState.currentFeedingStatus = "Error";
                xSemaphoreGive(instance->_mutex);
            }
        }
    }
}
