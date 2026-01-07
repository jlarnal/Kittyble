#include "SSD1680Driver.h"
#include "SSD1680_LUT.h"

// clang-format off

#define USE_CUSTOM_LUT (1)


static const uint8_t ssd1680_custom_init_code[] { 
    /* Command, args count, [args]  */
    SSD1680_SW_RESET, 0, // soft reset
    0xFF, 20, // busy wait (20ms)
    SSD1680_DATA_MODE, 1, 0x03, // Ram data entry mode
    SSD1680_WRITE_BORDER, 1, 0x05, // border color

    SSD1680_WRITE_VCOM, 1, 0x36, // Vcom Voltage
    SSD1680_GATE_VOLTAGE, 1, 0x17, // Set gate voltage
    SSD1680_SOURCE_VOLTAGE, 3, 0x46, 0xA8, 0x36, // 0x41, 0xA8, 0x32, // Set source voltage
    
    #if USE_CUSTOM_LUT
    SSD1680_END_OPTION, 1, 0x07, // Keep LUT levels at power-down.

    SSD1680_WRITE_LUT, 153,
        // We're only using the first two groups (one and a half to be exact)
        vDUDU, vUU__, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, // L0 (BKBK) groups [0..11]
        vUDUD, vDD__, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, // L1 (BKWH)
        vDUDU, vUU__, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, // L2 (WHBK)
        vUDUD, vDD__, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, // L3 (WHWH)
        v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, v____, // L4 (not used)
        // First phase is a 50% square oscillation between VSH and VSL, repeated 25 time
        // then 80 frames of the final polarity (U/VSH for black, D/VSL for white) without repetition
        // The whole sequence is only done once.        
        1   , 1   , 15  , 1   , 1   , 15   , 0   , // Group #0 timing 
        1   , 1   , 15  , 1   , 1   , 15   , 0   , // Group #1 timing 
        1   , 1   , 15  , 1   , 1   , 15   , 0   , // Group #2 timing 
        1   , 1   , 15  , 1   , 1   , 15   , 0   , // Group #3 timing 
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #4 timing 
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #5 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #6 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #7 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #8 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #9 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #10 timing  
        0   , 0   , 0   , 0   , 0   , 0   , 0   , // Group #11 timing 
        0x44, 0x44, 0x77, 0x77, 0x77, 0x77, // FR[n]
        0    , 0    , 0    , // XON[n]
    #endif
  

    SSD1680_SET_RAMXCOUNT, 1, 1, 
    SSD1680_SET_RAMYCOUNT, 2, 0, 0, 
    0xFE , // <-- 0xFE is the "end of list" token.
};

// clang-format on

void Ssd1680_Driver::powerUp()
{
    uint8_t buf[5];

    hardwareReset();
    delay(100);
    busy_wait();

    const uint8_t* init_code = ssd1680_custom_init_code;

    if (_epd_init_code != NULL) {
        init_code = _epd_init_code;
    }
    EPD_commandList(init_code);

    uint8_t height = HEIGHT;
    if ((height % 8) != 0) {
        height += 8 - (height % 8);
    }
    
    // Set ram X start/end postion
    buf[0] = _xram_offset;
    buf[1] = height / 8 - 1 + _xram_offset;
    EPD_command(SSD1680_SET_RAMXPOS, buf, 2);

    // Set ram Y start/end postion
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = (WIDTH - 1);
    buf[3] = (WIDTH - 1) >> 8;
    EPD_command(SSD1680_SET_RAMYPOS, buf, 4);

    // Set LUT (if we have one)
    if (_epd_lut_code) {
        EPD_commandList(_epd_lut_code);
    }

    // Set display size and driver output control
    buf[0] = (WIDTH - 1);
    buf[1] = (WIDTH - 1) >> 8;
    buf[2] = 0;
    EPD_command(SSD1680_DRIVER_CONTROL, buf, 3);
}

void Ssd1680_Driver::update()
{
    uint8_t buf[1];

#if USE_CUSTOM_LUT
    buf[0] = 0xC7; // do not load temperature value.
#else
    buf[0] = 0xF7;
#endif
    EPD_command(SSD1680_DISP_CTRL2, buf, 1);
    EPD_command(SSD1680_MASTER_ACTIVATE);
    busy_wait();

    if(_busy_pin <= -1) {
        ESP_LOGI("SSD1680", "No busy pin defined, waiting for 1s.");
        delay(1000);
    }
}