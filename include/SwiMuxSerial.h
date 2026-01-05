#ifndef H_SWIMUX_SERIAL_H
#define H_SWIMUX_SERIAL_H

#include <HardwareSerial.h>
#include "SwiMuxComms.hpp"

enum SwiMuxSerialResult_e : uint8_t /* Includes proprietary values as well as values from SwiMuxError_e and OneWireError_e */
{
    SMREZ_OK = 0,
    SMREZ_INVALID_PAYLOAD,
    SMREZ_BUS_INDEX_OUT_OF_RANGE,
    SMREZ_NO_DEVICE,
    SMREZ_TIMED_OUT,
    SMREZ_READ_RESP_ERROR,
    SMREZ_WRITE_OUTOFMEM,
    SMREZ_WRITE_ENCODE_FAILED,
    SMREZ_WRITE_ACK_MISSING,
    SMREZ_SWIMUX_SILENT,
    SMREZ_NULL_PARAM,
    SMREZ_OW_DIO_PORT_NULL = SwiMuxError_e::SMERR_OW_DIO_PORT_NULL,
    SMREZ_OW_DIO_PORT_INVALID,
    SMREZ_OW_DIO_PIN_INVALID,
    SMREZ_OW_PULLUP_PORT_INVALID,
    SMREZ_OW_PULLUP_PIN_INVALID,
    SMREZ_OW_NULL_INPUT_BUFFER,
    SMREZ_OW_NULL_OUTPUT_BUFFER,
    SMREZ_OW_NO_BUS_POWER,
    SMREZ_OW_BUS_HELD_LOW,
    SMREZ_OW_NO_DEVICE_PRESENT,
    SMREZ_OW_READ_ROM_FAILED,
    SMREZ_OW_ALIGNED_WRITE_HEAD_PREREAD,
    SMREZ_OW_ALIGNED_WRITE_TAIL_PREREAD,
    SMREZ_OW_MEMADDRESS_OUT_OF_BOUNDS, // Specified address is out of memory bounds.
    SMREZ_OW_OUT_OF_BOUNDS, // Specified length exceeds out of memory bounds.
    SMREZ_OW_WRITE_MEM_FAILED,
    SMREZ_OW_MULTIDROP_ID_UNREADABLE,
    SMREZ_OW_WRITE_SCRATCHPAD_PRESELECT,
    SMREZ_OW_WRITE_SCRATCHPAD_CRC16,
    SMREZ_OW_READ_SCRATCHPAD_PRESELECT,
    SMREZ_OW_READ_SCRATCHPAD_CRC16,
    SMREZ_OW_SCRATCHPAD_PF, // Power loss or scratchpad not full
    SMREZ_OW_WRITTEN_SCRATCHPAD_MISMATCH,
    SMREZ_OW_COPY_SCRATCHPAD_PRESELECT,
    SMREZ_OW_COPY_SCRATCHPAD,
    SMREZ_UnkownCommand = SwiMuxError_e::SMERR_UnkownCommand,
    SMREZ_Framing,
    SMREZ_WrongEscape,
    SMREZ_ReadBytesParams,
    SMREZ_BusIndexOutOfRange,
    SMREZ_MemOffsetOutOfRange,
    SMREZ_ReadLengthOutOfRange,
    SMREZ_ReadMemoryFailed,
    SMREZ_ResponseEncodingFailed,
    SMREZ_WriteLengthOutOfRange,
    SMREZ_WriteFailed,
    SMREZ_GuidUnreadable,
    SMREZ_BadCrc,
    SMREZ_BADFUNCALL, // critial software error
    SMREZ_CommandDisabled,
    SMREZ_MutexAcquisition,
};



struct SwiMuxPresenceReport_t {
    uint16_t presences; // Bit flags, each representing presence '1' or absence `0` of an EEPROM on each bus of the respective bus index/bit index.
    uint8_t busesCount; // The actual count of connected EEPROMS.

