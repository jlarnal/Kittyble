#include "EPaperDisplay.hpp"
#include "board_pinout.h"
#include "esp_log.h"
#include <qrcode.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/Picopixel.h>

static const char* TAG              = "EPaperDisplay";
static const int AP_QR_CODE_VERSION = 3;
#define QR_WIFI_ECC_TYPE (ECC_LOW)

// Colors for Adafruit_EPD
#define EPD_BLACK        1
#define EPD_WHITE        0

// Constructor for the 2.6" display with SSD1680 controller
EPaperDisplay::EPaperDisplay(DeviceState& deviceState, SemaphoreHandle_t& mutex) : _deviceState(deviceState), _mutex(mutex), _display(nullptr) {}

bool EPaperDisplay::begin()
{
    // Create the display instance with the proper dimensions
    // Adafruit_SSD1680(width, height, DC, RST, CS, SRCS, BUSY, SPI)
    _display = new Ssd1680_Driver(296, 152, EPD_DC, EPD_RST, EPD_CS, -1, EPD_BUSY, &SPI);

    _display->begin();
    // Set rotation to 1 for PORTRAIT mode (152x296)
    _display->setRotation(1);
    _display->setFont(&FreeSans9pt7b);
    _display->setTextColor(EPD_BLACK);

    // Clear the display buffer
    _clearDisplay();

    ESP_LOGI(TAG, "E-Paper Display initialized (SSD1680, Portrait).");
    return true;
}

void EPaperDisplay::startTask()
{
    xTaskCreate(_displayTask, "Display Task", 4096, this, 4, &_displayTaskHandle);
}



void EPaperDisplay::showBootScreen()
{
    _clearDisplay();
    _display->setCursor(10, 140);
    _display->print("KibbleT5 Starting...");
    _display->display();
}

void EPaperDisplay::showError(const char* title, const char* message)
{
    _clearDisplay();
    _display->setFont(&FreeSansBold9pt7b);
    _display->setCursor(10, 20);
    _display->print("ERROR");
    _display->drawFastHLine(10, 25, _display->width() - 20, EPD_BLACK);
    _display->setFont(&FreeSans9pt7b);
    _display->setCursor(10, 50);
    _display->print(title);
    _display->setCursor(10, 70);
    _display->print(message);
    _display->display();
}

void EPaperDisplay::showStatus(const char* title, const char* message)
{
    _clearDisplay();
    _display->setFont(&FreeSansBold9pt7b);
    _display->setCursor(10, 20);
    _display->print(title);
    _display->setFont(&FreeSans9pt7b);
    _display->setCursor(10, 50);
    _display->print(message);
    _display->display();
}

static int qr_version_for_alphanumeric(const char* str, int ecc_level)
{
    // QR Code alphanumeric capacity table [version-1][ecc_level]
    // ecc_level: 0=L(Low), 1=M(Medium), 2=Q(Quartile), 3=H(High)
    static const int capacity_table[40][4] = {
        // Ver  L    M    Q    H
        { 25, 20, 16, 10 }, // 1
        { 47, 38, 29, 20 }, // 2
        { 77, 61, 47, 35 }, // 3
        { 114, 90, 67, 50 }, // 4
        { 154, 122, 87, 64 }, // 5
        { 195, 154, 108, 84 }, // 6
        { 224, 178, 125, 93 }, // 7
        { 279, 221, 157, 122 }, // 8
        { 335, 262, 189, 143 }, // 9
        { 395, 311, 221, 174 }, // 10
        { 468, 366, 259, 200 }, // 11
        { 535, 419, 296, 227 }, // 12
        { 619, 483, 352, 259 }, // 13
        { 667, 528, 376, 283 }, // 14
        { 758, 600, 426, 321 }, // 15
        { 854, 656, 470, 365 }, // 16
        { 938, 734, 531, 408 }, // 17
        { 1046, 816, 574, 452 }, // 18
        { 1153, 909, 644, 493 }, // 19
        { 1249, 970, 702, 557 }, // 20
        { 1352, 1035, 742, 587 }, // 21
        { 1460, 1134, 823, 640 }, // 22
        { 1588, 1248, 890, 672 }, // 23
        { 1704, 1326, 963, 744 }, // 24
        { 1853, 1451, 1041, 779 }, // 25
        { 1990, 1542, 1094, 864 }, // 26
        { 2132, 1637, 1172, 910 }, // 27
        { 2223, 1732, 1263, 958 }, // 28
        { 2369, 1839, 1322, 1016 }, // 29
        { 2520, 1994, 1429, 1080 }, // 30
        { 2677, 2113, 1499, 1150 }, // 31
        { 2840, 2238, 1618, 1226 }, // 32
        { 3009, 2369, 1700, 1307 }, // 33
        { 3183, 2506, 1787, 1394 }, // 34
        { 3351, 2632, 1867, 1431 }, // 35
        { 3537, 2780, 1966, 1530 }, // 36
        { 3729, 2894, 2071, 1591 }, // 37
        { 3927, 3054, 2181, 1658 }, // 38
        { 4087, 3220, 2298, 1774 }, // 39
        { 4296, 3391, 2420, 1852 } // 40
    };

    if (str == NULL)
        return 1;

    size_t length = strlen(str);
    length += length >> 3;
    // Validate input parameters
    if (length <= 0)
        return -1;
    if (ecc_level < 0 || ecc_level > 3)
        return -1;

    // Find the minimum version that can handle the given length
    for (int version = 1; version <= 40; version++) {
        if (capacity_table[version - 1][ecc_level] >= length) {
            return version;
        }
    }

    // If we reach here, the string is too long for any QR code version
    return -1;
}

