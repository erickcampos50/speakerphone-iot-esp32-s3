#include "es7210.h"

#include <Wire.h>

// Registradores usados (ES7210)
namespace reg {
constexpr uint8_t RESET = 0x00;
constexpr uint8_t MAINCLK = 0x02;
constexpr uint8_t LRCK_DIVH = 0x04;
constexpr uint8_t LRCK_DIVL = 0x05;
constexpr uint8_t POWER_DOWN = 0x06;
constexpr uint8_t OSR = 0x07;
constexpr uint8_t TIME_CONTROL0 = 0x09;
constexpr uint8_t TIME_CONTROL1 = 0x0A;
constexpr uint8_t SDP_INTERFACE1 = 0x11;
constexpr uint8_t SDP_INTERFACE2 = 0x12;
constexpr uint8_t ADC1_DB = 0x1B;
constexpr uint8_t ADC2_DB = 0x1C;
constexpr uint8_t ADC3_DB = 0x1D;
constexpr uint8_t ADC4_DB = 0x1E;
constexpr uint8_t ADC34_HPF2 = 0x20;
constexpr uint8_t ADC34_HPF1 = 0x21;
constexpr uint8_t ADC12_HPF2 = 0x22;
constexpr uint8_t ADC12_HPF1 = 0x23;
constexpr uint8_t ANALOG_POWER = 0x40;
constexpr uint8_t MIC12_BIAS = 0x41;
constexpr uint8_t MIC34_BIAS = 0x42;
constexpr uint8_t MIC1_GAIN = 0x43;
constexpr uint8_t MIC2_GAIN = 0x44;
constexpr uint8_t MIC3_GAIN = 0x45;
constexpr uint8_t MIC4_GAIN = 0x46;
constexpr uint8_t MIC1_POWER = 0x47;
constexpr uint8_t MIC2_POWER = 0x48;
constexpr uint8_t MIC3_POWER = 0x49;
constexpr uint8_t MIC4_POWER = 0x4A;
constexpr uint8_t MIC12_POWER = 0x4B;
constexpr uint8_t MIC34_POWER = 0x4C;
}  // namespace reg

namespace {
struct ClockCoeff {
  uint32_t mclk;
  uint32_t lrck;
  uint8_t adc_div;
  uint8_t dll;
  uint8_t doubler;
  uint8_t osr;
  uint8_t lrck_h;
  uint8_t lrck_l;
};

// Apenas as taxas relevantes para esta placa (MCLK = 256 * fs).
constexpr ClockCoeff kCoeffs[] = {
    {4096000, 16000, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
    {12288000, 48000, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
    {12288000, 24000, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
    {12288000, 12000, 0x02, 0x01, 0x00, 0x20, 0x04, 0x00},
    {12288000, 8000, 0x03, 0x01, 0x00, 0x20, 0x06, 0x00},
};

const ClockCoeff *findCoeff(uint32_t mclk, uint32_t lrck) {
  for (const auto &c : kCoeffs) {
    if (c.mclk == mclk && c.lrck == lrck) return &c;
  }
  return nullptr;
}
}  // namespace

bool Es7210::begin(uint8_t addr) {
  _addr = addr;
  Wire.beginTransmission(_addr);
  _present = (Wire.endTransmission() == 0);
  return _present;
}

bool Es7210::writeReg(uint8_t r, uint8_t value) {
  Wire.beginTransmission(_addr);
  Wire.write(r);
  Wire.write(value);
  uint8_t err = Wire.endTransmission();
  _lastError = err;
  if (err != 0) {
    _errors++;
    return false;
  }
  return true;
}

bool Es7210::configure(uint32_t sample_rate, uint32_t mclk_ratio,
                       uint8_t mic_gain_enum, bool tdm) {
  const ClockCoeff *coeff = findCoeff(sample_rate * mclk_ratio, sample_rate);
  if (coeff == nullptr) {
    _lastError = 0xFE;  // combinacao nao suportada
    return false;
  }

  const uint8_t errors_before = _errors;

  // Reset + tempo de estabilizacao
  writeReg(reg::RESET, 0xFF);
  delay(2);
  writeReg(reg::RESET, 0x32);
  writeReg(reg::TIME_CONTROL0, 0x30);
  writeReg(reg::TIME_CONTROL1, 0x30);

  // High-pass filter dos ADCs
  writeReg(reg::ADC12_HPF1, 0x2A);
  writeReg(reg::ADC12_HPF2, 0x0A);
  writeReg(reg::ADC34_HPF1, 0x2A);
  writeReg(reg::ADC34_HPF2, 0x0A);

  // 16 bits, formato I2S, TDM 1xFS
  writeReg(reg::SDP_INTERFACE1, 0x00 | 0x60);
  writeReg(reg::SDP_INTERFACE2, tdm ? 0x02 : 0x00);

  // Alimentacao analogica / VMID
  writeReg(reg::ANALOG_POWER, 0xC3);

  // Bias dos microfones: 2.87 V
  writeReg(reg::MIC12_BIAS, 0x70);
  writeReg(reg::MIC34_BIAS, 0x70);

  // Ganho dos 4 canais
  writeReg(reg::MIC1_GAIN, mic_gain_enum | 0x10);
  writeReg(reg::MIC2_GAIN, mic_gain_enum | 0x10);
  writeReg(reg::MIC3_GAIN, mic_gain_enum | 0x10);
  writeReg(reg::MIC4_GAIN, mic_gain_enum | 0x10);

  // Liga os microfones
  writeReg(reg::MIC1_POWER, 0x08);
  writeReg(reg::MIC2_POWER, 0x08);
  writeReg(reg::MIC3_POWER, 0x08);
  writeReg(reg::MIC4_POWER, 0x08);

  // Taxa de amostragem / divisores de clock
  writeReg(reg::OSR, coeff->osr);
  writeReg(reg::MAINCLK,
           coeff->adc_div | (coeff->doubler << 6) | (coeff->dll << 7));
  writeReg(reg::LRCK_DIVH, coeff->lrck_h);
  writeReg(reg::LRCK_DIVL, coeff->lrck_l);

  writeReg(reg::POWER_DOWN, 0x04);
  writeReg(reg::MIC12_POWER, 0x0F);
  writeReg(reg::MIC34_POWER, 0x0F);

  // Habilita o dispositivo
  writeReg(reg::RESET, 0x71);
  writeReg(reg::RESET, 0x41);

  setVolumeDb(0);

  return _errors == errors_before;
}

bool Es7210::setVolumeDb(int8_t db) {
  if (db < -95 || db > 32) return false;
  // 0x00 = -95.5 dB, 0xBF = 0 dB, 0xFF = +32 dB (passo de 0.5 dB)
  uint8_t value = 191 + db * 2;
  return writeReg(reg::ADC1_DB, value) && writeReg(reg::ADC2_DB, value) &&
         writeReg(reg::ADC3_DB, value) && writeReg(reg::ADC4_DB, value);
}
