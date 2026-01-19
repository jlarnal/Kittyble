#include "SwiMuxSerial.h"
#include "SerialDebugger.hpp"



static const char* TAG = "SwiMuxSerial";

#if defined(DEBUG_SWIMUX) && defined(ARDUINO)
#include <HardwareSerial.h>
#define SWI_DBGF(fmt, ...)                                                                                                                           \
    do {                                                                                                                                             \
        Serial.printf(fmt, __VA_ARGS__);                                                                                                             \
        Serial.flush();                                                                                                                              \
    } while (0)
#define SWI_DBG(fmt)                                                                                                                                 \
    do {                                                                                                                                             \
        Serial.print(fmt);                                                                                                                           \
    } while (0)
#define SWI_DBG_FLUSH()               Serial.flush()
#define SWI_DBGBUFF(TITLE, BUFF, LEN) DebugSerial.print(TITLE, BUFF, LEN)
#else
#define SWI_DBGF(fmt, ...)            _NOP()
#define SWI_DBG(fmt)                  _NOP()
#define SWI_DBGBUFF(TITLE, BUFF, LEN) _NOP()
#define SWI_DBG_FLUSH()               _NOP()
#endif

const char* SwiMuxSerial_t::getSwiMuxErrorString(const SwiMuxSerialResult_e value)
{
    switch (value) {
        case SwiMuxSerialResult_e::SMREZ_OK:
            return "SMREZ_OK";
        case SwiMuxSerialResult_e::SMREZ_INVALID_PAYLOAD:
            return "SMREZ_INVALID_PAYLOAD";
        case SwiMuxSerialResult_e::SMREZ_BUS_INDEX_OUT_OF_RANGE:
            return "SMREZ_BUS_INDEX_OUT_OF_RANGE";
        case SwiMuxSerialResult_e::SMREZ_NO_DEVICE:
            return "SMREZ_NO_DEVICE";
        case SwiMuxSerialResult_e::SMREZ_TIMED_OUT:
            return "SMREZ_TIMED_OUT";
        case SwiMuxSerialResult_e::SMREZ_READ_RESP_ERROR:
            return "SMREZ_READ_RESP_ERROR";
        case SwiMuxSerialResult_e::SMREZ_WRITE_OUTOFMEM:
            return "SMREZ_WRITE_OUTOFMEM";
        case SwiMuxSerialResult_e::SMREZ_WRITE_ENCODE_FAILED:
            return "SMREZ_WRITE_ENCODE_FAILED";
        case SwiMuxSerialResult_e::SMREZ_WRITE_ACK_MISSING:
            return "SMREZ_WRITE_ACK_MISSING";
        case SwiMuxSerialResult_e::SMREZ_SWIMUX_SILENT:
            return "SMREZ_SWIMUX_SILENT";
        case SwiMuxSerialResult_e::SMREZ_NULL_PARAM:
            return "SMREZ_NULL_PARAM";
        case SwiMuxSerialResult_e::SMREZ_OW_DIO_PORT_NULL:
            return "SMREZ_OW_DIO_PORT_NULL";
        case SwiMuxSerialResult_e::SMREZ_OW_DIO_PORT_INVALID:
            return "SMREZ_OW_DIO_PORT_INVALID";
        case SwiMuxSerialResult_e::SMREZ_OW_DIO_PIN_INVALID:
            return "SMREZ_OW_DIO_PIN_INVALID";
        case SwiMuxSerialResult_e::SMREZ_OW_PULLUP_PORT_INVALID:
            return "SMREZ_OW_PULLUP_PORT_INVALID";
        case SwiMuxSerialResult_e::SMREZ_OW_PULLUP_PIN_INVALID:
            return "SMREZ_OW_PULLUP_PIN_INVALID";
        case SwiMuxSerialResult_e::SMREZ_OW_NULL_INPUT_BUFFER:
            return "SMREZ_OW_NULL_INPUT_BUFFER";
        case SwiMuxSerialResult_e::SMREZ_OW_NULL_OUTPUT_BUFFER:
            return "SMREZ_OW_NULL_OUTPUT_BUFFER";
        case SwiMuxSerialResult_e::SMREZ_OW_NO_BUS_POWER:
            return "SMREZ_OW_NO_BUS_POWER";
        case SwiMuxSerialResult_e::SMREZ_OW_BUS_HELD_LOW:
            return "SMREZ_OW_BUS_HELD_LOW";
        case SwiMuxSerialResult_e::SMREZ_OW_NO_DEVICE_PRESENT:
            return "SMREZ_OW_NO_DEVICE_PRESENT";
        case SwiMuxSerialResult_e::SMREZ_OW_READ_ROM_FAILED:
            return "SMREZ_OW_READ_ROM_FAILED";
        case SwiMuxSerialResult_e::SMREZ_OW_ALIGNED_WRITE_HEAD_PREREAD:
            return "SMREZ_OW_ALIGNED_WRITE_HEAD_PREREAD";
        case SwiMuxSerialResult_e::SMREZ_OW_ALIGNED_WRITE_TAIL_PREREAD:
            return "SMREZ_OW_ALIGNED_WRITE_TAIL_PREREAD";
        case SwiMuxSerialResult_e::SMREZ_OW_MEMADDRESS_OUT_OF_BOUNDS:
            return "SMREZ_OW_MEMADDRESS_OUT_OF_BOUNDS";
        case SwiMuxSerialResult_e::SMREZ_OW_OUT_OF_BOUNDS:
            return "SMREZ_OW_OUT_OF_BOUNDS";
        case SwiMuxSerialResult_e::SMREZ_OW_WRITE_MEM_FAILED:
            return "SMREZ_OW_WRITE_MEM_FAILED";
        case SwiMuxSerialResult_e::SMREZ_OW_MULTIDROP_ID_UNREADABLE:
            return "SMREZ_OW_MULTIDROP_ID_UNREADABLE";
        case SwiMuxSerialResult_e::SMREZ_OW_WRITE_SCRATCHPAD_PRESELECT:
            return "SMREZ_OW_WRITE_SCRATCHPAD_PRESELECT";
        case SwiMuxSerialResult_e::SMREZ_OW_WRITE_SCRATCHPAD_CRC16:
            return "SMREZ_OW_WRITE_SCRATCHPAD_CRC16";
        case SwiMuxSerialResult_e::SMREZ_OW_READ_SCRATCHPAD_PRESELECT:
            return "SMREZ_OW_READ_SCRATCHPAD_PRESELECT";
        case SwiMuxSerialResult_e::SMREZ_OW_READ_SCRATCHPAD_CRC16:
            return "SMREZ_OW_READ_SCRATCHPAD_CRC16";
        case SwiMuxSerialResult_e::SMREZ_OW_SCRATCHPAD_PF:
            return "SMREZ_OW_SCRATCHPAD_PF";
        case SwiMuxSerialResult_e::SMREZ_OW_WRITTEN_SCRATCHPAD_MISMATCH:
            return "SMREZ_OW_WRITTEN_SCRATCHPAD_MISMATCH";
        case SwiMuxSerialResult_e::SMREZ_OW_COPY_SCRATCHPAD_PRESELECT:
            return "SMREZ_OW_COPY_SCRATCHPAD_PRESELECT";
        case SwiMuxSerialResult_e::SMREZ_OW_COPY_SCRATCHPAD:
            return "SMREZ_OW_COPY_SCRATCHPAD";
        case SwiMuxSerialResult_e::SMREZ_UnkownCommand:
            return "SMREZ_UnkownCommand";
        case SwiMuxSerialResult_e::SMREZ_Framing:
            return "SMREZ_Framing";
        case SwiMuxSerialResult_e::SMREZ_WrongEscape:
            return "SMREZ_WrongEscape";
        case SwiMuxSerialResult_e::SMREZ_ReadBytesParams:
            return "SMREZ_ReadBytesParams";
        case SwiMuxSerialResult_e::SMREZ_BusIndexOutOfRange:
            return "SMREZ_BusIndexOutOfRange";
        case SwiMuxSerialResult_e::SMREZ_MemOffsetOutOfRange:
            return "SMREZ_MemOffsetOutOfRange";
        case SwiMuxSerialResult_e::SMREZ_ReadLengthOutOfRange:
            return "SMREZ_ReadLengthOutOfRange";
        case SwiMuxSerialResult_e::SMREZ_ReadMemoryFailed:
            return "SMREZ_ReadMemoryFailed";
        case SwiMuxSerialResult_e::SMREZ_ResponseEncodingFailed:
            return "SMREZ_ResponseEncodingFailed";
        case SwiMuxSerialResult_e::SMREZ_WriteLengthOutOfRange:
            return "SMREZ_WriteLengthOutOfRange";
        case SwiMuxSerialResult_e::SMREZ_WriteFailed:
            return "SMREZ_WriteFailed";
        case SwiMuxSerialResult_e::SMREZ_GuidUnreadable:
            return "SMREZ_GuidUnreadable";
        case SwiMuxSerialResult_e::SMREZ_BadCrc:
            return "SMREZ_BadCrc";
        case SwiMuxSerialResult_e::SMREZ_BADFUNCALL:
            return "SMREZ_BADFUNCALL";
        case SwiMuxSerialResult_e::SMREZ_CommandDisabled:
            return "SMREZ_CommandDisabled";
        case SwiMuxSerialResult_e::SMREZ_MutexAcquisition:
            return "SMREZ_MutexAcquisition";
        default:
            break;
    }
    return "<undefined>";
}


