#include <Arduino.h>
#include "esp_log.h"
#include "FS.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

#include "DeviceState.hpp"
#include "ConfigManager.hpp"
#include "TimeKeeping.hpp"
#include "board_pinout.h"
#include "TankManager.hpp"
#include "HX711Scale.hpp"
#include "RecipeProcessor.hpp"
#include "EPaperDisplay.hpp"
#include "SafetySystem.hpp"
#include "WebServer.hpp"
#include <SPIFFS.h>
#include "Battery.h"
#include "test.h" // Include the new test header

// --- Global Objects ---
ConfigManager configManager("KibbleT5");
TimeKeeping timeKeeping(globalDeviceState, xDeviceStateMutex, configManager);
TankManager tankManager(globalDeviceState, xDeviceStateMutex);
HX711Scale scale(globalDeviceState, xDeviceStateMutex, configManager);
RecipeProcessor recipeProcessor(globalDeviceState, xDeviceStateMutex, configManager, tankManager, scale);
EPaperDisplay display(globalDeviceState, xDeviceStateMutex);
SafetySystem safetySystem(globalDeviceState, xDeviceStateMutex, tankManager);
WebServer webServer(globalDeviceState, xDeviceStateMutex, configManager, recipeProcessor, tankManager, display);
Battery battMon(3000, 4200, BATT_HALFV_PIN);


static const char* TAG    = "main";
static const char* OTATAG = "OTA update";

#if !defined(KIBBLET5_DEBUG_ENABLED) && defined(LOG_TO_FILE_ENABLED)
#define LOG_TO_SPIFFS
#elif defined(KIBBLET5_DEBUG_ENABLED)
static void printSPIFFSTree(fs::FS& fs, const char* path, uint8_t depth = 0);
#endif

// --- Prototypes for RTOS Tasks ---
void feedingTask(void* pvParameters);
void battAndOTA_Task(void* pvParameters);

#ifdef LOG_TO_SPIFFS

#include "RollingLog.hpp"
#define MAX_SPIFFS_LOG_SIZE (262144UL)
static RollingLog _spiffsLog(SPIFFS, "/log.txt", 64 * 1024);
static bool open_spiffs_log();
static int log_to_spiff(const char*, va_list);

#endif

void setup()
{

    if (!SPIFFS.begin()) {
        ESP_LOGE(TAG, "Fatal: Could not initialize SPIFFS partition.");
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS partition mounted.");
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        ESP_LOGI(TAG, "Listing files in SPIFFS:");
        while (file) {
            ESP_LOGI(TAG, "  FILE: %s, SIZE: %d", file.name(), file.size());
            file = root.openNextFile();
        }
    }


    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    // Wait a moment for serial to initialize
    delay(1000);

#ifdef LOG_TO_SPIFFS
    if (open_spiffs_log()) {
        // Redirect all esp_log output (log_i, log_e, etc.) to our custom function.
        _spiffsLog.print("\r\n<ESP32 restart>\r\n");
        _spiffsLog.flush();
        esp_log_set_vprintf(log_to_spiff);
        esp_log_level_set("*", esp_log_level_t::ESP_LOG_WARN);
        vTaskDelay(pdMS_TO_TICKS(50));
        Serial.println("Redirected ESP_LOG to SPIFFS file.");
    } else {
        // Fallback to default Serial output if SPIFFS fails.
        Serial.print("Failed to initialize SPIFFS logging. Using Serial output.\r\n");
        Serial.setDebugOutput(true);
    }
#else
    Serial.println("LOG_TO_SPIFFS disabled. Using Serial output.");
    esp_log_level_set("*", esp_log_level_t::ESP_LOG_INFO);

    Serial.setDebugOutput(true);
#endif

    ESP_LOGI(TAG, "--- KibbleT5 Starting Up ---");

    xDeviceStateMutex = xSemaphoreCreateMutex();
    if (xDeviceStateMutex == NULL) {
        ESP_LOGE(TAG, "Fatal: Could not create device state mutex.");
        return;
    } else {
        ESP_LOGI(TAG, "Device state mutex instantiated.");
    }


    globalDeviceState.Settings.begin();
    configManager.begin();
    display.begin();
    display.showBootScreen();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize hardware before running tests
    uint16_t hopper_closed, hopper_open;
    configManager.loadHopperCalibration(hopper_closed, hopper_open);
    tankManager.begin(hopper_closed, hopper_open);
    scale.begin(HX711_DATA_PIN, HX711_CLOCK_PIN);

#ifdef KIBBLET5_DEBUG_ENABLED
    // --- RUN DIAGNOSTIC AND TEST CLI ---
    Serial.print("\r\n=== Content of the SPIFFS partition ===\r\n");
    printSPIFFSTree(SPIFFS, "/");
    Serial.print("\r\n===end of SPIFFS content enumeration ===\r\n");
    doDebugTest(tankManager, scale);
#endif

    bool wifiConnected = webServer.manageWiFiConnection();

    if (wifiConnected) {

        ESP_LOGI(TAG, "IP address is %s", WiFi.localIP().toString().c_str());
        ArduinoOTA.begin();
        xTaskCreate(battAndOTA_Task, "Batt monitor", 3192, &battMon, 10, NULL);


        webServer.startAPIServer(); // 1
        timeKeeping.begin(); // 2
        timeKeeping.startTask(); // 4
        safetySystem.startTask(); // 5
        scale.startTask(); // 8
        tankManager.startTask(); // 6
        recipeProcessor.begin(); // 3
        display.startTask(); // 7
        xTaskCreate(feedingTask, "Feeding Task", 4096, &recipeProcessor, 10, NULL);


        ESP_LOGI(TAG, "--- Setup Complete, System Operational ---");
    } else {
        ESP_LOGE(TAG, "Fatal: WiFi could not be configured. Halting.");
        display.showError("WiFi Failed", "Halting system.");
    }
}