void EPaperDisplay::showWifiSetup(const char* ap_ssid)
{
    char* wifi_string = (char*)malloc(100);
    snprintf(wifi_string, 100, "WIFI:S:%s;T:nopass;;", ap_ssid);

    ESP_LOGI(TAG, "Qr content: %s", wifi_string);
    _clearDisplay();

    int qrVersion = qr_version_for_alphanumeric(wifi_string, QR_WIFI_ECC_TYPE);
    ESP_LOGI(TAG, "Drawing Qr code version %d with ECC%d", qrVersion, QR_WIFI_ECC_TYPE);
    QRCode qrcode;
    uint8_t* qrcodeData = (uint8_t*)malloc(qrcode_getBufferSize(AP_QR_CODE_VERSION));
    if (qrcodeData == NULL) {
        ESP_LOGE(TAG, "Could not allocate %d bytes for QrCode", qrcode_getBufferSize(AP_QR_CODE_VERSION));
    } else {

        qrcode_initText(&qrcode, qrcodeData, AP_QR_CODE_VERSION, QR_WIFI_ECC_TYPE, wifi_string);

        size_t scale = _display->width() / qrcode.size;
        if (scale > 4)
            scale = 4;

        int16_t x0 = (_display->width() - (qrcode.size * scale)) / 2; // Centered
        int16_t y0 = (_display->height() - 10) - (qrcode.size * scale);
        ESP_LOGE(TAG, "QrCode size: %d, scale %d, drawn at {%d; %d}", qrcode.size, scale, x0, y0);
        for (uint8_t y = 0; y < qrcode.size; y++) {
            for (uint8_t x = 0; x < qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    _display->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, EPD_BLACK);
                }
            }
        }
        free(qrcodeData);
    }

    free(wifi_string);

    _display->setFont(&FreeSansBold9pt7b);
    _display->setCursor(15, 20);
    _display->print("WiFi setup");
    _display->drawFastHLine(10, 25, _display->width() - 20, EPD_BLACK);

    _display->setFont(&FreeSans9pt7b);
    _display->setCursor(10, 50);
    _display->print("SSID:");
    _display->setCursor(10, 70);
    _display->print(ap_ssid);

    _display->setCursor(10, 95);
    _display->print("(no password)");

    _display->display();
}

void EPaperDisplay::_displayTask(void* pvParameters)
{
    EPaperDisplay* instance = (EPaperDisplay*)pvParameters;
    ESP_LOGI(TAG, "Display Task started.");

    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        instance->_updateDisplay();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
    }
}

void EPaperDisplay::forceUpdate()
{
    if (_displayTaskHandle == NULL) {
        ESP_LOGE(TAG, "Cannot force display update: Task Handle is NULL");
        return;
    }

    // Check if we are running inside an Interrupt Service Routine (ISR)
    if (xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(_displayTaskHandle, &xHigherPriorityTaskWoken);

        // If the display task has higher priority than the current task,
        // yield immediately so it runs right now.
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        // Standard Task-to-Task notification
        xTaskNotifyGive(_displayTaskHandle);
    }
}


void EPaperDisplay::_updateDisplay()
{
    char timeStr[20];
    char ipStr[16] = "N/A";
    float weight;
    std::string feedingStatus;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        strncpy(timeStr, _deviceState.formattedTime, sizeof(timeStr));
        if (_deviceState.ipAddress) {
            _deviceState.ipAddress.toString().toCharArray(ipStr, sizeof(ipStr));
        }
        weight        = _deviceState.currentWeight;
        feedingStatus = _deviceState.currentFeedingStatus;
        xSemaphoreGive(_mutex);
    } else {
        ESP_LOGE(TAG, "Could not get mutex to update display.");
        return;
    }

    _clearDisplay();
    _display->setFont(&Picopixel);
    _display->setTextSize(2);

    _display->setCursor(_display->width() - 32, 11);
    _display->print(_deviceState.batteryLevel);
    _display->print('%');


    _display->setTextSize(1);
    _display->setFont(&FreeSans9pt7b);

    // Header
    _display->setCursor(5, 15);
    _display->print(ipStr);
    _display->drawFastHLine(0, 22, _display->width(), EPD_BLACK);

    // Main Status
    _display->setCursor(5, 45);
    _display->print("Status: ");
    _display->print(feedingStatus.c_str());

    _display->setCursor(5, 70);
    _display->print("Weight: ");
    char weightStr[10];
    dtostrf(weight, 4, 1, weightStr);
    _display->print(weightStr);
    _display->print(" g");

    // Footer
    _display->drawFastHLine(0, _display->height() - 22, _display->width(), EPD_BLACK);
    _display->setCursor(5, _display->height() - 8);
    _display->print(timeStr);

    _display->display();
}