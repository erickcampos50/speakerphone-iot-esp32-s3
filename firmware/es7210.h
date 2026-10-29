#pragma once

#include <Arduino.h>

// Driver minimo do ES7210 (ADC de 4 canais / microfones) via Wire.
// Sequencia de inicializacao identica ao driver oficial da Espressif,
// usada no demo da Waveshare para esta placa.
class Es7210 {
 public:
  bool begin(uint8_t addr = 0x40);
  bool configure(uint32_t sample_rate = 16000, uint32_t mclk_ratio = 256,
                 uint8_t mic_gain_enum = 10 /* 30 dB */, bool tdm = true);
  bool setVolumeDb(int8_t db);
  bool present() const { return _present; }
  uint8_t lastError() const { return _lastError; }
  uint8_t errorCount() const { return _errors; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);

  uint8_t _addr = 0x40;
  bool _present = false;
  uint8_t _lastError = 0;
  uint8_t _errors = 0;
};
