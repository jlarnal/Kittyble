#include "TankManager.hpp"
#include "DeviceState.hpp"
#include "board_pinout.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstddef> // Required for offsetof
#include <esp_mac.h>
#include "ReedSolomon.hpp"

static const char* TAG = "TankManager";

TaskHandle_t TankManager::_runningTask;
static ReedSolomon<TankEEpromData_t::DATA_SIZE, TankEEpromData_t::ECC_SIZE> rs;


// Helper to convert a device address to a string for logging/UID.
std::string addressToString(const uint8_t* addr)
{
    char uid_str[17];
    sprintf(uid_str, "%02X%02X%02X%02X%02X%02X%02X%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
    return std::string(uid_str);
}

void TankEEpromData_t::printTo(Stream& stream, TankEEpromData_t* eeprom)
{
    if (eeprom == nullptr) {
        ESP_LOGE(TAG, "Null TankEEpromData_t argument given.");
        return;
    }

    stream.printf("lastBaseMAC48:  %02X%02X%02X%02X%02X%02X\r\n", eeprom->data.history.lastBaseMAC48[0], eeprom->data.history.lastBaseMAC48[1],
      eeprom->data.history.lastBaseMAC48[2], eeprom->data.history.lastBaseMAC48[3], eeprom->data.history.lastBaseMAC48[4],
      eeprom->data.history.lastBaseMAC48[5]);
    stream.flush();
    stream.printf("lastBusIndex:   %d\r\n", eeprom->data.history.lastBusIndex);
    stream.flush();
    stream.printf("nameLength:     %d\r\n", eeprom->data.nameLength);
    stream.flush();
    stream.printf("capacity:       %2.4f\r\n", TankManager::q3_13_to_double(eeprom->data.capacity));
    stream.flush();
    stream.printf("density:        %2.4f\r\n", TankManager::q2_14_to_double(eeprom->data.density));
    stream.flush();
    stream.printf("servoIdlePwm:   %d\r\n", eeprom->data.servoIdlePwm);
    stream.flush();
    stream.printf("remainingGrams: %d\r\n", eeprom->data.remainingGrams);
    stream.flush();

    { // shallow scope for the `nameCpy` array
        char* pChar             = eeprom->data.name;
        bool firstNull          = true;
        size_t consecutiveNulls = 0;
        stream.print("name: \"");
        for (int idx = 0; idx < TankEEpromData_t::NAME_FIELD_SIZE; idx++) {
            if (eeprom->data.name[idx]) {
                if (consecutiveNulls == 1) {
                    stream.print("<NULL>");
                } else if (consecutiveNulls) {
                    stream.printf("<NULL x%d>", consecutiveNulls);
                }
                stream.print(eeprom->data.name[idx]);
                stream.flush();
                consecutiveNulls = 0;
            } else {
                if (firstNull) {
                    firstNull = false;
                    stream.print('\"');
                } else {
                    consecutiveNulls++;
                }
            }
        }
    }
    stream.print("\r\n");
    stream.flush();
}

void TankEEpromData_t::finalize(TankEEpromData_t& eedata)
{
    // Use RS-FEC to encode the data
    // Message length = 128 (DATA_SIZE + ECC_SIZE)
    // Data length = 96
    // Parity length = 32
    rs.encode((uint8_t*)&eedata.data, eedata.ecc);
}

void TankEEpromData_t::format(TankEEpromData_t& eedata)
{
    memset(&eedata, 0, sizeof(TankEEpromData_t));

    // Initialize with defaults
    eedata.data.history.lastBusIndex = 0xFF; // No history
    const char* defaultName          = "New Tank";
    eedata.data.nameLength           = (uint8_t)strlen(defaultName) + 1; // Include null terminator
    strncpy(eedata.data.name, defaultName, TankEEpromData_t::NAME_FIELD_SIZE - 1);

    eedata.data.capacity       = 0;
    eedata.data.density        = 0;
    eedata.data.remainingGrams = 0;
    eedata.data.servoIdlePwm   = 1500; // Safe default

    // Finalize will handle ECC
}

bool TankEEpromData_t::sanitize(TankEEpromData_t& eedata)
{
    // 1. Attempt RS-FEC Decode
    int res = rs.decode((uint8_t*)&eedata.data, eedata.ecc);

    // If Decode returns non-zero, it means uncorrectable errors were found.
    if (res < 0) {
        return false;
    }

    // 2. Structural/Range Checks on the (potentially corrected) data
    bool structuralIntegrity = true;

    // Check ranges that would indicate corruption or fresh flash (0xFF)
    if (eedata.data.nameLength > TankEEpromData_t::NAME_FIELD_SIZE) {
        structuralIntegrity = false;
    }

    if (eedata.data.history.lastBusIndex > 6 && eedata.data.history.lastBusIndex != 0xFF)
        structuralIntegrity = false;

    // Check Servo PWM sanity (prevent damage)
    if (eedata.data.servoIdlePwm < 500 || eedata.data.servoIdlePwm > 2500)
        structuralIntegrity = false;

    return structuralIntegrity;
}


void TankManager::begin(uint16_t hopper_closed_pwm, uint16_t hopper_open_pwm)
{
    _hopperClosedPwm = hopper_closed_pwm;
    _hopperOpenPwm   = hopper_open_pwm;
    _isServoMode     = false;

    _swimuxMutex = xSemaphoreCreateRecursiveMutex();

    // Init PCA9685
    _pwm.begin();
    _pwm.setFull(-1, 0);

    // Configure pins and their respective default levels.
    _swiMux.begin();

    pinMode(SERVO_POWER_ENABLE_PIN, OUTPUT);
    digitalWrite(SERVO_POWER_ENABLE_PIN, HIGH); // Start with power off (HIGH for active-low)

    setServoPower(false);

    ESP_LOGI(TAG, "Initializing Tank Manager with SwiMux interface...");
    refresh();
}

void TankManager::_switchToSwiMode()
{
    _isServoMode = false;
    _pwm.setPWMFreq(50); // Low frequency for DC power
    PCA9685::I2C_Result_e res = _pwm.setFull(-1, true); // Set all channels full on (powers the 1-Wire EEPROMs through their pullup resistors)
    if (res) {
        ESP_LOGE(TAG, "PCA9685 \"all full on\" failed (I2C error #%d)", res);
    } else {
        ESP_LOGI(TAG, "PCA9685 switched to EEPROM power mode.");
    }
}


void TankManager::_switchToServoMode()
{
    _pwm.setPWMFreq(50); // Standard servo frequency
    _pwm.setFull(-1, false); // Set all channel to mute for now.
    // Set each connected tank pulse duration to its idle value.
    for (auto t : _knownTanks) {
        _pwm.writeMicroseconds(t.busIndex, t.servoIdlePwm);
    }
    // Wait for a full RC servo cycle to elapse.
    vTaskDelay(pdMS_TO_TICKS(20) + 1); // 20ms plus chaff.
    _isServoMode = true;
    ESP_LOGI(TAG, "PCA9685 switched to Servo PWM mode.");
}

void TankManager::_tankDetectionTask(void* pvParam)
{
    if (pvParam == nullptr)
        return;
    TankManager* pInst = (TankManager*)pvParam;
    RollCallArray_t currentUids;
    bool changesDetected = false;

    // Task loop
    while (1) {
        // We only check if NOT in servo mode.
        // We try to take the mutex; if the SwiMux is busy, we just skip this cycle.
        if (!pInst->_isServoMode) {
            if (xSemaphoreTakeRecursive(pInst->_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
                if (pInst->_swiMux.rollCall(currentUids) == SMREZ_OK) {
                    uint16_t changedBuses = 0;

                    // Compare each bus's UID against the previously known population.
                    for (int i = 0; i < NUMBER_OF_BUSES; i++) {
                        // Normalize: UINT64_MAX (no device) and 0 both mean "empty bus"
                        uint64_t currUid = (currentUids.bus[i] == UINT64_MAX) ? 0 : currentUids.bus[i];
                        uint64_t prevUid = (pInst->_lastKnownUids.bus[i] == UINT64_MAX) ? 0 : pInst->_lastKnownUids.bus[i];

                        if (currUid != prevUid) {
                            changedBuses |= (1 << i);
                        }
                        // Update stored UID (store the normalized value)
                        pInst->_lastKnownUids.bus[i] = currUid;
                    }

                    if (changedBuses != 0) {
                        changesDetected = true;
                        ESP_LOGI(TAG, "Tank population change detected on buses: 0x%02X", changedBuses);
                        // We are already holding the mutex, but refresh() expects to take it.
                        // Since it's a recursive mutex, this is fine.
                        pInst->refresh(changedBuses);
                        // Notify listeners (e.g., WebServer SSE) of tank population change.
                        if (pInst->_onTanksChangedCallback) {
                            pInst->_onTanksChangedCallback();
                        }
                    }
                }
                xSemaphoreGiveRecursive(pInst->_swimuxMutex);
            }
        }
        // Poll every second.
        if (changesDetected) {
            vTaskDelay(pdMS_TO_TICKS(3000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

bool TankInfo::fillFromEeprom(TankEEpromData_t& eeprom)
{
    // Warning: fillFromEeprom assumes the data is SANITIZED.
    // It will blindly read whatever fields are in there.
    // TankManager::refresh guarantees this by calling format() if sanitize() fails.

    name.clear();
    // Safety clamp on name length (nameLength includes the null terminator)
    uint8_t safeLen = std::min(eeprom.data.nameLength, (uint8_t)TankEEpromData_t::NAME_FIELD_SIZE);
    if (safeLen > 0) {
        // Ensure null termination for string construction
        char tempName[TankEEpromData_t::NAME_FIELD_SIZE + 1];
        memcpy(tempName, eeprom.data.name, safeLen);
        tempName[safeLen] = '\0';
        name              = std::string(tempName);
    } else {
        name = "";
    }

    capacityLiters = TankManager::q3_13_to_double(eeprom.data.capacity);
    kibbleDensity  = TankManager::q2_14_to_double(eeprom.data.density);
    // Remaining kibble (in kg in TankInfo, in 16-bits integer grams in eeprom)
    remaining_weight_kg = eeprom.data.remainingGrams * 1E-3;
    servoIdlePwm        = eeprom.data.servoIdlePwm;
    isFullInfo          = true;
    return true;
}


TankInfo::TankInfoDiscrepancies_e TankInfo::toTankData(TankEEpromData_t& eeprom)
{
    uint32_t result = TID_NONE;
    // name
    if (name.compare(0, TankEEpromData_t::NAME_FIELD_SIZE, (char*)&eeprom.data.name[0]) != 0) {
        result |= TID_NAME_CHANGED;
        // Copy the length-capped name string.
        size_t maxCopy = std::min(name.length(), (size_t)(TankEEpromData_t::NAME_FIELD_SIZE - 1));
        strncpy((char*)&eeprom.data.name[0], name.c_str(), maxCopy);
        eeprom.data.name[maxCopy] = '\0'; // Ensure null termination
        eeprom.data.nameLength    = maxCopy + 1; // Include null terminator in length
    }
    // bus index
    if (busIndex != eeprom.data.history.lastBusIndex) {
        result |= TID_BUSINDEX_CHANGED;
        eeprom.data.history.lastBusIndex = busIndex;
    }
    // MAC48 of last connected base .
    if (0 != memcmp(lastBaseMAC48, eeprom.data.history.lastBaseMAC48, 6)) {
        result |= TID_MAC_CHANGED;
        memcpy(eeprom.data.history.lastBaseMAC48, lastBaseMAC48, 6);
    }
    // Specs
    uint16_t qCap  = TankManager::double_to_q3_13(capacityLiters);
    uint16_t qDens = TankManager::double_to_q2_14(kibbleDensity);
    if (eeprom.data.servoIdlePwm != servoIdlePwm || eeprom.data.capacity != qCap || eeprom.data.density != qDens) {
        result |= TID_SPECS_CHANGED;
        eeprom.data.servoIdlePwm = servoIdlePwm;
        eeprom.data.capacity     = qCap;
        eeprom.data.density      = qDens;
    }
    // Remaining kibble (in grams in eeprom, in kg in TankInfo)
    uint32_t tankRemGrams = (uint32_t)std::abs(remaining_weight_kg * 1E3);
    if (eeprom.data.remainingGrams != tankRemGrams) {
        result |= TID_REMAINING_CHANGED;
        eeprom.data.remainingGrams = tankRemGrams;
    }

    // Compute eeprom ECC
    TankEEpromData_t::finalize(eeprom);

    return (TankInfoDiscrepancies_e)result;
}


void TankManager::refresh(uint16_t refreshMap)
{
    // Based on the code, we assume there are 6 buses.
    constexpr uint16_t allBusesMask = (1 << NUMBER_OF_BUSES) - 1;
    refreshMap &= allBusesMask;

    if (refreshMap == 0 || _isServoMode) {
        return;
    }

    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for refresh!");
        return;
    }

    // PHASE 1: Hardware Scan
    uint64_t foundUids[6] = { 0 };
    bool scannedBus[6]    = { false };

    if (refreshMap == allBusesMask) {
        RollCallArray_t presences;
        if (_swiMux.rollCall(presences) == SMREZ_OK) {
            for (int i = 0; i < 6; i++) {
                // Important: Map UINT64_MAX (Driver's "No Device") and 0 to Internal "Empty" (0)
                if (presences.bus[i] == UINT64_MAX || presences.bus[i] == 0) {
                    foundUids[i] = 0;
                } else {
                    foundUids[i] = presences.bus[i];
                }
                scannedBus[i] = true;
            }
        }
    } else {
        for (int i = 0; i < 6; i++) {
            if ((refreshMap >> i) & 1) {
                scannedBus[i]   = true;
                uint64_t uidVal = 0;
                if (_swiMux.getUid(i, uidVal) == SMREZ_OK) {
                    // Important: Map UINT64_MAX (Driver's "No Device") and 0 to Internal "Empty" (0)
                    if (uidVal == UINT64_MAX || uidVal == 0) {
                        foundUids[i] = 0;
                    } else {
                        foundUids[i] = uidVal;
                    }
                }
            }
        }
    }

    // PHASE 2: Reconcile Detached Tanks
    for (auto& tank : _knownTanks) {
        if (tank.busIndex >= 0 && tank.busIndex < 6 && scannedBus[tank.busIndex]) {
            if (tank.uid != foundUids[tank.busIndex]) {
                ESP_LOGI(TAG, "Tank 0x%016llX detached from bus %d", tank.uid, tank.busIndex);
                tank.busIndex = -1; // Mark as detached
            }
        }
    }

    // PHASE 3: Attach / Create Tanks
    for (int i = 0; i < 6; i++) {
        if (!scannedBus[i] || foundUids[i] == 0)
            continue;

        uint64_t uid = foundUids[i];

        auto it = std::find_if(_knownTanks.begin(), _knownTanks.end(), [uid](const TankInfo& t) { return t.uid == uid; });

        TankInfo* targetTank = nullptr;

        if (it != _knownTanks.end()) {
            // Found existing tank.
            if (it->busIndex != i) {
                ESP_LOGI(TAG, "Tank 0x%016llX moved to bus %d", uid, i);
                it->busIndex = i;
            }
            targetTank = &(*it);
        } else {
            // New Tank!
            ESP_LOGI(TAG, "New tank 0x%016llX discovered on bus %d", uid, i);
            _knownTanks.emplace_back();
            targetTank             = &_knownTanks.back();
            targetTank->uid        = uid;
            targetTank->busIndex   = i;
            targetTank->isFullInfo = false;
        }

        // If we don't have full info, try to read it now.
        if (targetTank && !targetTank->isFullInfo) {
            TankEEpromData_t data;
            uint8_t* eeData = reinterpret_cast<uint8_t*>(&data);

            if (_swiMux.read(i, eeData, 0, sizeof(TankEEpromData_t)) == SMREZ_OK) {

                // --- CRITICAL SECTION: VALIDATION & FORMATTING ---
                if (!TankEEpromData_t::sanitize(data)) {
                    ESP_LOGW(TAG, "Corrupt or uninitialized EEPROM detected on tank 0x%016llX. Formatting...", uid);

                    // Format memory structure to default "New Tank"
                    TankEEpromData_t::format(data);
                    TankEEpromData_t::finalize(data);

                    // Write formatted data back to the physical device
                    if (_swiMux.write(i, eeData, 0, sizeof(TankEEpromData_t)) == SMREZ_OK) {
                        ESP_LOGI(TAG, "Tank 0x%016llX successfully formatted.", uid);
                    } else {
                        ESP_LOGE(TAG, "Failed to write formatted data to tank 0x%016llX!", uid);
                        // We still proceed to fillFromEeprom so the webserver sees it as New,
                        // even if the write failed (it might succeed next time).
                    }
                }
                // --------------------------------------------------

                targetTank->fillFromEeprom(data);
            }
        }
    }

    // PHASE 4: Garbage Collection
    auto newEnd = std::remove_if(_knownTanks.begin(), _knownTanks.end(), [](const TankInfo& t) { return t.busIndex == -1; });

    if (newEnd != _knownTanks.end()) {
        ESP_LOGI(TAG, "Garbage collecting %d disconnected tanks.", std::distance(newEnd, _knownTanks.end()));
        _knownTanks.erase(newEnd, _knownTanks.end());
    }

    // Update the global state so other modules (WebServer, etc.) see the changes
    if (xSemaphoreTake(_deviceStateMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
        _deviceState.connectedTanks = _knownTanks;
        xSemaphoreGive(_deviceStateMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire DeviceState mutex to update connected tanks!");
    }

    xSemaphoreGiveRecursive(_swimuxMutex);
}



bool TankManager::updateEeprom(TankEEpromData_t& data, TankInfo::TankInfoDiscrepancies_e updatesNeeded, int8_t forcedBusIndex)
{
    if (_isServoMode)
        return false;
    // Determine the bus index to use. If forcedBusIndex is provided (>=0), use it.
    // Otherwise, use the last known bus index from the data structure.
    uint8_t busIndex = (forcedBusIndex >= 0) ? forcedBusIndex : data.data.history.lastBusIndex;
    busIndex %= 6;

    // If there's nothing to update, we can return immediately.
    if (updatesNeeded == TankInfo::TID_NONE) {
        return true;
    }

    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for updateEeprom!");
        return false;
    }

    // --- Write the whole data in one fell swoop.

    uint8_t* eepromBytes = reinterpret_cast<uint8_t*>(&data);
    TankEEpromData_t::finalize(data); // prepare the data to be transfered to eeprom (duplicate and crc32 addition)

    // If any of the header/numeric fields changed, perform a single coalesced write.
    if (SMREZ_OK != _swiMux.write(busIndex, eepromBytes, 0, sizeof(TankEEpromData_t))) {
        ESP_LOGE(TAG, "Failed to write memory of tank #%d", busIndex);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    xSemaphoreGiveRecursive(_swimuxMutex);
    return true;
}

TankInfo* TankManager::getKnownTankOfUis(uint64_t uid)
{
    // Use 'auto&' to get a reference to the element
    for (auto& tank : _knownTanks) {
        if (tank.uid == uid) {
            return &tank;
        }
    }
    return nullptr; // It's good practice to return nullptr if nothing is found
}

TankInfo* TankManager::getKnownTankOfBus(uint8_t busIndex)
{
    // Use 'auto&' to get a reference to the element
    for (auto& tank : _knownTanks) {
        if (tank.busIndex == busIndex) {
            return &tank;
        }
    }
    return nullptr; // It's good practice to return nullptr if nothing is found
}

void TankManager::removeKnownTank(TankInfo* tankToRemove)
{
    if (tankToRemove == nullptr)
        return;

    // Check validity of pointer before removal attempt (safety measure)
    bool isValid = false;
    for (const auto& t : _knownTanks) {
        if (&t == tankToRemove) {
            isValid = true;
            break;
        }
    }
    if (!isValid)
        return;

    // 2. Use std::remove_if to move the target element to the end of the vector.
    // The lambda function identifies the element by comparing its memory address.
    auto newEnd = std::remove_if(_knownTanks.begin(), _knownTanks.end(), [&](const TankInfo& tank) { return &tank == tankToRemove; });

    // 3. Actually erase the element(s) from the vector.
    _knownTanks.erase(newEnd, _knownTanks.end());
}

int8_t TankManager::getBusOfTank(const uint64_t tankUid)
{
    if (_isServoMode) {
        ESP_LOGE(TAG, "Call to `TankManager::getBusOfTank` while in servo mode.");
        return -1;
    }
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for getBusOfTank!");
        return -1;
    }

    refresh(); // Refresh list to get current state

    int8_t foundIndex = -1;
    for (const auto& tank : _knownTanks) {
        if (tank.uid == tankUid) {
            foundIndex = tank.busIndex;
            break;
        }
    }

    xSemaphoreGiveRecursive(_swimuxMutex);
    return foundIndex;
}

// Handles updating the tank's configuration in its EEPROM.
bool TankManager::commitTankInfo(const TankInfo& tankInfo)
{
    if (_isServoMode) {
        ESP_LOGE(TAG, "Call to commitTankInfo while in servo mode");
        return false;
    }
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for commitTankInfo!");
        return false;
    }



    // 1. Find the bus where the tank is currently connected.
    int8_t busIndex = getBusOfTank(tankInfo.uid);
    if (busIndex < 0) {
        ESP_LOGE(TAG, "commitTankInfo: Tank with UID 0x%016llX not found.", tankInfo.uid);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }



    // 2. Read the current data from the EEPROM to establish a baseline.
    TankEEpromData_t currentEepromData;
    uint8_t* eeData = reinterpret_cast<uint8_t*>(&currentEepromData);
    if (SMREZ_OK != _swiMux.read(busIndex, eeData, 0, sizeof(TankEEpromData_t))) {
        ESP_LOGE(TAG, "commitTankInfo: Failed to read from tank on bus %d", busIndex);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }


    // 3. Create a mutable copy to call toTankData, which modifies the eeprom struct
    // and returns flags indicating what has changed.
    TankInfo mutableTankInfo                      = tankInfo;
    mutableTankInfo.busIndex                      = busIndex; // Ensure the busIndex is up-to-date for comparison.
    TankInfo::TankInfoDiscrepancies_e changesMade = mutableTankInfo.toTankData(currentEepromData);

    // 4. If there are changes, write them back using our optimized method.
    if (changesMade != TankInfo::TID_NONE) {
        ESP_LOGI(TAG, "Committing changes (flags: 0x%X) to tank 0x%016llX on bus %d", (uint32_t)changesMade, tankInfo.uid, busIndex);
        // Copy this ESP32 6 lower bytes of MAC48 into the tankInfo struct for future reference.
        { // inside a scope for baseMac, let's not consume more stack than needed
            uint8_t baseMac[6];
            esp_efuse_mac_get_default(baseMac);
            memcpy(mutableTankInfo.lastBaseMAC48, baseMac, 6);
        }
        // Check if write was successful
        if (updateEeprom(currentEepromData, changesMade, busIndex)) {

            // 1. Update the local cache (_knownTanks)
            for (auto& t : _knownTanks) {
                if (t.uid == tankInfo.uid) {
                    // Update the cached object with the new data
                    // We preserve the isFullInfo flag and ensure busIndex is correct
                    bool wasFull = t.isFullInfo;
                    t            = tankInfo;
                    t.busIndex   = busIndex;
                    t.isFullInfo = wasFull;
                    break;
                }
            }

            // 2. Update the Global State (so the UI sees the new name immediately)
            if (xSemaphoreTake(_deviceStateMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
                for (auto& t : _deviceState.connectedTanks) {
                    if (t.uid == tankInfo.uid) {
                        bool wasFull = t.isFullInfo;
                        t            = tankInfo;
                        t.busIndex   = busIndex;
                        t.isFullInfo = wasFull;
                        break;
                    }
                }
                xSemaphoreGive(_deviceStateMutex);
            }
        }
    } else {
        ESP_LOGI(TAG, "No changes to commit for tank 0x%016llX", tankInfo.uid);
    }


    xSemaphoreGiveRecursive(_swimuxMutex);
    return true;
}

bool TankManager::refreshTankInfo(TankInfo& tankInfo)
{
    if (_isServoMode) {
        ESP_LOGE(TAG, "Call to refreshTankInfo while in servo mode.");
        return false;
    }
    // 1. The UID must be provided in the tankInfo struct.
    if (tankInfo.uid == 0) {
        ESP_LOGE(TAG, "refreshTankInfo: UID must be provided.");
        return false;
    }

    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for refreshTankInfo!");
        return false;
    }

    // 2. Find which bus the tank is on. This also refreshes the presence list.
    int8_t busIndex = getBusOfTank(tankInfo.uid);
    if (busIndex < 0) {
        ESP_LOGW(TAG, "refreshTankInfo: Tank with UID 0x%016llX not found.", tankInfo.uid);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    // 3. Read the entire EEPROM data block from the tank.
    TankEEpromData_t eepromData;
    uint8_t* eeData = reinterpret_cast<uint8_t*>(&eepromData);
    if (SMREZ_OK != _swiMux.read(busIndex, eeData, 0, sizeof(TankEEpromData_t))) {
        ESP_LOGE(TAG, "refreshTankInfo: Failed to read from tank on bus %d", busIndex);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    // 4. Populate the passed-in TankInfo object with the data from the EEPROM.
    tankInfo.fillFromEeprom(eepromData);
    // fillFromEeprom doesn't set the busIndex, so we must set it manually.
    tankInfo.busIndex = busIndex;

    ESP_LOGI(TAG, "Refreshed info for tank 0x%016llX on bus %d", tankInfo.uid, busIndex);
    xSemaphoreGiveRecursive(_swimuxMutex);
    return true;
}

bool TankManager::updateRemaingKibble(const uint64_t uid, uint16_t newRemainingGrams)
{
    if (_isServoMode) {
        ESP_LOGE(TAG, "Call to updateRemaingKibble while in servo mode.");
        return false;
    }
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire SwiMux mutex for updateRemaingKibble!");
        return false;
    }

    // 1. Find the tank. This also refreshes the presence list.
    int8_t busIndex = getBusOfTank(uid);
    if (busIndex < 0) {
        ESP_LOGE(TAG, "updateRemaingKibble: Tank with UID 0x%016llX not found.", uid);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    // 2. Update the local in-memory cache (_knownTanks).
    bool foundInCache = false;
    for (auto& tank : _knownTanks) {
        if (tank.uid == uid) {
            tank.remaining_weight_kg = (double)newRemainingGrams / 1000.0;
            foundInCache             = true;
            break;
        }
    }
    if (!foundInCache) {
        ESP_LOGW(TAG, "updateRemaingKibble: Tank 0x%016llX found on bus but not in cache.", uid);
    }

    // Sync this specific change to global state
    if (xSemaphoreTake(_deviceStateMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
        for (auto& t : _deviceState.connectedTanks) {
            if (t.uid == uid) {
                t.remaining_weight_kg = (double)newRemainingGrams / 1000.0;
                break;
            }
        }
        xSemaphoreGive(_deviceStateMutex);
    }

    // --- Safe path: full read/modify/finalize/write ---
    TankEEpromData_t eedata;
    uint8_t* eeBytes = reinterpret_cast<uint8_t*>(&eedata);

    if (SMREZ_OK != _swiMux.read(busIndex, eeBytes, 0, sizeof(TankEEpromData_t))) {
        ESP_LOGE(TAG, "updateRemaingKibble: Failed to read EEPROM of tank 0x%016llX", uid);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    // Update record
    eedata.data.remainingGrams = newRemainingGrams;

    // Encode ECC
    TankEEpromData_t::finalize(eedata);

    if (SMREZ_OK != _swiMux.write(busIndex, eeBytes, 0, sizeof(TankEEpromData_t))) {
        ESP_LOGE(TAG, "updateRemaingKibble: Failed to write EEPROM of tank 0x%016llX", uid);
        xSemaphoreGiveRecursive(_swimuxMutex);
        return false;
    }

    ESP_LOGI(TAG, "Updated remaining kibble (safe path) for tank 0x%016llX to %d g.", uid, newRemainingGrams);
    xSemaphoreGiveRecursive(_swimuxMutex);
    return true;
}

// --- Servo Control Implementation ---
void TankManager::setServoPower(bool on)
{
    if (on) {
        _switchToServoMode();
    } else {
        _switchToSwiMode();
    }
    digitalWrite(SERVO_POWER_ENABLE_PIN, on ? LOW : HIGH);
    ESP_LOGI(TAG, "Servo power %s", on ? "ON" : "OFF");
}

PCA9685::I2C_Result_e TankManager::setServoPWM(uint8_t servoNum, uint16_t pwm)
{
    if (servoNum >= NUMBER_OF_BUSES)
        return PCA9685::I2C_Result_e::I2C_Unknown;
    if (!_isServoMode)
        _switchToServoMode();

    uint16_t ticks = map(pwm, 0, 20000, 0, 4095);
    return _pwm.setPWM(servoNum, 0, ticks);
}

PCA9685::I2C_Result_e TankManager::setContinuousServo(uint8_t servoNum, float speed)
{
    if (!_isServoMode) {
        ESP_LOGI(TAG, "Switching out of SWI mode to set continuous servo speed.");
        _switchToServoMode();
    }

    if (speed > 1.0)
        speed = 1.0;
    if (speed < -1.0)
        speed = -1.0;

    if (abs(speed) < 0.01) {
        return setServoPWM(servoNum, SERVO_CONTINUOUS_STOP_PWM);
    } else if (speed > 0) {
        uint16_t pwm = map(speed * 100, 0, 100, SERVO_CONTINUOUS_STOP_PWM, SERVO_CONTINUOUS_FWD_PWM);
        return setServoPWM(servoNum, pwm);
    } else {
        uint16_t pwm = map(speed * 100, -100, 0, SERVO_CONTINUOUS_REV_PWM, SERVO_CONTINUOUS_STOP_PWM);
        return setServoPWM(servoNum, pwm);
    }
}

PCA9685::I2C_Result_e TankManager::stopAllServos()
{
    if (!_isServoMode) {
        ESP_LOGI(TAG, "Switching out of SWI mode to stop all Servos.");
        _switchToServoMode(); // Ensure we are in a mode where we can send stop commands
    }

    // 1. Command all servos to their neutral/stop position
    PCA9685::I2C_Result_e result = PCA9685::I2C_Result_e::I2C_Success;
    for (uint8_t i = 0; i < 16; i++) {
        PCA9685::I2C_Result_e res = setContinuousServo(i, 0.0);
        if (!result && res)
            result = res;
    }

    // 2. CRITICAL: Wait a moment for the servos to physically stop moving
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Now, safely cut the power and switch the PCA9685 back to SWI mode
    setServoPower(false);

    ESP_LOGW(TAG, "All servos stopped and powered off.");
    return result;
}

void TankManager::setOnTanksChangedCallback(std::function<void()> cb)
{
    _onTanksChangedCallback = cb;
}

void TankManager::printConnectedTanks(Stream& stream)
{
    stream.println("=== CONNECTED TANKS ===");
    stream.println();

    if (_knownTanks.empty()) {
        stream.println("  (no tanks connected)");
        stream.println();
        stream.println("=== END TANKS ===");
        return;
    }

    stream.printf("  Total: %d tank(s)\r\n", (int)_knownTanks.size());
    stream.println();

    for (size_t i = 0; i < _knownTanks.size(); i++) {
        const TankInfo& tank = _knownTanks[i];
        stream.printf("--- Tank %d ---\r\n", (int)i);
        stream.printf("  UID:              %016llX\r\n", (unsigned long long)tank.uid);
        stream.printf("  Name:             %s\r\n", tank.name.c_str());
        stream.printf("  Bus Index:        %d\r\n", (int)tank.busIndex);
        stream.printf("  Full Info:        %s\r\n", tank.isFullInfo ? "yes" : "no");

        if (tank.isFullInfo) {
            stream.printf("  Capacity:         %.3f L\r\n", tank.capacityLiters);
            stream.printf("  Density:          %.3f kg/L\r\n", tank.kibbleDensity);
            stream.printf("  Remaining:        %.3f kg (%.0f g)\r\n", tank.remaining_weight_kg, tank.remaining_weight_kg * 1000.0);
            stream.printf("  Servo Idle PWM:   %u\r\n", tank.servoIdlePwm);

            // Calculate and show fill percentage if capacity is set
            if (tank.capacityLiters > 0 && tank.kibbleDensity > 0) {
                double maxKg = tank.capacityLiters * tank.kibbleDensity;
                double fillPercent = (tank.remaining_weight_kg / maxKg) * 100.0;
                stream.printf("  Fill Level:       %.1f%%\r\n", fillPercent);
            }

            // Show last connected base MAC
            stream.printf("  Last Base MAC:    %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                tank.lastBaseMAC48[0], tank.lastBaseMAC48[1], tank.lastBaseMAC48[2],
                tank.lastBaseMAC48[3], tank.lastBaseMAC48[4], tank.lastBaseMAC48[5]);
        }
        stream.println();
        stream.flush();
    }

    stream.println("=== END TANKS ===");
}

#pragma region Test methods for debug, to be commented out upon release

#ifdef KIBBLET5_DEBUG_ENABLED

SwiMuxPresenceReport_t TankManager::testSwiMuxAwaken()
{

    ESP_LOGI(TAG, "Poking SwiMux...\n");
    SwiMuxPresenceReport_t res;
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {

        res = _swiMux.getPresence(3000);
        xSemaphoreGiveRecursive(_swimuxMutex);
        if (res.busesCount > 0) {
            ESP_LOGI(TAG, "SwiMux awakened, %d buses, %d connected, map: 0x%04X", res.busesCount, __builtin_popcount(res.presences), res.presences);
        } else {
            ESP_LOGI(TAG, "Awakening of SwiMux FAILED !");
        }
    } else {
        ESP_LOGE(TAG, "Error: Could not acquire SwiMux mutex for test.");
    }

    return res;
}

bool TankManager::testSwiMuxSleep()
{

    ESP_LOGI(TAG, "Putting SwiMux to sleep.\n");

    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {

        bool result = _swiMux.sleep();
        xSemaphoreGiveRecursive(_swimuxMutex);
        ESP_LOGI(TAG, "Putting SwiMux to sleep %s", (result ? "successful." : "FAILED !"));
        return result;
    } else {
        ESP_LOGE(TAG, "Error: Could not acquire SwiMux mutex for test.");
    }

    return false;
}

bool TankManager::testSwiBusUID(uint8_t index, uint64_t& result)
{
    ESP_LOGI(TAG, "Getting UID from bus %d...", index % 6);

    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {

        SwiMuxSerialResult_e res = _swiMux.getUid(index % 6, result);

        xSemaphoreGiveRecursive(_swimuxMutex);
        if (res == SMREZ_OK)
            return true;
        ESP_LOGD(TAG, "UID acquisition failed (%s)", SwiMuxSerial_t::getSwiMuxErrorString(res));
    } else {
        ESP_LOGE(TAG, "Error: Could not acquire SwiMux mutex for test.");
    }

    return false;
}

bool TankManager::testRollCall(RollCallArray_t& results)
{

    SwiMuxSerialResult_e res = _swiMux.rollCall(results);
    if (res != SwiMuxSerialResult_e::SMREZ_OK) {
        ESP_LOGD(TAG, "rollCall failed with error %d", res);
        return false;
    } else {
        ESP_LOGD(TAG, "rollCall succeeded with result %d", res);
    }
    return true;
}

SwiMuxSerialResult_e TankManager::testSwiRead(uint8_t busIndex, uint16_t address, uint8_t* dataOut, uint16_t length)
{
    return _swiMux.read(busIndex, dataOut, address & 0xFF, length);
}

SwiMuxSerialResult_e TankManager::testSwiWrite(uint8_t busIndex, uint16_t address, const uint8_t* dataIn, uint16_t length)
{
    return _swiMux.write(busIndex, dataIn, address & 0xFF, length);
}

/**
 * @brief Test the formatting of a memory on a designated bus. 
 * @param index Index of the bus to format the memory on.
 * @return A value of the SwiMuxSerialResult_e enum.
 */
SwiMuxSerialResult_e TankManager::testFormat(uint8_t index)
{
    if (index < 0 || index >= NUMBER_OF_BUSES) {
        ESP_LOGE(TAG, "TankManager::testFormat called with invalid argument value (%d)", index);
        return SMREZ_BUS_INDEX_OUT_OF_RANGE;
    }
    TankEEpromData_t data;
    TankEEpromData_t::format(data);
    TankEEpromData_t::finalize(data);
    SwiMuxSerialResult_e res = SMREZ_MutexAcquisition;
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
        res = _swiMux.write(index, (const uint8_t*)&data, 0, sizeof(TankEEpromData_t));
        xSemaphoreGiveRecursive(_swimuxMutex);
    }

    return res;
}

SwiMuxSerialResult_e TankManager::testSwiMuxECC(uint8_t index, int& correctedCount)
{
    if (index < 0 || index >= NUMBER_OF_BUSES) {
        ESP_LOGE(TAG, "TankManager::testSwiMuxECC called with invalid argument value (%d)", index);
        return SMREZ_BUS_INDEX_OUT_OF_RANGE;
    }
    correctedCount = 0;
    TankEEpromData_t eeprom;
    SwiMuxSerialResult_e result = SMREZ_MutexAcquisition;
    if (xSemaphoreTakeRecursive(_swimuxMutex, MUTEX_ACQUISITION_TIMEOUT) == pdTRUE) {
        result = _swiMux.read(index, (uint8_t*)&eeprom, 0, sizeof(TankEEpromData_t));
        if (result != SMREZ_OK) {
            return result;
        }
        // Now to do the ecc
        correctedCount = rs.decode((uint8_t*)&eeprom.data, eeprom.ecc);

        xSemaphoreGiveRecursive(_swimuxMutex);
    }
    return result;
}

#endif // KIBBLET5_DEBUG_ENABLED

#pragma endregion
