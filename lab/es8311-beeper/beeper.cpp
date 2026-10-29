#include "beeper.h"

#include <math.h>

namespace {

// Parametros de audio do modulo (iguais aos do firmware: 16 kHz, 16 bits).
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kBlockFrames = 160;  // 10 ms por chamada de update()
constexpr uint32_t kFadeSamples = 80;   // 5 ms de fade-in/fade-out
constexpr int16_t kAmplitude = 12000;   // ~-8,7 dBFS (evita clipar no codec)

// O TX do I2S tem ~90 ms de buffer DMA; o silencio de entrada cobre o tempo
// de acomodacao do amplificador e o de saida garante que a cauda nao seja
// cortada quando o amplificador e desligado.
constexpr uint32_t kLeadInMs = 60;
constexpr uint32_t kTailMs = 120;

// Timeout curto no write() para nao travar a gravacao; restaurado depois.
constexpr unsigned long kWriteTimeoutMs = 25;

constexpr uint8_t kQueueSize = 4;
constexpr float kTwoPi = 6.28318530718f;

// ---------------------------------------------------------------------------
// Sequencias (seno, 16 kHz, estereo)
// ---------------------------------------------------------------------------
const ToneSegment kBoot[] = {
    {523, 523, 90},
    {659, 659, 90},
    {784, 784, 90},
};

const ToneSegment kRecordStart[] = {
    {1046, 1046, 70},
    {0, 0, 70},
    {1046, 1046, 70},
};

const ToneSegment kRecordStop[] = {
    {392, 392, 160},
};

const ToneSegment kLowBattery[] = {
    {330, 330, 90},
    {0, 0, 120},
    {330, 330, 90},
    {0, 0, 120},
    {330, 330, 90},
};

const ToneSegment kError[] = {
    {880, 330, 400},  // varredura descendente
};

const ToneSegment kWifiReady[] = {
    {1318, 1318, 60},
    {0, 0, 60},
    {1318, 1318, 60},
};

const ToneSegment kAmpOn[] = {
    {880, 880, 40},
};

const ToneSegment kAmpOff[] = {
    {440, 440, 40},
};

struct Sequence {
  const ToneSegment *segments;
  uint8_t count;
};

// Indexado pelo valor de BeepEvent.
const Sequence kSequences[] = {
    {kBoot, (uint8_t)(sizeof(kBoot) / sizeof(kBoot[0]))},
    {kRecordStart, (uint8_t)(sizeof(kRecordStart) / sizeof(kRecordStart[0]))},
    {kRecordStop, (uint8_t)(sizeof(kRecordStop) / sizeof(kRecordStop[0]))},
    {kLowBattery, (uint8_t)(sizeof(kLowBattery) / sizeof(kLowBattery[0]))},
    {kError, (uint8_t)(sizeof(kError) / sizeof(kError[0]))},
    {kWifiReady, (uint8_t)(sizeof(kWifiReady) / sizeof(kWifiReady[0]))},
    {kAmpOn, (uint8_t)(sizeof(kAmpOn) / sizeof(kAmpOn[0]))},
    {kAmpOff, (uint8_t)(sizeof(kAmpOff) / sizeof(kAmpOff[0]))},
};

constexpr uint8_t kSequenceCount =
    (uint8_t)(sizeof(kSequences) / sizeof(kSequences[0]));

}  // namespace

bool Beeper::begin(I2SClass *i2s) {
  _i2s = i2s;
  if (_i2s == nullptr) return false;

  // O canal TX ja foi criado pelo begin() do firmware (DOUT e DIN definidos).
  // configureTX so reconfigura taxa/formato; nao mexe nos pinos nem no RX.
  if (!_i2s->configureTX(kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                         I2S_SLOT_MODE_STEREO)) {
    _i2s = nullptr;
    return false;
  }

  const bool ok = _codec.begin();
  if (ok) _codec.setVolume(_volume);
  return ok;
}

void Beeper::setAmpControl(void (*fn)(bool on)) { _amp = fn; }

void Beeper::setVolume(uint8_t percent) {
  if (percent > 100) percent = 100;
  _volume = percent;
  _codec.setVolume(_volume);
}

bool Beeper::busy() const { return _state != State::Idle || _q_count > 0; }

void Beeper::play(BeepEvent ev) {
  if (_i2s == nullptr) return;
  if ((uint8_t)ev >= kSequenceCount) return;

  // Nada tocando: comeca na hora (retorno imediato).
  if (_state == State::Idle && _q_count == 0) {
    startEvent(ev);
    return;
  }

  // Ja tocando: enfileira para nao cortar o som atual.
  if (_q_count >= kQueueSize) return;  // fila cheia: descarta o novo evento
  _queue[(uint8_t)((_q_head + _q_count) % kQueueSize)] = ev;
  _q_count++;
}