    SwiMuxPresenceReport_t() : presences(0), busesCount(0) {}
    SwiMuxPresenceReport_t(uint16_t presenceMap, uint8_t maxDevices) : presences(presenceMap), busesCount(maxDevices) {}

    bool operator==(const SwiMuxPresenceReport_t& other) { return busesCount == other.busesCount && presences == other.presences; }
    bool operator!=(const SwiMuxPresenceReport_t& other) { return busesCount != other.busesCount || presences != other.presences; }
    SwiMuxPresenceReport_t operator^(const SwiMuxPresenceReport_t& other)
    {
        SwiMuxPresenceReport_t rpt = { (uint16_t)(presences ^ other.presences), 0 };
        rpt.busesCount             = (uint8_t)__builtin_popcount((unsigned int)rpt.presences);
        return rpt;
    }
    SwiMuxPresenceReport_t& operator^=(const SwiMuxPresenceReport_t& other)
    {
        presences ^= other.presences;
        busesCount = (uint8_t)__builtin_popcount((unsigned int)presences);
        return *this;
    }
};

class SwiMuxSerial_t {
  public:
    SwiMuxSerial_t(HardwareSerial& serial, uint8_t txPin, uint8_t rxPin)
        : _lastResult(SwiMuxSerialResult_e::SMREZ_OK), _codec(), _sPort(serial), _isAwake(false), _beginCalled(false), _txPin(txPin), _rxPin(rxPin)
    {}

    void begin();

    bool sleep();
    bool isAsleep() { return !_isAwake; }
    /**
     * @brief Uses a polling mechanism to detect if the SwiMux interface has produced a wake-up packet.
     * @param[out] reportOut <optional> A report to fill with the possible wake-up packet's presence report contents.
     * @return <false> if no wake-up event has been detected. 
     */
    bool hasEvents(SwiMuxPresenceReport_t* reportOut = NULL);
    /**
     * @brief Gets a report about the presences of the 1kb EEPROMS (DS28E04/DS2431+) attached 1-to-1 on the 6 distinct buses of this SwiMux.
     * @param timeout_ms Max time to wait for an answer from the SwiMux, in milliseconds
     * @return A report of the current population of EEPROMS connected (and answering) to this SwiMux.
     */
    SwiMuxPresenceReport_t getPresence(uint32_t timeout_ms = PRESENCE_TIMEOUT_MS);
    /**
     * @brief Gets an array of the UIDs (64-bits serial numbers) of each 1-wire EEPROM connected to the SwiMux's buses.
     * @param uids The result of the roll call. Any missing/dead EEPROM is reported as UINT64_MAX (all 64 bits set).
     * @return SwiMuxSerialResult_e::SMREZ_OK is the roll call succeeded.
     */
    SwiMuxSerialResult_e rollCall(RollCallArray_t& uids, uint32_t timeout_ms = ROLLCALL_TIMEOUT_MS);
    /**
     * @brief Read a span of bytes from the EEPROM on a specified bus.
     * @param busIndex Bus from which to read.
     * @param bufferOut Recipient buffer of the read. Mustn't be null.
     * @param offset Offset in EEPROM from which to start the read.
     * @param len Number of bytes to read. Must be <= to the size of @p bufferOut
     * @param timeout_ms Maximum amount of millisecond to wait for the result.
     * @return SwiMuxSerialResult_e::SMREZ_OK if the read succeeded.
     */
    SwiMuxSerialResult_e read(uint8_t busIndex, uint8_t* bufferOut, uint8_t offset, uint8_t len, uint32_t timeout_ms = READ_TIMEOUT_MS);
    /**
     * @brief Writes a span of bytes to the EEPROM on the specified bus.
    *  @param busIndex Bus to write to which.
     * @param bufferIn Input data to write in EEPROM. Mustn't be null.
     * @param offset Offset in EEPROM from which to start the write.
     * @param len Number of bytes to write. Must be <= to the size of @p bufferOut
     * @return SwiMuxSerialResult_e::SMREZ_OK if the write succeeded.
     */
    SwiMuxSerialResult_e write(uint8_t busIndex, const uint8_t* bufferIn, uint8_t offset, uint8_t len, uint32_t timeout_ms = WRITE_TIMEOUT_MS);
    /** @brief Gets the UID of the device present (or not) on the specified bus.
     * @param busIndex Index of the bus to interrogate.
     * @param &result Reference to the variable that will store the result. It will be UINT64_MAX if nothing's on @p busIndex.
     * @result SwiMuxSerialResult_e::SMREZ_OK if the SwiMux interrogated the bus, EEPROM present or not.
     */
    SwiMuxSerialResult_e getUid(uint8_t busIndex, uint64_t& result, uint32_t timeout_ms = GETUID_TIMEOUT_MS);


#ifdef KIBBLET5_DEBUG_ENABLED
    inline void printRawString(const char* str) { _sPort.print(str); }
    inline HardwareSerial& getSerialPort() { return _sPort; }

#endif