void loop()
{

#ifdef LOG_TO_SPIFFS
    static uint32_t elapsed = 0U, loopCount = 0U;
    if ((millis() - elapsed) > 500) {
        elapsed = millis();
        ESP_LOGI("loop", "Iteration #%u, this message is purposefully longer than it should.\r\n", loopCount);
        Serial.printf("Iteration #%u, this message is purposefully longer than it should.\r\n", loopCount);
        loopCount++;
    }
    vTaskDelay(50);
#endif
    //   vTaskDelete(NULL);
}

void battAndOTA_Task(void* pvParameters)
{
    constexpr uint32_t BATTERY_SAMPLING_PERIOD_MS = 500;
    constexpr size_t REPORTS_PERIOD               = 5000 / BATTERY_SAMPLING_PERIOD_MS;

    size_t reports = REPORTS_PERIOD;
    if (pvParameters == nullptr) {
        ESP_LOGE(TAG, "Battery object pointer was null in `batteryTask`");
        return;
    } else {
        ESP_LOGI(TAG, "Battery manager running.");
        Serial.flush();
    }

    Battery* pBatt = (Battery*)pvParameters;
    pBatt->begin(3300, 0.5f, asigmoidal);
    uint16_t voltage;
    for (;;) {
        pBatt->refreshAverage();
        pBatt->getAverages(&voltage, &globalDeviceState.batteryLevel);
#if defined(PRINT_BATT_STATUS) && !defined(LOG_TO_SPIFFS)
        if (!reports--) {
            Serial.printf("Battery status: %dmV, %d%%\r\n", voltage, globalDeviceState.batteryLevel);
            reports = REPORTS_PERIOD;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLING_PERIOD_MS));
    }
}

void feedingTask(void* pvParameters)
{
    RecipeProcessor* processor = (RecipeProcessor*)pvParameters;
    ESP_LOGI(TAG, "Feeding Task Started.");

    for (;;) {
        FeedCommand command = {};
        bool commandPresent = false;

        if (xSemaphoreTake(xDeviceStateMutex, portMAX_DELAY) == pdTRUE) {
            if (!globalDeviceState.feedCommand.processed) {
                command                                 = globalDeviceState.feedCommand;
                globalDeviceState.feedCommand.processed = true;
                commandPresent                          = true;
            }
            xSemaphoreGive(xDeviceStateMutex);
        }

        if (commandPresent) {
            bool success = false;
            ESP_LOGI(TAG, "Processing new command: %d", (int)command.type);

            if (xSemaphoreTake(xDeviceStateMutex, portMAX_DELAY) == pdTRUE) {
                globalDeviceState.currentFeedingStatus = "Processing...";
                xSemaphoreGive(xDeviceStateMutex);
            }

            switch (command.type) {
                case FeedCommandType::IMMEDIATE:
                    success = processor->executeImmediateFeed(command.tankUid, command.amountGrams);
                    break;
                case FeedCommandType::RECIPE:
                    success = processor->executeRecipeFeed(command.recipeId);
                    break;
                case FeedCommandType::TARE_SCALE:
                    processor->getScale().tare();
                    success = true;
                    break;
                case FeedCommandType::EMERGENCY_STOP:
                    processor->stopAllFeeding();
                    success = true;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown command type in feeding task.");
                    break;
            }

            if (xSemaphoreTake(xDeviceStateMutex, portMAX_DELAY) == pdTRUE) {
                globalDeviceState.currentFeedingStatus = success ? "Idle" : "Error";
                if (!success) {
                    // Keep the error message that was set by the processor
                } else {
                    globalDeviceState.lastError = "";
                }
                globalDeviceState.feedCommand.type = FeedCommandType::NONE;
            }
            xSemaphoreGive(xDeviceStateMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

#ifdef KIBBLET5_DEBUG_ENABLED
static void printIndent(uint8_t depth)
{
    for (uint8_t i = 0; i < depth; ++i)
        Serial.print("  "); // two spaces per level
}

static void printSPIFFSTree(fs::FS& fs, const char* path, uint8_t depth)
{
    File dir = fs.open(path);
    if (!dir) {
        Serial.printf("Failed to open '%s'\n", path);
        return;
    }
    if (!dir.isDirectory()) {
        Serial.printf("'%s' is not a directory\n", path);
        dir.close();
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        // name() returns the full path (e.g. /folder/file.txt)
        const char* name = file.name();
        if (file.isDirectory()) {
            printIndent(depth);
            Serial.printf("└─ %s/\n", name);
            // recurse into subdirectory
            printSPIFFSTree(fs, name, depth + 1);
        } else {
            printIndent(depth);
            Serial.printf("└─ %s\t%u bytes\n", name, (unsigned int)file.size());
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
}
#endif

#ifdef LOG_TO_SPIFFS
bool open_spiffs_log()
{

    if (_spiffsLog.begin(true)) {
        ESP_LOGI(TAG, "SPIFFS log.txt opened successfully.");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to open SPIFFS log.txt.");
    }
    return false;
}

int log_to_spiff(const char* format, va_list args)
{
    int result = _spiffsLog.vprintf(format, args);
    Serial.printf("Wrote %d chars to log.\r\n", result);
    return result;
}



#endif // LOG_TO_SPIFFS