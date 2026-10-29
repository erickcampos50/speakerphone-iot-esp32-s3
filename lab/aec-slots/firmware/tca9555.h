#pragma once

#include <Arduino.h>

// Driver minimo do TCA9555PWR (expansor I2C de 16 IO).
// Registradores: 0x00/0x01 entrada, 0x02/0x03 saida, 0x06/0x07 config.
class Tca9555 {
 public:
  bool begin(uint8_t addr = 0x20);
  bool pinMode(uint8_t pin, bool input);
  bool write(uint8_t pin, bool level);
  int read(uint8_t pin);  // -1 = erro de I2C, 0/1 = nivel
  bool writePort(uint16_t levels);
  int readPort();  // -1 = erro de I2C, senao os 16 bits lidos
  bool ok() const { return _ok; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readReg(uint8_t reg, uint8_t &value);

  uint8_t _addr = 0x20;
  uint8_t _out[2] = {0xFF, 0xFF};
  bool _ok = false;
};
