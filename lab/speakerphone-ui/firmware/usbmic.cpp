#include "usbmic.h"

#include "USB.h"
#include "USBAudioCard.h"
// USBAudioCard.h nao inclui os headers da classe de audio do TinyUSB.
// esp32-hal-tinyusb.h traz tusb_config.h (CFG_TUD_AUDIO=1); so depois disso
// audio_device.h declara tud_audio_n_available()/set_ep_in_fifo_threshold().
#include "esp32-hal-tinyusb.h"
#include "class/audio/audio_device.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Parametros
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kRingSamples = 8192;               // 512 ms @ 16 kHz
constexpr uint32_t kRingMask = kRingSamples - 1;      // potencia de 2
constexpr size_t kReadBytes = 1024;                   // 16 ms estereo
constexpr uint32_t kFeederChunk = 256;                // amostras por volta

// Instancia GLOBAL: o construtor roda no static-init, antes de app_main()
// chamar Serial.begin()/USB.begin(). Isso e obrigatorio porque
// tinyusb_enable_interface() falha depois que o TinyUSB iniciou.
USBAudioCard uac(kSampleRate, UAC_BPS_16, UAC_SPK_MONO, UAC_MIC_MONO);
StreamBufferHandle_t speakerBuffer = nullptr;
QueueHandle_t signalQueue = nullptr;
volatile uint32_t speakerDropped = 0;

int16_t g_ring[kRingSamples];
volatile uint32_t g_head = 0;  // so o produtor escreve
volatile uint32_t g_tail = 0;  // so o consumidor escreve
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

inline uint32_t ringCount() {
  portENTER_CRITICAL(&g_mux);
  uint32_t n = (g_head - g_tail) & kRingMask;
  portEXIT_CRITICAL(&g_mux);
  return n;
}

inline bool ringPush(int16_t s) {
  bool ok = false;
  portENTER_CRITICAL(&g_mux);
  uint32_t next = (g_head + 1) & kRingMask;
  if (next != g_tail) {
    g_ring[g_head] = s;
    g_head = next;
    ok = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return ok;
}

inline uint32_t ringPop(int16_t *dst, uint32_t maxSamples) {
  portENTER_CRITICAL(&g_mux);
  uint32_t n = (g_head - g_tail) & kRingMask;
  if (n > maxSamples) n = maxSamples;
  for (uint32_t i = 0; i < n; i++) {
    dst[i] = g_ring[(g_tail + i) & kRingMask];
  }
  g_tail = (g_tail + n) & kRingMask;
  portEXIT_CRITICAL(&g_mux);
  return n;
}

// Nao imprimir nada aqui: estes callbacks rodam na task do TinyUSB e o Serial
// tambem e CDC do TinyUSB (deadlock se o host nao estiver lendo).
void onAudioEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  if (id != ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT) return;

  arduino_usb_audio_card_event_data_t *p =
      (arduino_usb_audio_card_event_data_t *)data;
  if (p->interface_enable.interface != UAC_INTERFACE_MIC) return;
  usbmic.setStreaming(p->interface_enable.enable);
  if (p->interface_enable.enable) {
    // audiod_set_itf() reseta o threshold a cada SET_INTERFACE; reaplicamos
    // para ficar com ~8 ms de folga em vez dos ~48 ms padrao.
    tud_audio_n_set_ep_in_fifo_threshold(0, 256);
  }
}

void onSpeakerData(void *data, uint16_t len) {
  if (!speakerBuffer || !data) return;
  uac.applyVolume(data, len);
  size_t accepted = xStreamBufferSend(speakerBuffer, data, len & ~1u, 0);
  if (accepted < (len & ~1u)) speakerDropped += (len & ~1u) - accepted;
}

void onUsbEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  (void)data;
  if (id == ARDUINO_USB_STARTED_EVENT) {
    usbmic.setMounted(true);
  } else if (id == ARDUINO_USB_STOPPED_EVENT) {
    usbmic.setMounted(false);
    usbmic.setStreaming(false);
  }
}

}  // namespace

UsbMic usbmic;

