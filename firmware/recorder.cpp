#include "recorder.h"

#include <SD_MMC.h>
#include <math.h>

#include "board.h"
namespace {
constexpr size_t kQueueBlocks = 64;  // 64 KiB, cerca de 1 segundo
constexpr uint32_t kMinFreeBytes = 2 * 1024 * 1024;
constexpr uint32_t kBytesPerSecond =
    AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS / 8);
constexpr uint32_t kHeaderIntervalMs = 5000;   // seguranca contra queda de energia
constexpr uint32_t kRotateMs = 60UL * 60UL * 1000UL;  // 60 min por arquivo
constexpr uint32_t kSpaceCheckMs = 10000;

void put16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}
void put32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}
}  // namespace

bool Recorder::beginCaptureQueue() {
  _queue = xQueueCreate(kQueueBlocks, sizeof(AudioBlock));
  _queueLock = xSemaphoreCreateMutex();
  return _queue && _queueLock;
}

void Recorder::pushAudio(const int16_t *frames, size_t nframes) {
  if (!_queue || !_queueLock || !frames || nframes == 0 || nframes > 256) return;
  AudioBlock block;
  block.bytes = nframes * 4;
  memcpy(block.data, frames, block.bytes);
  if (xSemaphoreTake(_queueLock, pdMS_TO_TICKS(5)) != pdTRUE) {
    _shortReads = _shortReads + 1;
    return;
  }
  if (_recording && !_stopReq && xQueueSend(_queue, &block, 0) != pdTRUE) {
    _shortReads = _shortReads + 1;  // fila cheia: encerra para nao ocultar perda
    _recording = false;
    _stopReq = true;
  }
  xSemaphoreGive(_queueLock);
}

bool Recorder::beginSd() {
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, -1, -1, -1)) {
    Serial.println("[SD] setPins falhou");
    _sdReady = false;
    return false;
  }

  // 1-bit, sem formatar automaticamente, 20 MHz
  _sdReady = SD_MMC.begin("/sdcard", true, false, 20000);
  if (!_sdReady) {
    Serial.println("[SD] montagem falhou (cartao ausente ou nao FAT32?)");
    return false;
  }

  Serial.printf("[SD] ok: %.1f GB total, %.1f GB livres\n",
                SD_MMC.totalBytes() / 1073741824.0,
                (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1073741824.0);

  if (!SD_MMC.exists("/AUDIO")) {
    if (!SD_MMC.mkdir("/AUDIO")) {
      Serial.println("[SD] nao foi possivel criar /AUDIO");
    }
  }
  return true;
}

bool Recorder::buildNextPath(char *out, size_t len) {
  if (_provider != nullptr) {
    char base[64] = {0};
    if (_provider(base, sizeof(base)) && base[0] != 0) {
      snprintf(out, len, "/AUDIO/%s.wav", base);
      int n = 1;
      while (SD_MMC.exists(out) && n < 100) {
        snprintf(out, len, "/AUDIO/%s_%d.wav", base, n);
        n++;
      }
      return !SD_MMC.exists(out);
    }
  }

  for (int i = 1; i <= 9999; i++) {
    snprintf(out, len, "/AUDIO/REC_%04d.WAV", i);
    if (!SD_MMC.exists(out)) return true;
  }
  return false;
}

bool Recorder::writeHeader(uint32_t data_bytes) {
  uint8_t h[44] = {0};
  const uint32_t byte_rate = AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * 2;
  const uint16_t block_align = AUDIO_CHANNELS * 2;

  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + data_bytes);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  put32(h + 16, 16);
  put16(h + 20, 1);  // PCM
  put16(h + 22, AUDIO_CHANNELS);
  put32(h + 24, AUDIO_SAMPLE_RATE);
  put32(h + 28, byte_rate);
  put16(h + 32, block_align);
  put16(h + 34, AUDIO_BITS);
  memcpy(h + 36, "data", 4);
  put32(h + 40, data_bytes);

  return _file.write(h, sizeof(h)) == sizeof(h);
}

bool Recorder::patchHeader() {
  uint32_t pos = _file.position();
  if (!_file.seek(0)) return false;
  bool ok = writeHeader(_dataBytes);
  _file.seek(pos);
  return ok;
}

bool Recorder::openNewFile() {
  if (!buildNextPath(_path, sizeof(_path))) {
    Serial.println("[REC] nao foi possivel gerar nome de arquivo");
    return false;
  }

  _file = SD_MMC.open(_path, FILE_WRITE);
  if (!_file) {
    Serial.printf("[REC] falha ao criar %s\n", _path);
    return false;
  }

  _dataBytes = 0;
  if (!writeHeader(0)) {
    Serial.println("[REC] falha ao escrever cabecalho");
    _file.close();
    return false;
  }
  _fileStartMs = millis();
  Serial.printf("[REC] gravando em %s\n", _path);
  return true;
}

