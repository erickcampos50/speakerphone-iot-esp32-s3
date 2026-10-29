#include "led_ring.h"

namespace {
constexpr uint8_t kBrightness = 48;  // limita corrente (bateria)
constexpr uint32_t kFrameMs = 33;    // ~30 fps
}  // namespace

void LedRing::begin() {
  _strip.begin();
  _strip.setBrightness(kBrightness);
  _strip.clear();
  _strip.show();
}

void LedRing::fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    _strip.setPixelColor(i, _strip.Color(r, g, b));
  }
}

void LedRing::bootAnimation() {
  // Varredura verde + duas piscadas.
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    _strip.clear();
    _strip.setPixelColor(i, _strip.Color(0, 90, 20));
    _strip.show();
    delay(60);
  }
  delay(80);
  for (int n = 0; n < 2; n++) {
    fill(0, 90, 20);
    _strip.show();
    delay(120);
    _strip.clear();
    _strip.show();
    delay(120);
  }
}

void LedRing::barColor(uint8_t percent, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (percent > 50) {
    r = 0;
    g = 120;
    b = 0;
  } else if (percent > 20) {
    r = 120;
    g = 80;
    b = 0;
  } else {
    r = 140;
    g = 0;
    b = 0;
  }
}

void LedRing::drawBar(uint8_t percent, uint8_t r, uint8_t g, uint8_t b) {
  if (percent > 100) percent = 100;
  uint8_t lit = (uint8_t)((percent * LED_COUNT + 99) / 100);
  if (percent > 0 && lit == 0) lit = 1;
  _strip.clear();
  for (uint8_t i = 0; i < lit; i++) {
    _strip.setPixelColor(i, _strip.Color(r, g, b));
  }
}

void LedRing::setMode(LedMode mode) { _mode = mode; }

void LedRing::flashConfirm() { _flash_until = millis() + 150; }

void LedRing::showBattery(uint8_t percent) {
  _bat_overlay = true;
  _bat_pct = percent;
  _bat_overlay_until = millis() + 2000;
}

void LedRing::showStatus(uint8_t bat_pct, uint8_t free_pct) {
  _status_active = true;
  _status_start = millis();
  _status_bat = bat_pct;
  _status_free = free_pct;
}

void LedRing::update(uint32_t now_ms) {
  if (now_ms - _last_show_ms < kFrameMs) return;
  _last_show_ms = now_ms;

  _smooth = (_smooth * 3 + _level) / 4;

  // Flash de confirmacao tem prioridade.
  if (now_ms < _flash_until) {
    fill(110, 110, 110);
    _strip.show();
    return;
  }

  // Sequencia de status (bateria -> espaco livre).
  if (_status_active) {
    uint32_t dt = now_ms - _status_start;
    if (dt < 2000) {
      uint8_t r, g, b;
      barColor(_status_bat, r, g, b);
      drawBar(_status_bat, r, g, b);
      _strip.show();
      return;
    }
    if (dt < 4000) {
      drawBar(_status_free, 0, 110, 110);  // ciano = espaco livre
      _strip.show();
      return;
    }
    _status_active = false;
  }

  // Barra de bateria periodica (ocioso).
  if (_bat_overlay) {
    if (now_ms < _bat_overlay_until) {
      uint8_t r, g, b;
      barColor(_bat_pct, r, g, b);
      drawBar(_bat_pct, r, g, b);
      _strip.show();
      return;
    }
    _bat_overlay = false;
  }

  switch (_mode) {
    case LedMode::IdleUsb: {
      float phase = (now_ms % 3000) / 3000.0f;
      float breath = 0.5f - 0.5f * cosf(phase * 2.0f * PI);
      uint8_t v = (uint8_t)(10 + breath * 80);
      fill(0, v / 4, v);  // azul
      break;
    }
    case LedMode::IdleBattery: {
      float phase = (now_ms % 3000) / 3000.0f;
      float breath = 0.5f - 0.5f * cosf(phase * 2.0f * PI);
      uint8_t v = (uint8_t)(10 + breath * 80);
      fill(0, v, v / 3);  // verde
      break;
    }
    case LedMode::LowBattery: {
      float phase = (now_ms % 1200) / 1200.0f;
      float breath = 0.5f - 0.5f * cosf(phase * 2.0f * PI);
      uint8_t v = (uint8_t)(15 + breath * 85);
      fill(v, v / 3, 0);  // laranja
      break;
    }
    case LedMode::Critical: {
      float phase = (now_ms % 1500) / 1500.0f;
      float breath = 0.5f - 0.5f * cosf(phase * 2.0f * PI);
      uint8_t v = (uint8_t)(10 + breath * 90);
      fill(v, 0, 0);  // vermelho lento
      break;
    }
    case LedMode::Error: {
      // 3 piscadas vermelhas a cada 2 s.
      uint32_t t = now_ms % 2000;
      bool on = (t < 120) || (t >= 260 && t < 380) || (t >= 520 && t < 640);
      if (on) {
        fill(150, 0, 0);
      } else {
        _strip.clear();
      }
      break;
    }
    case LedMode::Recording: {
      // VU meter verde -> amarelo -> vermelho.
      uint8_t lit = (uint8_t)((_smooth * LED_COUNT + 99) / 100);
      if (lit > LED_COUNT) lit = LED_COUNT;
      _strip.clear();
      for (uint8_t i = 0; i < lit; i++) {
        uint8_t r, g;
        if (i < 4) {
          r = (uint8_t)(i * 45);
          g = 110;
        } else {
          r = 150;
          g = (uint8_t)(110 - (i - 4) * 35);
        }
        _strip.setPixelColor(i, _strip.Color(r, g, 0));
      }
      // LED 0 pisca em vermelho para indicar gravacao ativa.
      if ((now_ms / 500) % 2 == 0) {
        _strip.setPixelColor(0, _strip.Color(180, 0, 0));
      }
      break;
    }
  }
  _strip.show();
}
