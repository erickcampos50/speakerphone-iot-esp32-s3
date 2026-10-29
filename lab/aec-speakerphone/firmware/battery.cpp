#include "battery.h"

#include "board.h"

namespace {
// Curva de descarga aproximada de uma Li-ion (repouso).
struct Point {
  float volts;
  uint8_t percent;
};
constexpr Point kCurve[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60},
    {3.80f, 50},  {3.75f, 40}, {3.70f, 30}, {3.65f, 20}, {3.55f, 10},
    {3.45f, 5},   {3.30f, 0},
};
}  // namespace

float Battery::readVolts(int pin) {
  // Media de 8 amostras para reduzir ruido.
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(pin);
    delay(2);
  }
  float mv = sum / 8.0f;
  return (mv * BAT_DIVIDER_RATIO / 1000.0f) / BAT_CALIBRATION;
}

void Battery::begin() {
  analogReadResolution(12);

  // Testa os dois pinos candidatos e aceita o que tiver uma tensao
  // plausivel de bateria Li-ion (o pino do divisor fica em ~1.0-1.45 V).
  const int candidates[] = {PIN_BAT_ADC_SCHEMATIC, PIN_BAT_ADC_DEMO};
  float best = 0.0f;
  int best_pin = -1;

  for (int pin : candidates) {
    pinMode(pin, INPUT);
    float v = readVolts(pin);
    Serial.printf("[BAT] GPIO%d -> %.2f V\n", pin, v);
    if (v > 2.8f && v < 4.6f && v > best) {
      best = v;
      best_pin = pin;
    }
  }

  if (best_pin >= 0) {
    _available = true;
    _pin = best_pin;
    _volts = best;
    Serial.printf("[BAT] usando GPIO%d (%.2f V, %d%%)\n", _pin, _volts,
                  percent());
  } else {
    _available = false;
    _pin = -1;
    Serial.println(
        "[BAT] divisor nao detectado. O BAT_ADC vem desabilitado de fabrica:");
    Serial.println(
        "      solde o resistor 0R do BAT_ADC (GPIO1) para medir a bateria.");
  }
  _last_read_ms = millis();
}

void Battery::update(uint32_t now_ms) {
  if (!_available) return;
  if (now_ms - _last_read_ms < 5000) return;
  _last_read_ms = now_ms;
  _volts = readVolts(_pin);
}

int Battery::percent() const {
  if (!_available || _volts <= 0) return -1;
  if (_volts >= kCurve[0].volts) return 100;
  const size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
  for (size_t i = 1; i < n; i++) {
    if (_volts >= kCurve[i].volts) {
      float v0 = kCurve[i - 1].volts, v1 = kCurve[i].volts;
      float p0 = kCurve[i - 1].percent, p1 = kCurve[i].percent;
      float t = (_volts - v1) / (v0 - v1);
      return (int)(p1 + t * (p0 - p1) + 0.5f);
    }
  }
  return 0;
}
