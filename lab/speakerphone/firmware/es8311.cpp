#include "es8311.h"

#include <Wire.h>

// Registradores do ES8311 usados na inicializacao.
namespace es8311_reg {
constexpr uint8_t RESET = 0x00;
constexpr uint8_t CLK_MANAGER01 = 0x01;
constexpr uint8_t CLK_MANAGER02 = 0x02;
constexpr uint8_t CLK_MANAGER03 = 0x03;
constexpr uint8_t CLK_MANAGER04 = 0x04;
constexpr uint8_t CLK_MANAGER05 = 0x05;
constexpr uint8_t CLK_MANAGER06 = 0x06;
constexpr uint8_t CLK_MANAGER07 = 0x07;
constexpr uint8_t CLK_MANAGER08 = 0x08;
constexpr uint8_t SDPIN = 0x09;   // porta serial do DAC
constexpr uint8_t SDPOUT = 0x0A;  // porta serial do ADC
constexpr uint8_t SYSTEM0D = 0x0D;
constexpr uint8_t SYSTEM0E = 0x0E;
constexpr uint8_t SYSTEM12 = 0x12;
constexpr uint8_t SYSTEM13 = 0x13;
constexpr uint8_t ADC1C = 0x1C;
constexpr uint8_t DAC32 = 0x32;  // volume do DAC
constexpr uint8_t DAC37 = 0x37;  // rampa / equalizador do DAC
}  // namespace es8311_reg

namespace {
// Coeficientes do driver oficial para MCLK = 256 * fs (16 kHz -> 4,096 MHz).
constexpr uint8_t kPreDiv = 1;
constexpr uint8_t kPreMulti = 0;
constexpr uint8_t kAdcDiv = 1;
constexpr uint8_t kDacDiv = 1;
constexpr uint8_t kFsMode = 0;    // single speed
constexpr uint8_t kLrckH = 0x00;
constexpr uint8_t kLrckL = 0xFF;
constexpr uint8_t kBclkDiv = 4;
constexpr uint8_t kAdcOsr = 0x10;
constexpr uint8_t kDacOsr = 0x10;

constexpr uint8_t kDefaultVolume = 60;
}  // namespace

bool Es8311::begin(uint8_t addr) {
  using namespace es8311_reg;

  _addr = addr;

  // Sonda o barramento: ACK = chip presente.
  Wire.beginTransmission(_addr);
  _present = (Wire.endTransmission() == 0);
  if (!_present) return false;

  // Reset e sequencia de power-on do driver oficial.
  if (!writeReg(RESET, 0x1F)) return false;
  delay(20);
  if (!writeReg(RESET, 0x00)) return false;
  if (!writeReg(RESET, 0x80)) return false;  // comando de power-on

  if (!configureClock(16000, 256)) return false;
  if (!configureFormat()) return false;

  // Alimentacao analogica, DAC e driver de saida (valores do driver oficial).
  if (!writeReg(SYSTEM0D, 0x01)) return false;
  if (!writeReg(SYSTEM0E, 0x02)) return false;
  if (!writeReg(SYSTEM12, 0x00)) return false;  // liga o DAC
  if (!writeReg(SYSTEM13, 0x10)) return false;  // liga a saida de fone
  if (!writeReg(ADC1C, 0x6A)) return false;     // equalizador do ADC bypass
  if (!writeReg(DAC37, 0x08)) return false;     // equalizador do DAC bypass

  return setVolume(kDefaultVolume);
}

bool Es8311::configureClock(uint32_t sample_rate, uint32_t mclk_ratio) {
  using namespace es8311_reg;

  // A tabela de coeficientes do driver oficial cobre 16 kHz com MCLK de
  // 4,096 MHz; outras combinacoes nao sao suportadas por este driver minimo.
  if (sample_rate != 16000 || mclk_ratio != 256) {
    _lastError = 0xFE;  // combinacao nao suportada
    return false;
  }

  uint8_t regv = 0;

  // MCLK vem do pino MCLK e todos os clocks internos ficam habilitados.
  if (!writeReg(CLK_MANAGER01, 0x3F)) return false;

  // BCLK sem inversao (bit 5 = 0).
  if (!readReg(CLK_MANAGER06, &regv)) return false;
  regv &= (uint8_t)~0x20;
  if (!writeReg(CLK_MANAGER06, regv)) return false;

  // Pre-divisor e multiplicador de clock.
  if (!readReg(CLK_MANAGER02, &regv)) return false;
  regv &= 0x07;
  regv |= (uint8_t)((kPreDiv - 1) << 5);
  regv |= (uint8_t)(kPreMulti << 3);
  if (!writeReg(CLK_MANAGER02, regv)) return false;

  // Modo de velocidade e oversampling do ADC.
  if (!writeReg(CLK_MANAGER03, (uint8_t)((kFsMode << 6) | kAdcOsr))) return false;

  // Oversampling do DAC.
  if (!writeReg(CLK_MANAGER04, kDacOsr)) return false;

  // Divisores de clock do ADC e do DAC.
  if (!writeReg(CLK_MANAGER05,
                (uint8_t)(((kAdcDiv - 1) << 4) | (kDacDiv - 1)))) {
    return false;
  }

  // Divisor de BCLK (valores abaixo de 19 entram como div - 1).
  if (!readReg(CLK_MANAGER06, &regv)) return false;
  regv &= 0xE0;
  if (kBclkDiv < 19) {
    regv |= (uint8_t)((kBclkDiv - 1) << 0);
  } else {
    regv |= kBclkDiv;
  }
  if (!writeReg(CLK_MANAGER06, regv)) return false;

  // Divisor de LRCK (parte alta e parte baixa).
  if (!readReg(CLK_MANAGER07, &regv)) return false;
  regv &= 0xC0;
  regv |= kLrckH;
  if (!writeReg(CLK_MANAGER07, regv)) return false;
  if (!writeReg(CLK_MANAGER08, kLrckL)) return false;

  return true;
}

bool Es8311::configureFormat() {
  using namespace es8311_reg;

  // Codec em modo slave (bit 6 = 0): BCLK e LRCK vem do ESP32.
  uint8_t reg00 = 0;
  if (!readReg(RESET, &reg00)) return false;
  reg00 &= (uint8_t)~0x40;
  if (!writeReg(RESET, reg00)) return false;

  // Resolucao de 16 bits nas duas portas seriais (3 << 2).
  constexpr uint8_t kRes16 = (3 << 2);
  if (!writeReg(SDPIN, kRes16)) return false;
  if (!writeReg(SDPOUT, kRes16)) return false;

  return true;
}

bool Es8311::setVolume(uint8_t percent) {
  using namespace es8311_reg;

  if (percent > 100) percent = 100;

  // Mesma escala do driver oficial: 0 = mudo, 100 = 0xFF.
  uint8_t reg32 = 0;
  if (percent > 0) {
    reg32 = (uint8_t)(((uint16_t)percent * 256 / 100) - 1);
  }
  return writeReg(DAC32, reg32);
}

bool Es8311::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t err = Wire.endTransmission();
  _lastError = err;
  return err == 0;
}

bool Es8311::readReg(uint8_t reg, uint8_t *value) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  uint8_t err = Wire.endTransmission(false);  // repeated start
  if (err != 0) {
    _lastError = err;
    return false;
  }
  if (Wire.requestFrom(_addr, (uint8_t)1) != 1) {
    _lastError = 0xFD;  // sem resposta na leitura
    return false;
  }
  *value = (uint8_t)Wire.read();
  return true;
}
