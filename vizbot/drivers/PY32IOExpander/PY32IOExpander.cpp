/*
 * PY32IOExpander — IO expander driver for PY32L020 (I2C 0x6F)
 * Source: M5Stack StackChan-BSP (MIT License)
 * SPDX-License-Identifier: MIT
 */
#ifdef BOARD_HAS_STACKCHAN_BASE

#include "PY32IOExpander.hpp"

namespace m5 {

static constexpr uint8_t REG_UID_L      = 0x00;
static constexpr uint8_t REG_UID_H      = 0x01;
static constexpr uint8_t REG_VERSION    = 0x02;
static constexpr uint8_t REG_GPIO_M_L   = 0x03;
static constexpr uint8_t REG_GPIO_M_H   = 0x04;
static constexpr uint8_t REG_GPIO_O_L   = 0x05;
static constexpr uint8_t REG_GPIO_O_H   = 0x06;
static constexpr uint8_t REG_GPIO_I_L   = 0x07;
static constexpr uint8_t REG_GPIO_I_H   = 0x08;
static constexpr uint8_t REG_GPIO_PU_L  = 0x09;
static constexpr uint8_t REG_GPIO_PU_H  = 0x0A;
static constexpr uint8_t REG_GPIO_PD_L  = 0x0B;
static constexpr uint8_t REG_GPIO_PD_H  = 0x0C;
static constexpr uint8_t REG_GPIO_IE_L  = 0x0D;
static constexpr uint8_t REG_GPIO_IE_H  = 0x0E;
static constexpr uint8_t REG_GPIO_IS_L  = 0x11;
static constexpr uint8_t REG_GPIO_IS_H  = 0x12;
static constexpr uint8_t REG_GPIO_DRV_L = 0x13;
static constexpr uint8_t REG_GPIO_DRV_H = 0x14;
static constexpr uint8_t REG_LED_CFG       = 0x24;
static constexpr uint8_t REG_LED_RAM_START = 0x30;

void PY32IOExpander_Class::_writeBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin, bool value)
{
    if (pin < 8) {
        if (value)
            bitOn(reg_l, 1 << pin);
        else
            bitOff(reg_l, 1 << pin);
    } else {
        if (value)
            bitOn(reg_h, 1 << (pin - 8));
        else
            bitOff(reg_h, 1 << (pin - 8));
    }
}

bool PY32IOExpander_Class::_readBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin)
{
    if (pin < 8) {
        return (readRegister8(reg_l) & (1 << pin)) != 0;
    } else {
        return (readRegister8(reg_h) & (1 << (pin - 8))) != 0;
    }
}

bool PY32IOExpander_Class::begin()
{
    uint8_t version = readRegister8(REG_VERSION);
    if (version == 0 || version == 0xFF) return false;
    return true;
}

void PY32IOExpander_Class::setDirection(uint8_t pin, bool direction)
{
    _writeBit(REG_GPIO_M_L, REG_GPIO_M_H, pin, direction);
}

void PY32IOExpander_Class::enablePull(uint8_t pin, bool enablePull)
{
    if (enablePull) {
        bool pu = _readBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin);
        bool pd = _readBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin);
        if (!pu && !pd) {
            _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, true);
        }
    } else {
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, false);
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, false);
    }
}

void PY32IOExpander_Class::setPullMode(uint8_t pin, bool mode)
{
    if (mode) {
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, false);
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, true);
    } else {
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, false);
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, true);
    }
}

void PY32IOExpander_Class::setDriveMode(uint8_t pin, bool openDrain)
{
    _writeBit(REG_GPIO_DRV_L, REG_GPIO_DRV_H, pin, openDrain);
}

void PY32IOExpander_Class::setHighImpedance(uint8_t pin, bool enable)
{
    if (enable) {
        setDirection(pin, false);
        enablePull(pin, false);
    }
}

bool PY32IOExpander_Class::getWriteValue(uint8_t pin)
{
    return _readBit(REG_GPIO_O_L, REG_GPIO_O_H, pin);
}

void PY32IOExpander_Class::digitalWrite(uint8_t pin, bool level)
{
    _writeBit(REG_GPIO_O_L, REG_GPIO_O_H, pin, level);
}

bool PY32IOExpander_Class::digitalRead(uint8_t pin)
{
    return _readBit(REG_GPIO_I_L, REG_GPIO_I_H, pin);
}

void PY32IOExpander_Class::resetIrq()
{
    writeRegister8(REG_GPIO_IS_L, 0xFF);
    writeRegister8(REG_GPIO_IS_H, 0xFF);
}

void PY32IOExpander_Class::disableIrq()
{
    writeRegister8(REG_GPIO_IE_L, 0x00);
    writeRegister8(REG_GPIO_IE_H, 0x00);
}

void PY32IOExpander_Class::enableIrq()
{
    writeRegister8(REG_GPIO_IE_L, 0xFF);
    writeRegister8(REG_GPIO_IE_H, 0x3F);
}

uint16_t PY32IOExpander_Class::readDeviceUID()
{
    uint8_t l = readRegister8(REG_UID_L);
    uint8_t h = readRegister8(REG_UID_H);
    return (h << 8) | l;
}

uint8_t PY32IOExpander_Class::readVersion()
{
    return readRegister8(REG_VERSION);
}

void PY32IOExpander_Class::setLedCount(uint8_t count)
{
    if (count > 32) count = 32;
    writeRegister8(REG_LED_CFG, count & 0x3F);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint16_t color565)
{
    if (index >= 32) return;
    uint8_t data[2] = {(uint8_t)(color565 & 0xFF), (uint8_t)((color565 >> 8) & 0xFF)};
    writeRegister(REG_LED_RAM_START + index * 2, data, 2);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    setLedColor(index, val);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint32_t color)
{
    setLedColor(index, (uint8_t)((color >> 16) & 0xFF), (uint8_t)((color >> 8) & 0xFF), (uint8_t)(color & 0xFF));
}

void PY32IOExpander_Class::setLedData(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return;
    if (len > 64) len = 64;
    writeRegister(REG_LED_RAM_START, (uint8_t*)data, len);
}

void PY32IOExpander_Class::refreshLeds()
{
    uint8_t val = readRegister8(REG_LED_CFG);
    writeRegister8(REG_LED_CFG, val | (1 << 6));
}

}  // namespace m5

#endif // BOARD_HAS_STACKCHAN_BASE
