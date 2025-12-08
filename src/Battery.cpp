/*
 Battery.cpp - Battery library
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

#include "Battery.h"
#include <math.h>

uint8_t linear(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage)
{
    if (voltage <= minVoltage) {
        return 0;
    } else if (voltage >= maxVoltage) {
        return 100;
    } else {
        return map(voltage, minVoltage, maxVoltage, 0, 100);
    }
}

uint8_t sigmoidal(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage)
{
    if (voltage <= minVoltage) {
        return 0;
    } else if (voltage >= maxVoltage) {
        return 100;
    } else {
        // slow start, fast middle, slow end
        return 105 - (105 / (1 + pow(1.724 * (voltage - minVoltage) / (maxVoltage - minVoltage), 10)));
    }
}

uint8_t asigmoidal(uint16_t voltage, uint16_t minVoltage, uint16_t maxVoltage)
{
    if (voltage <= minVoltage) {
        return 0;
    } else if (voltage >= maxVoltage) {
        return 100;
    } else {
        // steep start, slow middle, fast end
        return 105 - (105 / (1 + pow(1.33 * (voltage - minVoltage) / (maxVoltage - minVoltage), 4.5)));
    }
}

Battery::Battery(uint16_t minVoltage, uint16_t maxVoltage, uint8_t sensePin, uint8_t adcBits, uint16_t averaging_samples)
{
    _minVoltage    = minVoltage;
    _maxVoltage    = maxVoltage;
    _sensePin      = sensePin;
    _adcBits       = adcBits;
    _refVoltage    = 5000;
    _dividerRatio  = 1.0;
    _activationPin = 0;

    _averaging_samples = averaging_samples;
    if (_averaging_samples > 0) {
        _window = (uint16_t*)malloc(_averaging_samples * sizeof(uint16_t));
        if (_window) {
            memset(_window, 0, _averaging_samples * sizeof(uint16_t));
        }
    } else {
        _window = NULL;
    }
    _windowIndex = 0;
    _accumulator = 0;
}

Battery::~Battery()
{
    if (_window) {
        free(_window);
        _window = NULL;
    }
}

void Battery::begin(uint16_t refVoltage, float dividerRatio, mapFn_t mapFunc)
{
    _refVoltage   = refVoltage;
    _dividerRatio = dividerRatio;
    _mapFunc      = mapFunc;
    pinMode(_sensePin, INPUT);
}

void Battery::onDemand(uint8_t activationPin, uint8_t activationMode)
{
    _activationPin  = activationPin;
    _activationMode = activationMode;
    pinMode(_activationPin, OUTPUT);
}

uint16_t Battery::voltage()
{
    // Default single read behavior or small average (standard library does 1 read usually, or small set)
    if (_activationPin)
        digitalWrite(_activationPin, _activationMode);
    uint16_t value = analogRead(_sensePin);
    if (_activationPin)
        digitalWrite(_activationPin, !_activationMode);

    return value * _refVoltage / (1 << _adcBits) * _dividerRatio;
}

uint16_t Battery::voltageFast(uint16_t samples)
{
    if (samples == 0)
        samples = 1;

    if (_activationPin)
        digitalWrite(_activationPin, _activationMode);

    uint32_t sum = 0;
    for (uint16_t i = 0; i < samples; i++) {
        sum += analogRead(_sensePin);
    }

    if (_activationPin)
        digitalWrite(_activationPin, !_activationMode);

    // Calculate average raw value then convert to voltage
    // (sum / samples) * ref / adcMax * ratio
    // Rearrange to maintain precision: (sum * ref * ratio) / (samples * adcMax)
    // Using floats for ratio application to ensure accuracy
    float v = (float)sum / samples;
    return (uint16_t)(v * _refVoltage / (1 << _adcBits) * _dividerRatio);
}

uint8_t Battery::level(uint16_t voltage)
{
    if (voltage == 0) {
        voltage = this->voltage();
    }
    if (_mapFunc) {
        return _mapFunc(voltage, _minVoltage, _maxVoltage);
    }
    // Fallback if no map function provided (defaults to linear logic)
    if (voltage <= _minVoltage) {
        return 0;
    } else if (voltage >= _maxVoltage) {
        return 100;
    } else {
        return map(voltage, _minVoltage, _maxVoltage, 0, 100);
    }
}

void Battery::refreshAverage()
{
    if (!_window || _averaging_samples == 0) {
        return;
    }

    // 1. Get single sample (which is itself an average of 100 fast reads)
    uint16_t newSample = voltageFast(100);

    // 2. Rolling Average Update
    _accumulator -= _window[_windowIndex]; // Remove oldest
    _window[_windowIndex] = newSample; // Insert newest
    _accumulator += newSample; // Add newest

    // Move index
    _windowIndex++;
    if (_windowIndex >= _averaging_samples) {
        _windowIndex = 0;
    }
}

void Battery::getAverages(uint16_t* milliVoltsOut, uint8_t* levelOut)
{
    if (milliVoltsOut == NULL && levelOut == NULL)
        return;
    if (_averaging_samples == 0) {
        if (milliVoltsOut)
            *milliVoltsOut = 0;
        if (levelOut)
            *levelOut = 0;
        return;
    }
    // Note: If buffer is not full (e.g. startup), this will return a lower value
    // until the window is filled, because we initialized with 0.
    uint16_t mvolts = (uint16_t)(_accumulator / _averaging_samples);
    if (milliVoltsOut)
        *milliVoltsOut = mvolts;
    if (levelOut) {
        if (mvolts <= _minVoltage) {
            *levelOut = 0;
        } else if (mvolts >= _maxVoltage) {
            *levelOut = 100;
        } else {
            *levelOut = map(mvolts, _minVoltage, _maxVoltage, 0, 100);
        }
    }
}