static uint64_t u64fromBytes(uint8_t* bytes, size_t len)
{
    uint64_t result = 0; // perfectly aligned
    if (!bytes || len < 8)
        return 0;
    if (len > 8)
        len = 8; /* clamp to 8 bytes max */

/* Compile-time endianness detection (common predefined macros) */
#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) || defined(__LITTLE_ENDIAN__) || defined(_WIN32) || defined(__ARMEL__)  \
  || defined(__MIPSEL__)
#define U64FROMBYTES_LITTLE_ENDIAN 1
#elif (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) || defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__MIPSEB__)
    /* big endian — leave macro unset */
#else
#error "Unable to detect endianness for u64fromBytes"
#endif

#ifdef U64FROMBYTES_LITTLE_ENDIAN
    // Little endian
    uint8_t* dest = (uint8_t*)&result;
    while (len--) {
        *dest++ = *bytes++;
    }
#else
    // Big endian
    uint8_t* dest = &(((uint8_t*)&result)[7]);
    while (len--) {
        *dest-- = *bytes++;
    }
#endif

    return result;
}

inline static constexpr bool areNegates(uint8_t a, uint8_t b) noexcept
{
    // cast the promoted result of ~b back to uint8_t to get the 8-bit bitwise NOT
    return a == static_cast<uint8_t>(~b);
}

