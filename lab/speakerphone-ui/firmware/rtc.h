#pragma once

#include <Arduino.h>
#include <time.h>

// Driver minimo do RTC PCF85063 (I2C 0x51).
class RtcPcf85063 {
 public:
  bool begin(uint8_t addr = 0x51);
  bool present() const { return _present; }

  // Le a hora atual. Retorna false se o oscilador parou (flag OS) ou se os
  // valores sao implausiveis.
  bool read(struct tm &out);

  bool set(int year, int month, int day, int hour, int minute, int second);

  // "2026-10-28_143012" (sem extensao)
  bool stamp(char *out, size_t len);

  bool valid() const { return _valid; }

 private:
  bool readRaw(uint8_t *buf7);
  bool writeRaw(const uint8_t *buf7);

  uint8_t _addr = 0x51;
  bool _present = false;
  bool _valid = false;
};