    static const char* getSwiMuxErrorString(const SwiMuxSerialResult_e value);
    static constexpr uint32_t DEFAULT_SERIAL_CONFIG = SERIAL_8N1;
    static constexpr uint32_t DEFAULT_SERIAL_BAUDS  = 57600;

  private:
#define UART_DURATION_MS_ROUND(CHAR_COUNT, BAUDS) ((((uint64_t)(CHAR_COUNT * 2) * 10000ULL + ((uint64_t)(BAUDS) / 2ULL)) / (uint64_t)(BAUDS)))
    //static constexpr uint32_t PRESENCE_DELAY_MS     = 3;
    //static constexpr uint32_t GETUID_CMD_DELAY_MS   = 3;
    //static constexpr uint32_t READ_CMD_DELAY_MS     = 4;
    //static constexpr uint32_t WRITE_CMD_DELAY_MS    = 8;
    //static constexpr uint32_t ROLLCALL_CMD_DELAY_MS = 20;
    static constexpr size_t AWAKE_RETRIES_DEFAULT = 3;
    static constexpr uint32_t PRESENCE_TIMEOUT_MS = 1 + 2 * UART_DURATION_MS_ROUND(sizeof(SwiMuxPresenceReport_t), DEFAULT_SERIAL_BAUDS);
    static constexpr uint32_t GETUID_TIMEOUT_MS   = 100; //(uint32_t)(10 + 5.0 * UART_DURATION_MS_ROUND(10, DEFAULT_SERIAL_BAUDS));
    static constexpr uint32_t READ_TIMEOUT_MS     = 600; //(uint32_t)(10 + 5.0 * UART_DURATION_MS_ROUND(140, DEFAULT_SERIAL_BAUDS));
    static constexpr uint32_t WRITE_TIMEOUT_MS    = 600; //(uint32_t)(10 + 5.0 * UART_DURATION_MS_ROUND(140, DEFAULT_SERIAL_BAUDS));
    static constexpr uint32_t ROLLCALL_TIMEOUT_MS
      = 333; //(uint32_t)(10 + 5.0 * UART_DURATION_MS_ROUND(sizeof(SwiMuxRespUID_t), DEFAULT_SERIAL_BAUDS));

    bool assertAwake(size_t retries = AWAKE_RETRIES_DEFAULT);
    SwiMuxPresenceReport_t _pollPresencePacket(uint32_t timeout_ms = PRESENCE_TIMEOUT_MS);
    bool pollAck(SwiMuxOpcodes_e opcode, uint32_t timeout_ms = 15);
    SwiMuxSerialResult_e _lastResult;
    SwiMuxComms_t _codec;
    HardwareSerial _sPort;
    volatile bool _isAwake, _beginCalled;
    uint8_t _rxPin, _txPin;
    uint16_t lastPresence();
};

#endif
