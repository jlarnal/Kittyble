#ifndef TANKMANAGER_HPP
#define TANKMANAGER_HPP

#include <string>
#include <vector>
#include <functional>
#include "PCA9685.h"
#include "freertos/semphr.h"
#include "board_pinout.h"
#include "SwiMuxSerial.h"


// Forward-declare DeviceState to break circular dependency.
struct DeviceState;

// Constants for continuous rotation servos
#define SERVO_CONTINUOUS_STOP_PWM 1500
#define SERVO_CONTINUOUS_FWD_PWM  2000
#define SERVO_CONTINUOUS_REV_PWM  1000

// Default values for the hopper servo if no calibration is found
#define DEFAULT_HOPPER_CLOSED_PWM 1000
#define DEFAULT_HOPPER_OPEN_PWM   2000

// Hopper servo is on PCA9685 channel 6 (tank augers use 0-5)
#define HOPPER_SERVO_INDEX (NUMBER_OF_BUSES)
#define TOTAL_SERVO_COUNT  (NUMBER_OF_BUSES + 1)



/** @brief Internal record structure for Tank EEPROM */
struct __attribute__((packed)) TankHistory_t {
    uint8_t lastBaseMAC48[6];
    uint8_t lastBusIndex;
};

/** @brief Main data section of the Tank EEPROM */
struct __attribute__((packed)) TankEEpromRecordData_t {
    TankHistory_t history;
    uint8_t nameLength;
    uint16_t capacity; // Tank capacity in milliliters (mL)
    uint16_t density; // Kibble density in grams per liter (g/L)
    uint16_t servoIdlePwm;
    uint16_t remainingGrams;
    char name[80];
};

/** @brief Complete data structure including ECC */
struct __attribute__((packed)) TankEEpromData_t {
    TankEEpromRecordData_t data;
    // ECC Section (32 bytes)
    uint8_t ecc[32];

    TankEEpromData_t()
    {
        memset(&data, 0, DATA_SIZE);
        memset(&ecc, 0, ECC_SIZE);
    }

    TankEEpromData_t(TankEEpromData_t& other)
    {
        memcpy(&data, &other.data, DATA_SIZE);
        memcpy(&ecc, &other.ecc, ECC_SIZE);
    }

    TankEEpromData_t(TankEEpromData_t&& other)
    {
        memcpy(&data, &other.data, DATA_SIZE);
        memcpy(&ecc, &other.ecc, ECC_SIZE);
    }

    /** @brief Sets the structure to a default "New Tank" state. */
    static void format(TankEEpromData_t& eedata);
    /** @brief Computes and writes the `eec`field from the `data` field */
    static void finalize(TankEEpromData_t& eedata);
    /** @brief Checks validity and attempts to repair corrupted data using RS-FEC. Returns false if unrecoverable. */
    static bool sanitize(TankEEpromData_t& eedata);
    static void printTo(Stream& stream, TankEEpromData_t* eeprom);
    static constexpr size_t DATA_SIZE       = sizeof(data);
    static constexpr size_t ECC_SIZE        = sizeof(ecc);
    static constexpr size_t NAME_FIELD_SIZE = sizeof(data.name);
};

/**
 * @struct TankInfo
 * @brief Holds all the runtime and configuration data for a single connected tank,
 * aligned with the API schema and using standard data types.
 */
struct TankInfo {

    uint64_t uid; // Read-only UID from the EEPROM
    uint8_t lastBaseMAC48[6]; // Last MAC48 of the KibbleT5 base this device was connected to.
    std::string name; // User-configurable name.
    int8_t busIndex; // The bus index (0-5) this tank has been detected at. Equal to -1 if not present on any bus.
    bool isFullInfo; // If <false>, the whole structure is simply a presence witness and onlyh the `.uid` and `.busIndex` fields are populated.

    // Values converted from EEPROM storage units for external use
    double capacityLiters; // Volumetric capacity in Liters (EEPROM stores mL)
    double kibbleDensity; // Kibble density in kg/L (EEPROM stores g/L)

    // Calculated value based on EEPROM data
    double remaining_weight_grams; // Estimated remaining weight in grams

    // Servo calibration data
    uint16_t servoIdlePwm;

    TankInfo()
        : uid(0ULL),
          lastBaseMAC48 { 0, 0, 0, 0, 0, 0 },
          name(""),
          busIndex(-1),
          isFullInfo(false),
          capacityLiters(0),
          kibbleDensity(0),
          remaining_weight_grams(0),
          servoIdlePwm(1500)
    {}

  protected:
    friend class TankManager;

