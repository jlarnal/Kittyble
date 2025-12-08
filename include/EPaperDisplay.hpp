#ifndef EPAPERDISPLAY_HPP
#define EPAPERDISPLAY_HPP

#include "DeviceState.hpp"
#include <SSD1680Driver.h>
#include <SPI.h>


/**
 * @file EPaperDisplay.hpp
 * @brief Manages the 2.6" E-Paper display using the Adafruit_EPD library
 * and updated to use the global DeviceState in portrait orientation.
 */

class EPaperDisplay {
  public:
    EPaperDisplay(DeviceState& deviceState, SemaphoreHandle_t& mutex);
    bool begin();
    void startTask();

    // Specific display screens, can be called from other modules
    void showBootScreen();
    // Corrected function signature for an open AP (no password)
    void showWifiSetup(const char* ap_ssid);
    void showStatus(const char* title, const char* message);
    void showError(const char* title, const char* message);

    void forceUpdate();

  private:
    DeviceState& _deviceState;
    SemaphoreHandle_t& _mutex;
    TaskHandle_t _displayTaskHandle;


    Ssd1680_Driver* _display;

    void _updateDisplay();
    static void _displayTask(void* pvParameters);

    // Helper methods for common display operations
    void _clearDisplay() { _display->clearBuffer(); /* _display->fillScreen(EPD_WHITE); */ }
};

#endif // EPAPERDISPLAY_HPP