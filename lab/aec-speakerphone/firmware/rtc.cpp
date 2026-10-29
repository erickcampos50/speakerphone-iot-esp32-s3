#include "rtc.h"

#include <Wire.h>
#include <time.h>

namespace {
constexpr uint8_t REG_SECONDS = 0x04;  // 0x04..0x0A: sec, min, hora, dia, dow, mes, ano
constexpr uint8_t OS_FLAG = 0x80;

uint8_t toBcd(int v) { return ((v / 10) << 4) | (v % 10); }
int fromBcd(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }
}  // namespace

bool RtcPcf85063::begin(uint8_t addr) {
  _addr = addr;
  Wire.beginTransmission(_addr);
  _present = (Wire.endTransmission() == 0);
  if (_present) {
    struct tm t;
    _valid = read(t);
  }
  return _present;
}

bool RtcPcf85063::readRaw(uint8_t *buf7) {
  Wire.beginTransmission(_addr);
  Wire.write(REG_SECONDS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(_addr, (uint8_t)7) != 7) return false;
  for (int i = 0; i < 7; i++) buf7[i] = Wire.read();
  return true;
}

bool RtcPcf85063::writeRaw(const uint8_t *buf7) {
  Wire.beginTransmission(_addr);
  Wire.write(REG_SECONDS);
  for (int i = 0; i < 7; i++) Wire.write(buf7[i]);
  return Wire.endTransmission() == 0;
}

bool RtcPcf85063::read(struct tm &out) {
  uint8_t b[7];
  if (!readRaw(b)) return false;

  // Bit 7 dos segundos = OS (oscillator stopped): hora invalida.
  if (b[0] & OS_FLAG) {
    _valid = false;
    return false;
  }

  int sec = fromBcd(b[0] & 0x7F);
  int min = fromBcd(b[1] & 0x7F);
  int hour;
  if (b[2] & 0x40) {  // modo 12h
    hour = fromBcd(b[2] & 0x1F);
    bool pm = b[2] & 0x20;
    if (pm && hour < 12) hour += 12;
    if (!pm && hour == 12) hour = 0;
  } else {
    hour = fromBcd(b[2] & 0x3F);
  }
  int day = fromBcd(b[3] & 0x3F);
  int month = fromBcd(b[5] & 0x1F);
  int year = 2000 + fromBcd(b[6]);

  if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || min > 59 || sec > 59) {
    _valid = false;
    return false;
  }

  memset(&out, 0, sizeof(out));
  out.tm_year = year - 1900;
  out.tm_mon = month - 1;
  out.tm_mday = day;
  out.tm_hour = hour;
  out.tm_min = min;
  out.tm_sec = sec;
  out.tm_isdst = -1;
  _valid = true;
  return true;
}

bool RtcPcf85063::set(int year, int month, int day, int hour, int minute,
                      int second) {
  if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  uint8_t b[7];
  b[0] = toBcd(second);  // OS = 0
  b[1] = toBcd(minute);
  b[2] = toBcd(hour);  // 24h
  b[3] = toBcd(day);
  b[4] = 0;  // dia da semana nao usado
  b[5] = toBcd(month);
  b[6] = toBcd(year % 100);

  if (!writeRaw(b)) return false;
  _valid = true;
  return true;
}

bool RtcPcf85063::stamp(char *out, size_t len) {
  struct tm t;
  if (!read(t)) return false;
  snprintf(out, len, "%04d-%02d-%02d_%02d%02d%02d", t.tm_year + 1900,
           t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}