    enum TankInfoDiscrepancies_e : uint32_t
    {
        TID_NONE              = 0, // Nothing changed.
        TID_NAME_CHANGED      = 1, // The .name and/or .nameLength fields have changed.
        TID_SPECS_CHANGED     = 2, // One or more fields of the specs (.capacity, .density, .calibration) have changed.
        TID_MAC_CHANGED       = 4, // the base MAC addres have changed.
        TID_BUSINDEX_CHANGED  = 8, // the bus index has changed.
        TID_REMAINING_CHANGED = 16, // .remainingGrams and .notDSR have changed.
        TID_ALL = TID_NAME_CHANGED | TID_SPECS_CHANGED | TID_MAC_CHANGED | TID_BUSINDEX_CHANGED | TID_REMAINING_CHANGED, // All fields have changed.
    };

    bool fillFromEeprom(TankEEpromData_t& eeprom);
    /** @brief Updates a TankEEpromData_t structure from this TankInfo's fields.
     * @returns A combination of TankInfoDiscrepancies_e flags telling which fields of the eeprom have changed.
     */
    TankInfoDiscrepancies_e toTankData(TankEEpromData_t& eeprom);
};

class TankManager {
    friend struct TankInfo;

  public:
    // Constructor now takes ServoController directly.
    TankManager(DeviceState& deviceState, SemaphoreHandle_t& mutex)
        : _deviceState(deviceState),
          _deviceStateMutex(mutex),
          _pwm(PCA9685()),
          _isServoMode(false),
          _swiMux(SWIMUX_SERIAL_DEVICE, SWIMUX_TX_PIN, SWIMUX_RX_PIN),
          _lastKnownUids {}
    {}

    /** @brief Initialize the multiplexed OneWire setup but does not start the task. */
    void begin(uint16_t hopper_closed_pwm, uint16_t hopper_open_pwm);
    /** @brief Refreshes the local data about connected tanks, by interrogating them. Uses lazy update.
     * @param refreshMap <optional> bit map of the tanks to refresh.
     */
    void refresh(uint16_t refreshMap = 0xFFFFU);

    void startTask() { xTaskCreate(TankManager::_tankDetectionTask, "TankManager", 5 * 1024UL, this, 11, &TankManager::_runningTask); }
    /**
     * @brief Update both local memory and eeprom so that the amount of remaining kibble is set to a new value.
     * @param uid Uid of the tank to update.
     * @param newRemainingGrams New remaining quantity of kibble, in grams.  
     * @return <true> if the operation succeeded in eeprom. Local memory is updated but not verified (no need to, it's in RAM).
     */
    bool updateRemaingKibble(const uint64_t uid, uint16_t newRemainingGrams);

    /**
     * @brief Updates the informations relative to a tank, by selecting the right bus given the provided TankInfo::uid field
     * @param[inout] tankInfo The TankInfo data to be commit to eeprom, identified by the TankInfo::uid. The TankInfo::busIndex field will be updated if the targeted tank had changed position (i.e bus index).
     * @return <true> if a the tank has been found and updated, <false> otherwise.
     */
    bool commitTankInfo(const TankInfo& tankInfo);
    /**
     * @brief Retreives the information of a tank based on the TankInfo::uid field.
     * @param[inout] tankInfo The TankInfo structure to refresh from eeprom.
     * @return <true> if a tank has been found, <false> otherwise.
     */
    bool refreshTankInfo(TankInfo& tankInfo);
    /**
     * @brief Gets the bus index of the tank with the given @p tankUid . 
     * @param tankUid Uid of the tank to get the bus index of.
     * @return Bus index [0..5] of the tank if found, -1 if no tank with the given @p tankUid has been found.
     * @note This method refreshes the presence list of the currently connected tanks.
     */
    int8_t getBusOfTank(const uint64_t tankUid);

    /**
     * @brief Gets (or not) a known TankInfo from its uid.
     * @param uid The UID signature of the searched tank.
     * @returns A pointer to the TankInfo bearing the uid, or nullptr if absent.
     */
    TankInfo* getKnownTankOfUis(uint64_t uid);

    /**
     * @brief Gets (or not) a known TankInfo from its bus index.
     * @param busIndex The bus index of the searched tank.
     * @returns A pointer to the TankInfo bearing the searched @p busIndex, or nullptr if absent.
     */
    TankInfo* getKnownTankOfBus(uint8_t busIndex);

    /**
     * @brief Puts the SwiMux interface to sleep.
     * @return <true> if successful, <false> if no response from the SwiMux.
     */
    inline bool disableSwiMux() { return _swiMux.sleep(); }

    /**
     * @brief Sets a callback to be invoked when the tank population changes.
     * @param cb The callback function to invoke on tank population change.
     */
    void setOnTanksChangedCallback(std::function<void()> cb);

