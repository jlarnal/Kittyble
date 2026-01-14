#ifndef H_EPD_PINOUT_H
#define H_EPD_PINOUT_H

#define EDP_MOSI   (23)
#define EPD_CLK    (18)
#define EPD_DC     (17)
#define EPD_CS     (5)
#define EPD_BUSY   (4)
#define EPD_RST    (16)

#define TWEETER_PIN (25)

#define BATT_HALFV_PIN (35)

// Apart from the custom "power" pin, the PCA9685 is controlled using the default I2C pinout.
#define SERVO_POWER_ENABLE_PIN (33)

// Dallas 1-Wire EEPROMs (DS28E07/DS2431+, 1Kb each) are multiplexed through a CH32V003 slave
// MCU named "SwiMux" (legacy name) and accessed through the SwiMuxSerial adapter class via UART.
// This allows for up to 6 tanks, each on its own bus for identification.
#define SWIMUX_SERIAL_DEVICE Serial2
#define SWIMUX_TX_PIN (27)
#define SWIMUX_RX_PIN (13)


// Those two pins allow for communication with the HX711 strain gauge ADC.
#define HX711_DATA_PIN  (15)
#define HX711_CLOCK_PIN (14)

#endif // H_EPD_PINOUT_H