bool Recorder::start(I2SClass *i2s) {
  (void)i2s;
  if (_recording || _taskRunning) return false;
  if (!_queue || !_queueLock) return false;
  if (!_sdReady) {
    Serial.println("[REC] SD indisponivel");
    return false;
  }
  if (SD_MMC.totalBytes() - SD_MMC.usedBytes() < kMinFreeBytes) {
    Serial.println("[REC] espaco livre insuficiente no cartao");
    return false;
  }

  if (!openNewFile()) return false;

  _shortReads = 0;
  _level = 0;
  _stopReq = false;
  xQueueReset(_queue);
  _recording = true;
  _taskRunning = true;
  _startMs = millis();

  BaseType_t ok =
      xTaskCreatePinnedToCore(taskEntry, "rec", 8192, this, 6, &_task, 1);
  if (ok != pdPASS) {
    _recording = false;
    _taskRunning = false;
    _file.close();
    Serial.println("[REC] falha ao criar task");
    return false;
  }
  return true;
}

void Recorder::stop() {
  if (!_recording && !_taskRunning) return;
  xSemaphoreTake(_queueLock, portMAX_DELAY);
  _recording = false;
  _stopReq = true;
  xSemaphoreGive(_queueLock);

  uint32_t t0 = millis();
  while (_taskRunning && millis() - t0 < 5000) {
    delay(10);
  }
  if (_taskRunning) {
    Serial.println("[REC] encerramento ainda em andamento");
    return;
  }

  uint32_t elapsed = millis() - _startMs;
  float bps = elapsed ? (_dataBytes * 1000.0f / elapsed) : 0.0f;
  Serial.printf("[REC] parado: %s (%lu bytes, %lu s)\n", _path,
                (unsigned long)_dataBytes, (unsigned long)seconds());
  Serial.printf("[REC] taxa media %.0f B/s (esperado %lu)  leituras curtas: %lu\n",
                bps, (unsigned long)kBytesPerSecond,
                (unsigned long)_shortReads);
  if (bps < kBytesPerSecond * 0.9f) {
    Serial.println("[REC] AVISO: taxa abaixo do esperado (perda de amostras)");
  }
}