// ---------------------------------------------------------------------------
// Leitura ociosa do I2S (quando a gravacao nao esta usando o barramento)
// ---------------------------------------------------------------------------
void UsbMic::readerLoop() {
  int16_t buf[kReadBytes / 2];

  for (;;) {
    if (_i2s == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    size_t n = _i2s->readBytes((char *)buf, sizeof(buf));
    if (n >= 4) {
      int peak = 0;
      for (size_t i = 0; i + 1 < n / 2; i += 2) {
        int v = buf[i]; if (v < 0) v = -v;
        if (v > peak) peak = v;
      }
      _micLevel = peak >= 8192 ? 100 : (uint8_t)(peak * 100 / 8192);
      pushStereo(buf, n / 4);
      if (_captureTap) _captureTap(buf, n / 4);
    }
  }
}

// ---------------------------------------------------------------------------
// Drenagem do ring para o FIFO do TinyUSB
// ---------------------------------------------------------------------------
void UsbMic::feederLoop() {
  int16_t staging[kFeederChunk];
  uint16_t total = 0;
  uint16_t off = 0;

  for (;;) {
    if (!_streaming) {
      total = off = 0;
      portENTER_CRITICAL(&g_mux);
      g_tail = g_head;
      portEXIT_CRITICAL(&g_mux);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    if (off == total) {
      uint32_t n = ringPop(staging, kFeederChunk);
      if (n == 0) {
        _underruns = _underruns + 1;
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      total = (uint16_t)(n * sizeof(int16_t));
      off = 0;
    }
    while (off < total) {
      uint16_t w =
          uac.write((const uint8_t *)staging + off, (uint16_t)(total - off));
      if (w == 0) break;  // FIFO cheio: o resto fica para a proxima volta
      off += w;
    }
    if (off < total) {
      // Mantem os dados pendentes no staging, sem recuar o ring.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void UsbMic::readerTaskEntry(void *arg) {
  static_cast<UsbMic *>(arg)->readerLoop();
  vTaskDelete(nullptr);
}

void UsbMic::feederTaskEntry(void *arg) {
  static_cast<UsbMic *>(arg)->feederLoop();
  vTaskDelete(nullptr);
}

void UsbMic::speakerTaskEntry(void *arg) {
  static_cast<UsbMic *>(arg)->speakerLoop();
  vTaskDelete(nullptr);
}

void UsbMic::speakerLoop() {
  int16_t mono[160];
  int16_t stereo[320];
  uint32_t remaining = 0, position = 0;
  float phase = 0;
  uint16_t frequency = 880;
  for (;;) {
    uint8_t event;
    if (!remaining && signalQueue && xQueueReceive(signalQueue, &event, 0) == pdTRUE) {
      frequency = event == 2 ? 440 : event == 3 ? 660 : 880;
      remaining = event == 3 ? 3200 : 960;
      position = 0;
      phase = 0;
    }
    size_t n = xStreamBufferReceive(speakerBuffer, mono, sizeof(mono), 0);
    n &= ~1u;
    int peak = 0;
    for (size_t i = 0; i < 160; ++i) {
      int32_t sample = i < n / 2 ? mono[i] : 0;
      int scaled = sample * _localVolume / 100;
      int a = scaled < 0 ? -scaled : scaled;
      if (a > peak) peak = a;
      if (remaining) {
        int envelope = position < 80 ? position : (remaining < 80 ? remaining : 80);
        sample += (int32_t)(1800 * sinf(phase) * envelope / 80.0f);
        phase += 2.0f * PI * frequency / 16000.0f;
        if (phase >= 2.0f * PI) phase -= 2.0f * PI;
        ++position;
        --remaining;
      }
      sample = sample * _localVolume / 100;
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      stereo[2 * i] = sample;
      stereo[2 * i + 1] = sample;
    }
    _speakerLevel = peak >= 8192 ? 100 : (uint8_t)(peak * 100 / 8192);
    if (_i2s->write(stereo, sizeof(stereo)) != sizeof(stereo)) vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------
bool UsbMic::begin(I2SClass *i2s) {
  _i2s = i2s;

  speakerBuffer = xStreamBufferCreate(16384, 1);
  signalQueue = xQueueCreate(4, sizeof(uint8_t));
  if (!speakerBuffer || !signalQueue) return false;
  uac.onData(onSpeakerData);
  if (!uac.begin()) return false;
  uac.onEvent(ARDUINO_USB_AUDIO_CARD_ANY_EVENT, onAudioEvent);
  USB.onEvent(onUsbEvent);

  BaseType_t a =
      xTaskCreatePinnedToCore(readerTaskEntry, "uac_rd", 3072, this, 4, nullptr, 1);
  BaseType_t b =
      xTaskCreatePinnedToCore(feederTaskEntry, "uac_fd", 3072, this, 5, nullptr, 0);
  BaseType_t c =
      xTaskCreatePinnedToCore(speakerTaskEntry, "uac_spk", 3072, this, 3, nullptr, 1);

  return a == pdPASS && b == pdPASS && c == pdPASS;
}

void UsbMic::setLocalVolume(uint8_t value) { _localVolume = value > 100 ? 100 : value; }
void UsbMic::signal(uint8_t event) { if (signalQueue) xQueueSend(signalQueue, &event, 0); }

void UsbMic::pushStereo(const int16_t *frames, size_t nframes) {
  if (!_streaming || nframes == 0) return;
  for (size_t i = 0; i < nframes; i++) {
    if (!ringPush(frames[i * 2])) {  // canal 0 (MIC1)
      _dropped = _dropped + 1;
    }
  }
}

void UsbMic::printStatus() {
  Serial.printf("[USB] %s  stream=%s  ring=%lu/%lu  underruns=%lu  dropped=%lu\n",
                tud_mounted() ? "montado" : "desconectado",
                _streaming ? "sim" : "nao", (unsigned long)ringCount(),
                (unsigned long)kRingSamples, (unsigned long)_underruns,
                (unsigned long)_dropped);
  Serial.printf("[USB OUT] descartados=%lu bytes\n", (unsigned long)speakerDropped);
  Serial.printf("[USB OUT] volume host: master=%d dB canal=%d dB; mute master=%d canal=%d\n",
                (int)uac.volume(UAC_CHAN_MASTER), (int)uac.volume(UAC_CHAN_LEFT),
                (int)uac.mute(UAC_CHAN_MASTER), (int)uac.mute(UAC_CHAN_LEFT));
}
