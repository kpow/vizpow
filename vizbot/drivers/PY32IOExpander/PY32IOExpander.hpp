/*
 * PY32IOExpander — IO expander driver for PY32L020 (I2C 0x6F)
 * Source: M5Stack StackChan-BSP (MIT License)
 * SPDX-License-Identifier: MIT
 */
#ifndef __M5_PY32IOEXPANDER_CLASS_H__
#define __M5_PY32IOEXPANDER_CLASS_H__

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <M5Unified.hpp>

namespace m5 {
class PY32IOExpander_Class : public IOExpander_Base {
public:
    static constexpr std::uint8_t DEFAULT_ADDRESS = 0x6F;

    PY32IOExpander_Class(std::uint8_t i2c_addr = DEFAULT_ADDRESS, std::uint32_t freq = 100000,
                         m5::I2C_Class* i2c = &m5::In_I2C)
        : IOExpander_Base(i2c_addr, freq, i2c)
    {
    }

    bool begin();

    // false input, true output
    void setDirection(uint8_t pin, bool direction) override;
    void enablePull(uint8_t pin, bool enablePull) override;
    // false down, true up
    void setPullMode(uint8_t pin, bool mode) override;
    // false push-pull, true open-drain
    void setDriveMode(uint8_t pin, bool openDrain);
    void setHighImpedance(uint8_t pin, bool enable) override;
    bool getWriteValue(uint8_t pin) override;
    void digitalWrite(uint8_t pin, bool level) override;
    bool digitalRead(uint8_t pin) override;
    void resetIrq() override;
    void disableIrq() override;
    void enableIrq() override;

    uint16_t readDeviceUID();
    uint8_t readVersion();

    // LED
    void setLedCount(uint8_t count);
    void setLedColor(uint8_t index, uint16_t color565);
    void setLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
    void setLedColor(uint8_t index, uint32_t color);
    void setLedData(const uint8_t* data, size_t len);
    void refreshLeds();

private:
    void _writeBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin, bool value);
    bool _readBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin);
};
}  // namespace m5

#endif // BOARD_HAS_STACKCHAN_BASE
#endif