void Recorder::taskEntry(void *arg) {
  static_cast<Recorder *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void Recorder::taskLoop() {
  AudioBlock *block = (AudioBlock *)malloc(sizeof(AudioBlock));
  if (block == nullptr) {
    Serial.println("[REC] sem memoria para buffer");
    _file.close();
    _recording = false;
    _taskRunning = false;
    return;
  }

  uint32_t last_header = millis();
  uint32_t last_space = millis();

  while (!_stopReq || uxQueueMessagesWaiting(_queue) > 0) {
    if (xQueueReceive(_queue, block, pdMS_TO_TICKS(50)) != pdTRUE) continue;
    size_t n = block->bytes;
    uint8_t *buf = block->data;

    size_t written = _file.write(buf, n);
    _dataBytes = _dataBytes + written;

    // Nivel (pico do canal esquerdo), amostrado a cada 8 frames.
    const int16_t *samples = (const int16_t *)buf;
    int peak = 0;
    for (size_t i = 0; i + 1 < n / 2; i += 16) {
      int v = samples[i];
      if (v < 0) v = -v;
      if (v > peak) peak = v;
    }
    float db = 20.0f * log10f((peak + 1) / 32768.0f);
    int lvl = (int)((db + 60.0f) * 100.0f / 60.0f);
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    _level = (uint8_t)lvl;

    if (written != n) {
      Serial.println("[REC] erro de escrita no SD, encerrando");
      xSemaphoreTake(_queueLock, portMAX_DELAY);
      _recording = false;
      _stopReq = true;
      xSemaphoreGive(_queueLock);
      break;
    }

    uint32_t now = millis();

    // Cabecalho atualizado periodicamente (arquivo consistente apos queda).
    if (now - last_header >= kHeaderIntervalMs) {
      last_header = now;
      patchHeader();
    }

    // Espaco livre
    if (now - last_space >= kSpaceCheckMs) {
      last_space = now;
      if (SD_MMC.totalBytes() - SD_MMC.usedBytes() < kMinFreeBytes) {
        Serial.println("[REC] cartao cheio, encerrando");
        xSemaphoreTake(_queueLock, portMAX_DELAY);
        _recording = false;
        _stopReq = true;
        xSemaphoreGive(_queueLock);
        break;
      }
    }

    // Rotacao por tempo
    if (now - _fileStartMs >= kRotateMs) {
      patchHeader();
      _file.flush();
      _file.close();
      if (!openNewFile()) {
        xSemaphoreTake(_queueLock, portMAX_DELAY);
        _recording = false;
        _stopReq = true;
        xSemaphoreGive(_queueLock);
        break;
      }
      last_header = millis();
    }
  }

  if (_shortReads) Serial.printf("[REC] blocos perdidos/fila cheia: %lu\n", (unsigned long)_shortReads);

  patchHeader();
  _file.flush();
  _file.close();
  _level = 0;
  free(block);
  _recording = false;
  _taskRunning = false;
}

uint32_t Recorder::seconds() const { return _dataBytes / kBytesPerSecond; }

void Recorder::listFiles() {
  File dir = SD_MMC.open("/AUDIO");
  if (!dir) {
    Serial.println("[SD] pasta /AUDIO nao existe");
    return;
  }
  Serial.println("[SD] arquivos em /AUDIO:");
  File f = dir.openNextFile();
  int count = 0;
  while (f) {
    if (!f.isDirectory()) {
      Serial.printf("  %-28s %9lu bytes\n", f.name(), (unsigned long)f.size());
      count++;
    }
    f = dir.openNextFile();
  }
  if (count == 0) Serial.println("  (vazio)");
  dir.close();
}

void Recorder::printSdInfo() {
  if (!_sdReady) {
    Serial.println("[SD] nao montado");
    return;
  }
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  Serial.printf("[SD] total=%.2f GB usado=%.2f GB livre=%.2f GB\n",
                total / 1073741824.0, used / 1073741824.0,
                (total - used) / 1073741824.0);
  Serial.printf("[SD] tipo=%d  gravacao livre ~%lu min\n", SD_MMC.cardType(),
                (unsigned long)((total - used) / kBytesPerSecond / 60));
}

void Recorder::printStats(const char *path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[WAV] nao foi possivel abrir %s\n", path);
    return;
  }

  uint8_t h[44];
  if (f.read(h, sizeof(h)) != sizeof(h) || memcmp(h, "RIFF", 4) != 0) {
    Serial.println("[WAV] cabecalho invalido");
    f.close();
    return;
  }

  uint16_t channels = h[22] | (h[23] << 8);
  uint32_t rate = h[24] | (h[25] << 8) | ((uint32_t)h[26] << 16) |
                  ((uint32_t)h[27] << 24);
  uint16_t bits = h[34] | (h[35] << 8);
  uint32_t data_bytes = h[40] | (h[41] << 8) | ((uint32_t)h[42] << 16) |
                        ((uint32_t)h[43] << 24);

  Serial.printf("[WAV] %s\n", path);
  Serial.printf("      %lu Hz, %u ch, %u bits, %lu bytes (%.1f s)\n",
                (unsigned long)rate, channels, bits, (unsigned long)data_bytes,
                data_bytes / (float)(rate * channels * (bits / 8)));

  // Analise: no maximo os primeiros 60 s (arquivos longos levariam minutos).
  const uint32_t kMaxFrames = rate * 60;
  const uint32_t kFramesPerLine = rate;  // 1 linha por segundo

  double sum[2] = {0, 0};
  int peak[2] = {0, 0};
  double dc[2] = {0, 0};
  uint32_t frames = 0;

  double sec_sum[2] = {0, 0};
  int sec_peak[2] = {0, 0};
  uint32_t sec_frames = 0;
  int sec_index = 1;

  int16_t frame[2];
  while (frames < kMaxFrames &&
         f.read((uint8_t *)frame, sizeof(frame)) == sizeof(frame)) {
    for (int c = 0; c < 2; c++) {
      int v = frame[c];
      sum[c] += (double)v * v;
      dc[c] += v;
      sec_sum[c] += (double)v * v;
      int a = v < 0 ? -v : v;
      if (a > peak[c]) peak[c] = a;
      if (a > sec_peak[c]) sec_peak[c] = a;
    }
    frames++;
    sec_frames++;

    if (sec_frames >= kFramesPerLine) {
      double r0 = sqrt(sec_sum[0] / sec_frames);
      double r1 = sqrt(sec_sum[1] / sec_frames);
      Serial.printf("      %3ds  L: RMS=%6.0f pico=%5d | R: RMS=%6.0f pico=%5d\n",
                    sec_index, r0, sec_peak[0], r1, sec_peak[1]);
      sec_sum[0] = sec_sum[1] = 0;
      sec_peak[0] = sec_peak[1] = 0;
      sec_frames = 0;
      sec_index++;
    }
  }
  f.close();

  if (frames == 0) {
    Serial.println("      (sem amostras)");
    return;
  }

  // Ultimo segundo parcial.
  if (sec_frames > 0) {
    double r0 = sqrt(sec_sum[0] / sec_frames);
    double r1 = sqrt(sec_sum[1] / sec_frames);
    Serial.printf("      %3ds  L: RMS=%6.0f pico=%5d | R: RMS=%6.0f pico=%5d (parcial)\n",
                  sec_index, r0, sec_peak[0], r1, sec_peak[1]);
  }

  Serial.printf("      analisados %lu frames (%.1f s)\n", (unsigned long)frames,
                frames / (float)rate);
  for (int c = 0; c < 2; c++) {
    double rms = sqrt(sum[c] / frames);
    Serial.printf("      canal %d: RMS=%.1f  pico=%d (%.1f dBFS)  DC=%.1f\n", c,
                  rms, peak[c], 20.0f * log10f((peak[c] + 1) / 32768.0f),
                  dc[c] / frames);
  }
}
