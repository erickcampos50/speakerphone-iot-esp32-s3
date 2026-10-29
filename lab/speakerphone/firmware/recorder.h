#pragma once

#include <Arduino.h>
#include <FS.h>

#include "ESP_I2S.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

struct AudioBlock {
  size_t bytes;
  uint8_t data[1024];
};

// Gravador de WAV 16 kHz / 16 bits / estereo para o cartao SD.
class Recorder {
 public:
  // Callback opcional para nomear o arquivo (ex.: timestamp do RTC).
  // Deve preencher "out" sem pasta e sem extensao.
  typedef bool (*NameProvider)(char *out, size_t len);

  void setNameProvider(NameProvider provider) { _provider = provider; }

  bool beginCaptureQueue();
  void pushAudio(const int16_t *frames, size_t nframes);

  bool beginSd();
  bool sdReady() const { return _sdReady; }

  bool start(I2SClass *i2s);
  void stop();

  bool isRecording() const { return _recording; }
  bool busy() const { return _taskRunning; }
  uint32_t dataBytes() const { return _dataBytes; }
  uint32_t seconds() const;
  uint8_t level() const { return _level; }
  const char *fileName() const { return _path; }
  uint32_t shortReads() const { return _shortReads; }

  void listFiles();
  void printStats(const char *path);
  void printSdInfo();

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  bool writeHeader(uint32_t data_bytes);
  bool patchHeader();
  bool openNewFile();
  bool buildNextPath(char *out, size_t len);

  NameProvider _provider = nullptr;
  QueueHandle_t _queue = nullptr;
  SemaphoreHandle_t _queueLock = nullptr;
  File _file;
  char _path[80] = {0};
  bool _sdReady = false;
  volatile bool _recording = false;
  volatile bool _stopReq = false;
  volatile bool _taskRunning = false;
  volatile uint32_t _dataBytes = 0;
  volatile uint8_t _level = 0;
  volatile uint32_t _shortReads = 0;
  TaskHandle_t _task = nullptr;
  uint32_t _fileStartMs = 0;
  uint32_t _startMs = 0;
};
