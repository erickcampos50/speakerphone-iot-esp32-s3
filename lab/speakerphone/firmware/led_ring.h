#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "board.h"

enum class LedMode {
  IdleUsb,      // ligado, alimentado pela USB (azul)
  IdleBattery,  // ligado, alimentado pela bateria (verde)
  LowBattery,   // bateria < 15% (laranja)
  Critical,     // bateria critica (vermelho lento)
  Recording,    // gravando (VU meter)
  Error,        // falha de SD / codec (vermelho 3 piscadas)
};

// Anel de 7 WS2812B (GPIO38).
class LedRing {
 public:
  void begin();
  void bootAnimation();

  void setMode(LedMode mode);
  void setLevel(uint8_t percent) { _level = percent; }
  void update(uint32_t now_ms);

  void flashConfirm();                              // flash branco curto
  void showBattery(uint8_t percent);                // barra por 2 s
  void showStatus(uint8_t bat_pct, uint8_t free_pct);  // 2 s + 2 s

 private:
  void fill(uint8_t r, uint8_t g, uint8_t b);
  void drawBar(uint8_t percent, uint8_t r, uint8_t g, uint8_t b);
  static void barColor(uint8_t percent, uint8_t &r, uint8_t &g, uint8_t &b);

  Adafruit_NeoPixel _strip{LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800};
  LedMode _mode = LedMode::IdleUsb;
  uint8_t _level = 0;
  uint8_t _smooth = 0;
  uint32_t _last_show_ms = 0;

  uint32_t _flash_until = 0;

  bool _bat_overlay = false;
  uint32_t _bat_overlay_until = 0;
  uint8_t _bat_pct = 0;

  bool _status_active = false;
  uint32_t _status_start = 0;
  uint8_t _status_bat = 0;
  uint8_t _status_free = 0;
};