void SwiMuxSerial_t::begin()
{
    if (!_beginCalled) {
        _sPort.begin(57600, SerialConfig::SERIAL_8N1, _rxPin, _txPin);
        SWI_DBG("Serial port initialized.");
    }
}



bool SwiMuxSerial_t::assertAwake(size_t retries)
{
    uint8_t msg[2] = { SMCMD_Wakeup, (uint8_t)(0xFF & ~SMCMD_Wakeup) };
    _sPort.flush();
    while (_sPort.available()) {
        _sPort.read();
    }

    bool success;
    // Wake up and resync
    _codec.resync([this](uint8_t val) { this->_sPort.write(val); }, [](unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }, true);

    do {
        _codec.encode(msg, 2, [this](uint8_t value) { this->_sPort.write(((uint8_t)value)); });
        if (_codec.waitForAckTo(
              SMCMD_Wakeup, millis, [this]() -> int { return this->_sPort.read(); }, [](unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); })) {
            success = true;
            break;
        } else {
            _lastResult = SMREZ_NO_DEVICE;
            success     = false;
            SWI_DBGF("\r\n--> waitForAckTo(%d) failed, %d retries remaining.\r\n", SMCMD_Wakeup, retries - 1);
        }
    } while (--retries);
    // Wait for any other message to arrive
    vTaskDelay(pdMS_TO_TICKS(20));
    while (_sPort.available()) {
        _sPort.read();
    }

    return success;
}

