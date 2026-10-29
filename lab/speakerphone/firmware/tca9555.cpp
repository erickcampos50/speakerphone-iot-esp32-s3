#include "tca9555.h"

#include <Wire.h>

#include "board.h"

namespace {
constexpr uint8_t REG_INPUT0 = 0x00;
constexpr uint8_t REG_INPUT1 = 0x01;
constexpr uint8_t REG_OUTPUT0 = 0x02;
constexpr uint8_t REG_OUTPUT1 = 0x03;
constexpr uint8_t REG_CONFIG0 = 0x06;
constexpr uint8_t REG_CONFIG1 = 0x07;
}  // namespace

bool Tca9555::begin(uint8_t addr) {
  _addr = addr;

  // Estado seguro logo apos o power-on:
  //  - EXIO7 precisa ficar ALTO (mantem o roteamento USB em GPIO19/20)
  //  - EXIO3 alto (D3/CS do SD)
  //  - EXIO8 baixo (amplificador desligado)
  //  - EXIO9..11 como entrada (teclas K1..K3)
  _out[0] = 0xFF;
  _out[1] = 0xFE;

  if (!writeReg(REG_OUTPUT0, _out[0])) return _ok = false;
  if (!writeReg(REG_OUTPUT1, _out[1])) return _ok = false;
  if (!writeReg(REG_CONFIG0, 0x00)) return _ok = false;  // todos saida
  if (!writeReg(REG_CONFIG1, 0x0E)) return _ok = false;  // EXIO9..11 entrada

  _ok = true;
  return true;
}

bool Tca9555::pinMode(uint8_t pin, bool input) {
  uint8_t reg = pin < 8 ? REG_CONFIG0 : REG_CONFIG1;
  uint8_t value = 0;
  if (!readReg(reg, value)) return false;
  if (input) {
    value |= (1u << (pin & 7));
  } else {
    value &= ~(1u << (pin & 7));
  }
  return writeReg(reg, value);
}

bool Tca9555::write(uint8_t pin, bool level) {
  uint8_t port = pin < 8 ? 0 : 1;
  uint8_t bit = pin & 7;
  if (level) {
    _out[port] |= (1u << bit);
  } else {
    _out[port] &= ~(1u << bit);
  }
  return writeReg(port == 0 ? REG_OUTPUT0 : REG_OUTPUT1, _out[port]);
}

int Tca9555::read(uint8_t pin) {
  uint8_t value = 0;
  if (!readReg(pin < 8 ? REG_INPUT0 : REG_INPUT1, value)) return -1;
  return (value >> (pin & 7)) & 0x01;
}

bool Tca9555::writePort(uint16_t levels) {
  _out[0] = levels & 0xFF;
  _out[1] = levels >> 8;
  return writeReg(REG_OUTPUT0, _out[0]) && writeReg(REG_OUTPUT1, _out[1]);
}

int Tca9555::readPort() {
  uint8_t lo = 0, hi = 0;
  if (!readReg(REG_INPUT0, lo) || !readReg(REG_INPUT1, hi)) return -1;
  return (int)((uint16_t)lo | ((uint16_t)hi << 8));
}

bool Tca9555::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Tca9555::readReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(_addr, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}
