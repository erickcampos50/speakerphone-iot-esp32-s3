#pragma once

#include <Arduino.h>

// Leitura da tensao da bateria (Li-ion 3.7 V) atraves do divisor da placa.
// O pino de BAT_ADC depende da revisao/configuracao: o esquematico indica
// GPIO1 e o demo oficial usa GPIO8. O firmware testa os dois no boot.
class Battery {
 public:
  void begin();
  void update(uint32_t now_ms);

  bool available() const { return _available; }
  int pin() const { return _pin; }
  float volts() const { return _volts; }
  int percent() const;
  bool low() const { return _available && _volts > 0 && _volts < 3.45f; }
  bool critical() const { return _available && _volts > 0 && _volts < 3.30f; }

 private:
  static float readVolts(int pin);

  bool _available = false;
  int _pin = -1;
  float _volts = 0.0f;
  uint32_t _last_read_ms = 0;
};