void Beeper::update() {
  if (_i2s == nullptr || _state == State::Idle) return;

  uint32_t frames = 0;
  while (frames < kBlockFrames) {
    if (_pos >= _total) {
      advanceState();
      if (_state == State::Idle) break;
      continue;
    }
    const int16_t sample = nextSample();
    _buf[frames * 2] = sample;      // canal esquerdo
    _buf[frames * 2 + 1] = sample;  // canal direito
    _pos++;
    frames++;
  }

  if (frames == 0) return;

  // O Stream tem um unico timeout, usado tambem pelas leituras do RX:
  // troca por um valor curto e restaura em seguida.
  const unsigned long saved = _i2s->getTimeout();
  _i2s->setTimeout(kWriteTimeoutMs);
  const size_t bytes = (size_t)frames * 2 * sizeof(int16_t);
  const size_t written = _i2s->write(_buf, bytes);
  _i2s->setTimeout(saved);

  // Erro de escrita: encerra a sequencia para nao travar o loop principal.
  if (written < bytes) abort();
}

int16_t Beeper::nextSample() {
  if (_freq < 0.5f) return 0;  // silencio

  // Envelope trapezoidal: fade-in e fade-out de 5 ms contra clique.
  float env = 1.0f;
  if (_pos < kFadeSamples) {
    env = (float)_pos / (float)kFadeSamples;
  } else if (_total - _pos <= kFadeSamples) {
    env = (float)(_total - 1 - _pos) / (float)kFadeSamples;
  }

  const int16_t sample = (int16_t)(sinf(_phase) * (float)kAmplitude * env);

  _phase += kTwoPi * _freq / (float)kSampleRate;
  if (_phase >= kTwoPi) _phase -= kTwoPi;
  _freq += _freq_step;

  return sample;
}

void Beeper::startEvent(BeepEvent ev) {
  const uint8_t idx = (uint8_t)ev;
  if (idx >= kSequenceCount) {
    _state = State::Idle;
    return;
  }

  _segments = kSequences[idx].segments;
  _seg_count = kSequences[idx].count;
  _seg_index = 0;

  // Liga o amplificador antes de mandar audio e espera o lead-in.
  // Se uma sequencia ja esta tocando, nao chama o callback de novo: isso
  // permite ao firmware salvar/restaurar o estado do amplificador.
  if (_amp != nullptr && !_amp_active) {
    _amp(true);
    _amp_active = true;
  }
  beginSilence(kLeadInMs, State::LeadIn);
}

void Beeper::startSegment(uint8_t index) {
  const ToneSegment &seg = _segments[index];

  _seg_index = index;
  _pos = 0;
  _total = (uint32_t)seg.ms * kSampleRate / 1000;
  if (_total == 0) _total = 1;

  _freq = (float)seg.f0_hz;
  _freq_step = ((float)seg.f1_hz - (float)seg.f0_hz) / (float)_total;
  _phase = 0.0f;
  _state = State::Tone;
}

void Beeper::beginSilence(uint32_t ms, State state) {
  _pos = 0;
  _total = ms * kSampleRate / 1000;
  if (_total == 0) _total = 1;
  _freq = 0.0f;
  _freq_step = 0.0f;
  _phase = 0.0f;
  _state = state;
}

void Beeper::advanceState() {
  switch (_state) {
    case State::LeadIn:
      startSegment(0);
      break;

    case State::Tone:
      if ((uint8_t)(_seg_index + 1) < _seg_count) {
        startSegment((uint8_t)(_seg_index + 1));
      } else {
        beginSilence(kTailMs, State::Tail);
      }
      break;

    case State::Tail:
      finishSequence();
      break;

    default:
      _state = State::Idle;
      break;
  }
}

void Beeper::finishSequence() {
  // Proximo evento da fila: emenda sem desligar o amplificador.
  if (_q_count > 0) {
    const BeepEvent ev = _queue[_q_head];
    _q_head = (uint8_t)((_q_head + 1) % kQueueSize);
    _q_count--;
    startEvent(ev);
    return;
  }

  if (_amp != nullptr && _amp_active) {
    _amp(false);
    _amp_active = false;
  }
  _state = State::Idle;
}

void Beeper::abort() {
  _q_head = 0;
  _q_count = 0;
  if (_amp != nullptr && _amp_active) {
    _amp(false);
    _amp_active = false;
  }
  _state = State::Idle;
}
