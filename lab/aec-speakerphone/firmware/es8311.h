#pragma once

#include <Arduino.h>

// Driver minimo do ES8311 (DAC / saida de audio) via Wire.
// A sequencia de inicializacao foi portada do driver oficial da Espressif
// (es8311.c) usado no demo da Waveshare para esta placa:
//   - MCLK vindo do pino MCLK, 256 * fs
//   - 16 kHz, 16 bits, formato I2S, codec em modo slave
// O ADC do ES8311 nao e usado nesta placa (o microfone e o ES7210), por isso
// o bloco de configuracao do microfone do driver oficial foi omitido.
class Es8311 {
 public:
  // Detecta o codec no barramento e aplica a configuracao completa.
  // O I2C (Wire) precisa estar iniciado e o MCLK precisa estar presente
  // (o I2S ja rodando) antes desta chamada.
  bool begin(uint8_t addr = 0x18);

  // Volume do DAC, 0..100 (%). 0 = mudo, 60 = padrao do modulo.
  bool setVolume(uint8_t percent);

  bool present() const { return _present; }
  uint8_t lastError() const { return _lastError; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readReg(uint8_t reg, uint8_t *value);
  bool configureClock(uint32_t sample_rate, uint32_t mclk_ratio);
  bool configureFormat();

  uint8_t _addr = 0x18;
  bool _present = false;
  uint8_t _lastError = 0;
};
