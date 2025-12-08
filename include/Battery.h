/*
 Battery.h - Battery library
 Copyright (c) 2014 Roberto Lo Giacco.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as 
 published by the Free Software Foundation, either version 3 of the 
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include <Arduino.h>

typedef uint8_t (*mapFn_t)(uint16_t, uint16_t, uint16_t);

// Standard mapping functions provided by the library
uint8_t linear(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage);
uint8_t sigmoidal(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage);
uint8_t asigmoidal(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage);

class Battery {
  public:
    /**
		 * Creates an instance to monitor battery voltage and level.
		 * Initialization parameters depend on battery type and configuration.
		 *
		 * @param minVoltage is the voltage, expressed in millivolts, corresponding to an empty battery
		 * @param maxVoltage is the voltage, expressed in millivolts, corresponding to a full battery
		 * @param sensePin is the analog pin used for sensing the battery voltage
		 * @param adcBits is the number of bits the ADC uses (defaults to 10)
     * @param averaging_samples is the number of samples for the rolling average window (defaults to 10)
		 */
    Battery(uint16_t minVoltage, uint16_t maxVoltage, uint8_t sensePin, uint8_t adcBits = 10, uint16_t averaging_samples = 10);
    
    /**
     * Destructor to free the averaging window buffer.
     */
    ~Battery();

    /**
		 * Initializes the library by optionally setting additional parameters.
		 * To obtain the best results use a calibrated reference using the VoltageReference library or equivalent.
		 * * @param refVoltage is the board reference voltage, expressed in millivolts
		 * @param dividerRatio is the multiplier used to obtain the real battery voltage
		 * @param mapFunction is a pointer to the function used to map the battery voltage to the remaining capacity percentage (defaults to linear mapping)
		 */
    void begin(uint16_t refVoltage, float dividerRatio, mapFn_t = 0);

    /**
		 * Enables on-demand activation of the sensing circuit to limit battery consumption.
		 *
		 * @param activationPin is the pin which will be turned HIGH or LOW before starting the battery sensing
		 * @param activationMode is the mode (HIGH or LOW) to activate the sensing circuit (defaults to HIGH)
		 */
    void onDemand(uint8_t activationPin, uint8_t activationMode = HIGH);

    /**
		 * Reads the battery voltage.
		 *
		 * @return the battery voltage expressed in millivolts
		 */
    uint16_t voltage();

    /**
		 * Reads the remaining battery capacity.
		 *
		 * @param voltage is the battery voltage, if not provided it will be read from the sensor
		 * @return the remaining battery capacity expressed in percentage (0-100)
		 */
    uint8_t level(uint16_t voltage = 0);
    
    /**
     * Updates the rolling average by taking a new sample using voltageFast(100).
     */
    void refreshAverage();

    /**
     * @brief Gets the current rolling average voltage and level of the battery.
     *
     * @param[out] milliVoltsOut Pointer to a value receiving the average voltage of the battery, expressed in milliVolts.
     * @param[out] levelOut Pointer to a value receiving the average level [0..100] of the battery.
     */    
    void getAverages(uint16_t *milliVoltsOut, uint8_t *levelOut);

  private:
    /**
     * Reads the battery voltage by averaging a specific number of samples immediately.
     * * @param samples the number of samples to average
     * @return the battery voltage expressed in millivolts
     */
    uint16_t voltageFast(uint16_t samples);

    uint16_t _minVoltage;
    uint16_t _maxVoltage;
    uint8_t _sensePin;
    uint8_t _adcBits;
    uint16_t _refVoltage;
    float _dividerRatio;
    mapFn_t _mapFunc;
    uint8_t _activationPin;
    uint8_t _activationMode;
    
    // Rolling average variables
    uint16_t _averaging_samples;
    uint16_t *_window;
    uint16_t _windowIndex;
    uint64_t _accumulator;
};

#endif