bool SwiMuxSerial_t::sleep()
{
    _codec.encode(SwiMuxRequest_Sleep, sizeof(SwiMuxRequest_Sleep), [this](uint8_t value) { this->_sPort.write(((uint8_t)value)); });
    _isAwake    = false;
    _lastResult = (SwiMuxSerialResult_e)_codec.waitForAckTo(
      SwiMuxOpcodes_e::SMCMD_Sleep, millis, [this]() -> int { return this->_sPort.read(); }, [](unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });
    return _lastResult == SMREZ_OK;
}

bool SwiMuxSerial_t::hasEvents(SwiMuxPresenceReport_t* reportOut)
{
    if (!assertAwake())
        return false;
    SwiMuxPresenceReport_t report;
    report = _pollPresencePacket();
    if (report.busesCount > 0) {
        if (reportOut != NULL)
            *reportOut = report;
        return true;
    }
    return false;
}

SwiMuxPresenceReport_t SwiMuxSerial_t::getPresence(uint32_t timeout_ms)
{
    if (!assertAwake())
        return SwiMuxPresenceReport_t();
    while (_sPort.available()) {
        _sPort.read();
    }

    _codec.encode(SwiMuxRequest_GetPresence, sizeof(SwiMuxRequest_GetPresence), [this](uint8_t value) { this->_sPort.write(((uint8_t)value)); });

    SwiMuxPresenceReport_t res = _pollPresencePacket(timeout_ms);
    if (res.busesCount > 0)
        _isAwake = true;
    return res;
}

SwiMuxPresenceReport_t SwiMuxSerial_t::_pollPresencePacket(uint32_t timeout_ms)
{
    uint8_t* payload              = nullptr;
    size_t pLen                   = 0;
    uint32_t startTime            = millis();
    SwiMuxPresenceReport_t result = { 0, 0 };
    //SWI_DBGF("[_pollPresencePacket] entered.%s", "\r\n");
    do {

        int charVal = _sPort.read();

        if (charVal > -1) {
            SWI_DBGF("0x%02x, ", charVal);
            SwiMuxError_e res = _codec.decode((uint8_t)charVal, payload, pLen);
            if (res == SMERR_Done) {
                SWI_DBGF("[pollPresence] payload decoded at %u\r\n", millis());
                if (pLen != 0) {
                    ESP_LOGI(TAG, "_pollPresencePacket: packet decoded, pLen is %d", pLen);
                    SWI_DBGBUFF("(SwiMuxCmdPresence_t):", (void*)payload, pLen);
                }

                if (pLen == sizeof(SwiMuxCmdPresence_t) && payload != nullptr && areNegates(payload[0], payload[1])) {
                    if (payload[0] == SMCMD_GetPresence) {
                        ESP_LOGI(TAG, "_pollPresencePacket: Received payload of %d bytes\r\n", pLen);
                        SwiMuxCmdPresence_t resp;
                        memcpy(&resp, payload, sizeof(SwiMuxCmdPresence_t));
                        result.busesCount = resp.busesCount;
                        result.presences  = (((uint16_t)resp.presenceMSB) << 8) | resp.presenceLSB;
                        return result;
                    } else if (payload[0] == SMCMD_Nack) {
                        _lastResult = (SwiMuxSerialResult_e)payload[2];
                    }
                }
            } else if (res != SMERR_Ok) {
                _lastResult = (SwiMuxSerialResult_e)res;
                ESP_LOGW(TAG, "_pollPresencePacket: Failed to retreive presence report (err #%d)", res);
                return result;
            }
        }

        vTaskDelay(1); // with a tick of 1ms (as default on ESP32), we should get 5.76 characters per wait cycle @ 57600bds.
    } while ((millis() - startTime) <= timeout_ms);
    if (result.busesCount > 0)
        _isAwake = true;
    return result;
}


