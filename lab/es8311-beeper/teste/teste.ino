// ---------------------------------------------------------------------------
// Teste do modulo de audio de saida (ES8311 + Beeper)
// ESP32-S3-AUDIO-Board - nao grava nada: apenas toca as sequencias em ordem,
// uma a cada 1,5 s, para conferir o som no alto-falante.
//
// Layout: o arduino-cli so compila fontes que estejam dentro da pasta do
// sketch, por isso o modulo aparece em teste/src/ (copias identicas dos
// arquivos de lab/es8311-beeper/). No firmware principal eles entram como
// fontes normais da pasta firmware/.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>

#include "ESP_I2S.h"

#include "src/beeper.h"
#include "src/es8311.h"

namespace {

// ---------------------------------------------------------------------------
// Pinagem (identica a firmware/board.h)
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_I2C_SDA = 11;
constexpr uint8_t PIN_I2C_SCL = 10;
constexpr uint32_t I2C_FREQ_HZ = 100000;

constexpr uint8_t PIN_I2S_MCLK = 12;
constexpr uint8_t PIN_I2S_BCLK = 13;
constexpr uint8_t PIN_I2S_LRCK = 14;
constexpr uint8_t PIN_I2S_DIN = 15;   // ES7210 -> ESP32
constexpr uint8_t PIN_I2S_DOUT = 16;  // ESP32 -> ES8311

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;

// TCA9555: EXIO8 alto liga o amplificador NS4150B.
constexpr uint8_t TCA_ADDR = 0x20;
constexpr uint8_t TCA_REG_OUTPUT1 = 0x03;
constexpr uint8_t TCA_REG_CONFIG1 = 0x07;
constexpr uint8_t EXIO_PA_CTRL = 8;

constexpr uint32_t kIntervaloMs = 1500;

I2SClass i2s;
Beeper beeper;

// Estado das saidas do TCA9555 (EXIO7 alto mantem o roteamento USB).
uint8_t tca_out1 = 0xFE;

// Ordem de execucao do teste (todos os eventos).
const BeepEvent kOrdem[] = {
    BeepEvent::Boot,       BeepEvent::RecordStart, BeepEvent::RecordStop,
    BeepEvent::LowBattery, BeepEvent::Error,       BeepEvent::WifiReady,
    BeepEvent::AmpOn,      BeepEvent::AmpOff,
};
constexpr uint8_t kOrdemCount = (uint8_t)(sizeof(kOrdem) / sizeof(kOrdem[0]));

uint8_t indice = 0;
uint32_t ultimo_ms = 0;
bool tocando = false;

bool tcaWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool tcaBegin() {
  if (!tcaWrite(TCA_REG_OUTPUT1, tca_out1)) return false;
  return tcaWrite(TCA_REG_CONFIG1, 0x0E);  // EXIO9..11 entradas, resto saida
}

// No firmware principal isto e exio.write(EXIO_PA_CTRL, on).
void ampControl(bool on) {
  if (on) {
    tca_out1 |= (uint8_t)(1u << (EXIO_PA_CTRL & 7));
  } else {
    tca_out1 &= (uint8_t)~(1u << (EXIO_PA_CTRL & 7));
  }
  tcaWrite(TCA_REG_OUTPUT1, tca_out1);
  Serial.printf("[AMP] %s\n", on ? "ligado" : "desligado");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== teste ES8311 + Beeper ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

  if (tcaBegin()) {
    Serial.println("[IO] TCA9555 ok");
  } else {
    Serial.println("[IO] TCA9555 NAO respondeu (0x20): sem amplificador");
  }

  // O mesmo begin() do firmware: DOUT e DIN definidos criam TX e RX.
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN,
              PIN_I2S_MCLK);
  i2s.setTimeout(1000);
  if (i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO)) {
    Serial.println("[I2S] ok 16 kHz / 16 bits / estereo (RX + TX)");
  } else {
    Serial.println("[I2S] falha ao iniciar");
  }

  beeper.setAmpControl(ampControl);
  if (beeper.begin(&i2s)) {
    beeper.setVolume(60);
    Serial.println("[OUT] ES8311 ok, volume 60%");
  } else {
    Serial.println("[OUT] falha: ES8311 ausente ou TX indisponivel");
  }

  ultimo_ms = millis();
}

void loop() {
  beeper.update();

  if (!tocando) {
    if (millis() - ultimo_ms >= kIntervaloMs) {
      Serial.printf("[BEEP] evento %u de %u\n", (unsigned)(indice + 1),
                    (unsigned)kOrdemCount);
      beeper.play(kOrdem[indice]);
      indice = (uint8_t)((indice + 1) % kOrdemCount);
      tocando = true;
    }
  } else if (!beeper.busy()) {
    tocando = false;
    ultimo_ms = millis();
  }
}