    /**
     * @brief Prints formatted information about all connected tanks to a Stream.
     * @param stream The output stream (e.g., Serial) to print to.
     */
    void printConnectedTanks(Stream& stream);

    // --- Servo Control Methods ---
    void setServoPower(bool on);
    PCA9685::I2C_Result_e setContinuousServo(uint8_t servoNum, float speed); // speed from -1.0 to 1.0
    PCA9685::I2C_Result_e stopAllServos();
    PCA9685::I2C_Result_e setServoPWM(uint8_t servoNum, uint16_t pwm);
    PCA9685::I2C_Result_e openHopper() { return setServoPWM(HOPPER_SERVO_INDEX, _hopperOpenPwm); }
    PCA9685::I2C_Result_e closeHopper() { return setServoPWM(HOPPER_SERVO_INDEX, _hopperClosedPwm); }

    // --- Hopper PWM Getters ---
    uint16_t getHopperOpenPwm() const { return _hopperOpenPwm; }
    uint16_t getHopperClosedPwm() const { return _hopperClosedPwm; }

    SwiMuxSerialResult_e swiRead(uint8_t busIndex, uint16_t address, uint8_t* dataOut, uint16_t length);
    SwiMuxSerialResult_e formatTank(uint8_t index);


  private:
    friend struct TankEEpromData_t;
#ifdef DEBUG_MENU_ENABLED
    friend void swiMuxMenu(TankManager& tankManager);
    friend void servoTestMenu(TankManager& tankManager), servoMoveMenu(TankManager& tankManager, int numServo), servoOscillateMenu(TankManager& tankManager, int servoNum);
    friend void doReadTest(TankManager& tankManager, int busIndex);
    friend void doWriteTest(TankManager& tankManager, int busIndex);

    // Public methods for the hardware test suite
    SwiMuxPresenceReport_t testSwiMuxAwaken();
    bool testSwiMuxSleep(), testSwiBusUID(uint8_t index, uint64_t& result);
    inline HardwareSerial& testGetSwiMuxPort() { return _swiMux.getSerialPort(); }
    bool testRollCall(RollCallArray_t& results);
    SwiMuxSerialResult_e testSwiMuxECC(uint8_t busIndex, int& correctedCount);
    SwiMuxSerialResult_e testSwiWrite(uint8_t busIndex, uint16_t address, const uint8_t* dataIn, uint16_t length);
#endif

    static constexpr uint32_t SWIMUX_POWERUP_DELAY_MS     = 100;
    static constexpr TickType_t MUTEX_ACQUISITION_TIMEOUT = pdMS_TO_TICKS(2000);

    DeviceState& _deviceState;
    SemaphoreHandle_t& _deviceStateMutex;
    PCA9685 _pwm;
    uint16_t _hopperOpenPwm;
    uint16_t _hopperClosedPwm;
    bool _isServoMode;

    static TaskHandle_t _runningTask;

    // A physical interface to address Dallas 1-Wire EEPROMs (DS28E07/DS2431+, 128 bytes) on 6 separate buses via 57600B8N1 UART.
    SwiMuxSerial_t _swiMux;
    RollCallArray_t _lastKnownUids;
    // A dedicated mutex to protect 1-Wire bus transactions.
    SemaphoreHandle_t _swimuxMutex;
    // Callback invoked when tank population changes (for SSE notifications).
    std::function<void()> _onTanksChangedCallback;


    // Internal list of tanks, which holds the comprehensive state.
    std::vector<TankInfo> _knownTanks;
    void removeKnownTank(TankInfo* tankToRemove);

    // --- PCA9685 Mode Switching Helpers ---
    void _switchToSwiMode();
    void _switchToServoMode();


    inline void fullRefresh() { refresh(0xFFFF); }
    static void _tankDetectionTask(void* pvParam);

    /** @brief Selectively updates an eeprom through the _swiMux adapter. 
     * @param data Reference to the TankEEpromData_t to use as source.
     * @param updatesNeeded A combination of flags telling us which memory fields to update. 
     * @param forcedBusIndex [optional] By default, the bus to address is designated by @p data.busIndex . This parameter lets us ovveride this behavior.
     * @note The data structure in eeprom is identical to TankEEpromData_t, byte-for-byte. Thus, endianness and alignment is only relative to the host (this means us). The SwiMuxSerial_t can be seen as the simple, low-memory long-term storage it is. 
     * @remark This method will try to engage in the least amount of SwiMuxSerial_t::write transactions as possible (contiguous fields/fields groups to be updated will be written in one operation if possible). */
    bool updateEeprom(TankEEpromData_t& data, TankInfo::TankInfoDiscrepancies_e updatesNeeded, int8_t forcedBusIndex = -1);
};


#endif // TANKMANAGER_HPP