SwiMuxSerialResult_e SwiMuxSerial_t::getUid(uint8_t busIndex, uint64_t& uid, uint32_t timeout_ms)
{
    if (!assertAwake())
        return SwiMuxSerialResult_e::SMREZ_SWIMUX_SILENT;

    SwiMuxGetUID_t getUidCmd = SwiMuxGetUID_t((uint8_t)(busIndex % 6));
    _codec.encode((uint8_t*)&getUidCmd, sizeof(getUidCmd), [this](uint8_t value) { this->_sPort.write(((uint8_t)value)); });
    uint8_t* payload = nullptr;
    size_t pLen      = 0;
    //vTaskDelay(pdMS_TO_TICKS(GETUID_CMD_DELAY_MS));
    uint32_t startTime = millis();

    do {
        if (_sPort.available()) {
            int charVal = _sPort.read();

            if (charVal > -1) {
                SWI_DBGF("0x%02X\r\n", charVal);
                SwiMuxError_e res = _codec.decode((uint8_t)charVal, payload, pLen);
                if (res == SMERR_Done) {
                    SWI_DBGF("[getUid] payload decoded at %u\r\n", millis());
                    SWI_DBGBUFF("Payload:", payload, pLen);
                    SWI_DBGF("areNegates: %d\r\n", areNegates(payload[0], payload[1]) ? 1 : 0);
                    if (pLen > 0 && payload != nullptr && areNegates(payload[0], payload[1])) {
                        if (payload[0] == SMCMD_HaveUID) {
                            SWI_DBG("[geUid]\033[93mSUCCESS !!\033[0m\r\n");
                            uid      = u64fromBytes(payload + 2, sizeof(uid));
                            _isAwake = true;
                            return SwiMuxSerialResult_e::SMREZ_OK;
                        } else if (payload[0] == SMCMD_Nack) {
                            _lastResult = (SwiMuxSerialResult_e)payload[2];
                        } else {
                            _lastResult = SwiMuxSerialResult_e::SMREZ_Framing;
                        }
                    }
                } else if (res != SMERR_Ok) {
                    _lastResult = (SwiMuxSerialResult_e)res;
                    uid         = 0;
                    SWI_DBGF("[SwiMuxSerial::getUid] Failed to retreive UID on port %d (err #%d)\r\n", getUidCmd.busIndex, res);
                    return SwiMuxSerialResult_e::SMREZ_Framing;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2)); // with a tick of 1ms (as default on ESP32), we should get 5.76 characters per wait cycle @ 57600bds.
    } while ((millis() - startTime) <= timeout_ms);
    return SMREZ_TIMED_OUT;
}


SwiMuxSerialResult_e SwiMuxSerial_t::rollCall(RollCallArray_t& uidsList, uint32_t timeout_ms)
{
    if (!assertAwake())
        return SwiMuxSerialResult_e::SMREZ_SWIMUX_SILENT;
    uint8_t msg[8] = { SMCMD_RollCall, (uint8_t)(0xFF & ~SMCMD_RollCall) };
    _codec.encode(msg, 2, [this](uint8_t val) { this->_sPort.write(val); });

    //vTaskDelay(ROLLCALL_CMD_DELAY_MS);
    uint32_t startTime = millis();

    uint8_t* payload = nullptr;

    memset(&uidsList, 0, sizeof(RollCallArray_t));

    size_t pLen = 0;
    do {
        if (_sPort.available()) {
            int charVal = _sPort.read();
            if (charVal > -1) {
                SwiMuxError_e res = _codec.decode((uint8_t)charVal, payload, pLen);
                if (res == SMERR_Done) {
                    SWI_DBGF("[rollCall] payload decoded at %u\r\n", millis());
                    if (pLen == sizeof(SwiMuxRollCallResult_t) && payload != nullptr && areNegates(payload[0], payload[1])) {
                        if (payload[0] == SMCMD_RollCall) { // valid payload ?
                            SWI_DBG("[rollCall] payload validated !\r\n");
                            SWI_DBGBUFF("uids:", &payload[2], pLen - 2);
                            for (int busIndex = 0; busIndex < NUMBER_OF_BUSES; busIndex++) {
                                memcpy((void*)&uidsList.bus[busIndex], (void*)&payload[2 + busIndex * 8], 8);
                            }
                            _isAwake = true;
                            return SMREZ_OK;
                        } else if (payload[0] == SMCMD_Nack) {
                            _lastResult = (SwiMuxSerialResult_e)payload[2];
                        } else {
                            _lastResult = SMREZ_Framing;
                        }
                    } else { // We got a paylod, but not the expected one.
                        SWI_DBGF("[rollCall] payload invalid (payLoad=%p, pLen=%d) !\r\n", payload, pLen);
                        SWI_DBGBUFF("contents:", payload, pLen);
                        return SMREZ_INVALID_PAYLOAD;
                    }

                } else if (res != SMERR_Ok) {
                    _isAwake = true;
                    ESP_LOGW(TAG, "Failed to retreive roll call (err #%d)", res);
                    return SMREZ_Framing;
                }
            }
        }
        vTaskDelay(1); // with a tick of 1ms (as default on ESP32), we should get 5.76 characters per wait cycle @ 57600bds.
    } while ((millis() - startTime) <= timeout_ms);
    return SMREZ_TIMED_OUT;
}


SwiMuxSerialResult_e SwiMuxSerial_t::read(uint8_t busIndex, uint8_t* bufferOut, uint8_t offset, uint8_t len, uint32_t timeout_ms)
{
    if (bufferOut == nullptr)
        return SMREZ_NULL_PARAM;
    if (!assertAwake())
        return SwiMuxSerialResult_e::SMREZ_SWIMUX_SILENT;
    // Start by sending the read request.
    SwiMuxCmdRead_t cmd = { .Opcode = SMCMD_ReadBytes,
        .NegOpcode                  = (uint8_t)(0xFF & ~SMCMD_ReadBytes),
        .busIndex                   = (uint8_t)(busIndex % 6),
        .offset                     = offset,
        .length                     = len };
    _codec.encode((uint8_t*)&cmd, sizeof(SwiMuxCmdRead_t), [this](uint8_t val) { this->_sPort.write(val); });

    //vTaskDelay(pdMS_TO_TICKS(SwiMuxSerial_t::READ_CMD_DELAY_MS));
    uint32_t startTime = millis();
    uint8_t* payload   = nullptr;
    size_t pLen        = 0;
    int received;
    do {
        received = _sPort.read();
        if (received > -1) {
            SwiMuxError_e res = _codec.decode(received, payload, pLen);
            if (res == SMERR_Done) {
                SWI_DBGF("[read] payload decoded at %u\r\n", millis());
                if (pLen <= (len + sizeof(SwiMuxCmdRead_t)) && payload != nullptr) {
                    // Check and copy the payload.0
                    if (payload[0] != (uint8_t)SMCMD_ReadBytes || !areNegates(payload[0], payload[1]) || payload[2] != cmd.busIndex
                      || payload[3] != cmd.offset) {
                        // Payload has some unexpected header.

                        ESP_LOGE(TAG, "Unexpected values in read response header.");
                        _isAwake = true;
                        return SMREZ_READ_RESP_ERROR;
                    } else { // payload seems legit
                        uint8_t reportedLen = payload[4];

                        // Check if the remote device's reported length is valid
                        if (reportedLen > len) {
                            ESP_LOGE(TAG, "Read failed: device reported %d bytes, but buffer is only %d bytes", reportedLen, len);
                            return SMREZ_READ_RESP_ERROR; // Or some other error
                        }
                        // Check if the total packet is too small
                        if (pLen < (sizeof(SwiMuxCmdRead_t) + reportedLen)) {
                            ESP_LOGE(
                              TAG, "Read failed: packet truncated. pLen=%d, expected at least %d", pLen, sizeof(SwiMuxCmdRead_t) + reportedLen);
                            return SMREZ_Framing;
                        }

                        memcpy(bufferOut, &payload[sizeof(SwiMuxCmdRead_t)], reportedLen);

                        _isAwake = true;
                        return SMREZ_OK;
                    }
                }
            } else if (res != SMERR_Ok) {
                ESP_LOGW(TAG, "Failed to read device on bus %d (err #%d)", cmd.busIndex, res);
                return SMREZ_Framing;
            }
        }
        vTaskDelay(1); // with a tick of 1ms (as default on ESP32), we should get 5.76 characters per wait cycle @ 57600bds.
    } while ((millis() - startTime) <= timeout_ms);
    return SMREZ_TIMED_OUT;
}


SwiMuxSerialResult_e SwiMuxSerial_t::write(uint8_t busIndex, const uint8_t* bufferIn, uint8_t offset, uint8_t len, uint32_t timeout_ms)
{
    if (bufferIn == nullptr)
        return SMREZ_NULL_PARAM;
    if (!assertAwake())
        return SwiMuxSerialResult_e::SMREZ_SWIMUX_SILENT;
    // Create a write command.
    SwiMuxCmdWrite_t* pCmd = (SwiMuxCmdWrite_t*)malloc(sizeof(SwiMuxCmdWrite_t) + (size_t)len);

    if (pCmd == nullptr) { // allocation failed ?
        ESP_LOGE(
          TAG, "Could not allocate %u bytes in internal ram for `SwiMuxSerial_t::write` command buffer.", sizeof(SwiMuxCmdWrite_t) + (size_t)len);
        return SMREZ_WRITE_OUTOFMEM;
    }

    //// --- START DEBUG ADDITIONS ---
    //Serial.printf("[DEBUG] pCmd after malloc: %p\n", pCmd);
    //Serial.flush();
    //// --- END DEBUG ADDITIONS ---

    pCmd->Opcode    = SMCMD_WriteBytes;
    pCmd->NegOpcode = (uint8_t)(0xFF & ~SMCMD_WriteBytes);
    pCmd->busIndex  = (uint8_t)(busIndex % NUMBER_OF_BUSES);
    pCmd->offset    = offset;
    pCmd->length    = len;

    //// --- START DEBUG ADDITIONS ---
    //Serial.printf("[DEBUG] pCmd before memcpy: %p\r\n", pCmd);
    //Serial.printf("[DEBUG] Dest address: %p\r\n", &pCmd->length + 1);
    //Serial.printf("[DEBUG] Src address: %p\r\n", bufferIn);
    //Serial.printf("[DEBUG] Src length: %u\r\n", len);
    //Serial.flush();
    //vTaskDelay(pdMS_TO_TICKS(1000));
    //// --- END DEBUG ADDITIONS ---

    // Copy into pCmd just after the SwiMuxCmdWrite_t header last byte
    memcpy(&pCmd->length + 1, bufferIn, len);

    SwiMuxSerialResult_e result = SMREZ_WRITE_ENCODE_FAILED;
    if (_codec.encode((const uint8_t*)(void*)pCmd, sizeof(SwiMuxCmdWrite_t) + (size_t)len, [this](uint8_t wrtVal) { this->_sPort.write(wrtVal); })) {
        if (_codec.waitForAckTo(
              SMCMD_WriteBytes, millis, [this]() -> int { return this->_sPort.read(); }, [](unsigned long wms) { vTaskDelay(pdMS_TO_TICKS(wms)); },
              3000)) /* <-- timeout_ms = 13ms per character + 70ms per block of 8 bytes + 70ms for a single write */ {
            _isAwake = true;
            result   = SMREZ_OK;
        } else {
            result = (SwiMuxSerialResult_e)_codec.getLastAckError();
        }
    }
    free(pCmd);
    return result;
}
