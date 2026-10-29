#pragma once

#include <Arduino.h>

#include "ESP_I2S.h"
#include "es8311.h"

// Eventos sonoros da interface do gravador.
enum class BeepEvent {
  Boot,         // 3 notas subindo (523, 659, 784 Hz)
  RecordStart,  // 2 bips agudos (1046 Hz)
  RecordStop,   // 1 bip grave (392 Hz)
  LowBattery,   // 3 bips graves (330 Hz)
  Error,        // varredura descendente 880 -> 330 Hz
  WifiReady,    // 2 bips agudos (1318 Hz)
  AmpOn,        // bip curto 880 Hz
  AmpOff        // bip curto 440 Hz
};

// Um trecho da sequencia: frequencia inicial e final em Hz (0 = silencio)
// e duracao em milissegundos.
struct ToneSegment {
  uint16_t f0_hz;
  uint16_t f1_hz;
  uint16_t ms;
};

// Gerador de bips NAO BLOQUEANTE sobre o I2S compartilhado (full-duplex).
// O canal RX (microfone) ja roda; aqui so se configura o TX, sem chamar
// begin() de novo e sem mexer nos pinos.
class Beeper {
 public:
  // Configura o TX (16 kHz, 16 bits, estereo) e inicializa o ES8311.
  // Retorna false se o canal TX nao existir (DOUT/DIN precisam estar
  // definidos no setPins) ou se o codec nao responder.
  bool begin(I2SClass *i2s);

  // Callback que liga/desliga o amplificador (EXIO8 do TCA9555).
  // Chamado com true no inicio e false no fim de cada sequencia.
  void setAmpControl(void (*fn)(bool on));

  // Enfileira uma sequencia e retorna imediatamente.
  void play(BeepEvent ev);

  // Deve ser chamada do loop(): gera no maximo ~10 ms de audio por chamada.
  void update();

  bool busy() const;
  void setVolume(uint8_t percent);

 private:
  enum class State : uint8_t { Idle, LeadIn, Tone, Tail };

  void startEvent(BeepEvent ev);
  void startSegment(uint8_t index);
  void beginSilence(uint32_t ms, State state);
  void advanceState();
  void finishSequence();
  void abort();
  int16_t nextSample();

  I2SClass *_i2s = nullptr;
  Es8311 _codec;
  void (*_amp)(bool on) = nullptr;
  bool _amp_active = false;  // ja pedimos o amplificador ligado

  uint8_t _volume = 60;

  State _state = State::Idle;
  const ToneSegment *_segments = nullptr;
  uint8_t _seg_count = 0;
  uint8_t _seg_index = 0;

  // Gerador do trecho atual (nota ou silencio).
  uint32_t _pos = 0;    // amostras ja geradas
  uint32_t _total = 0;  // amostras do trecho
  float _freq = 0.0f;   // frequencia instantanea (0 = silencio)
  float _freq_step = 0.0f;
  float _phase = 0.0f;  // fase em radianos

  // Fila de eventos pendentes (ring buffer simples, sem STL).
  BeepEvent _queue[4] = {};
  uint8_t _q_head = 0;
  uint8_t _q_count = 0;

  int16_t _buf[160 * 2] = {};  // 10 ms estereo 16 bits
};
