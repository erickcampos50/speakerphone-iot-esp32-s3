#pragma once

#include <Arduino.h>

#include "ESP_I2S.h"

// Microfone USB (UAC1) composto com o CDC do console.
//
// A placa aparece no PC como "microfone" a 16 kHz / 16 bits / mono. O audio vem
// do mesmo I2S do ES7210 que a gravacao usa, mas por um caminho independente:
//
// A task interna e a unica leitora do I2S e distribui os blocos aos dois
// consumidores. O callback de gravacao deve retornar rapidamente.
class UsbMic {
 public:
  // Registra os eventos do UAC e cria as tasks. Nao chama USB.begin(): com
  // CDCOnBoot=cdc o core ja inicia o TinyUSB em app_main().
  bool begin(I2SClass *i2s);

  bool mounted() const { return _mounted; }
  bool streaming() const { return _streaming; }
  uint32_t underruns() const { return _underruns; }
  uint32_t dropped() const { return _dropped; }

  typedef void (*CaptureTap)(const int16_t *, size_t);
  void setCaptureTap(CaptureTap tap) { _captureTap = tap; }

  // Copia o canal 0 de um bloco estereo intercalado para o ring do USB.
  void pushStereo(const int16_t *frames, size_t nframes);

  // Usados pelos callbacks de evento do TinyUSB (nao chamar do firmware).
  void setMounted(bool on) { _mounted = on; }
  void setStreaming(bool on) { _streaming = on; }

  // Diagnostico para o console.
  void printStatus();
  void setLocalVolume(uint8_t value);
  uint8_t localVolume() const { return _localVolume; }
  uint8_t micLevel() const { return _micLevel; }
  uint8_t speakerLevel() const { return _speakerLevel; }
  void signal(uint8_t event);

 private:
  static void readerTaskEntry(void *arg);
  static void feederTaskEntry(void *arg);
  static void speakerTaskEntry(void *arg);
  void readerLoop();
  void feederLoop();
  void speakerLoop();

  I2SClass *_i2s = nullptr;

  volatile bool _mounted = false;
  volatile bool _streaming = false;
  CaptureTap _captureTap = nullptr;

  volatile uint32_t _underruns = 0;
  volatile uint32_t _dropped = 0;
  volatile uint8_t _localVolume = 100;
  volatile uint8_t _micLevel = 0;
  volatile uint8_t _speakerLevel = 0;
};

extern UsbMic usbmic;
