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
USBAudioCard uac(kSampleRate, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);

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
  usbmic.setStreaming(p->interface_enable.enable);
  if (p->interface_enable.enable) {
    // audiod_set_itf() reseta o threshold a cada SET_INTERFACE; reaplicamos
    // para ficar com ~8 ms de folga em vez dos ~48 ms padrao.
    tud_audio_n_set_ep_in_fifo_threshold(0, 256);
  }
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

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------
bool UsbMic::begin(I2SClass *i2s) {
  _i2s = i2s;

  uac.begin();  // no-op em mic-only, mas mantem o contrato da classe
  uac.onEvent(ARDUINO_USB_AUDIO_CARD_ANY_EVENT, onAudioEvent);
  USB.onEvent(onUsbEvent);

  BaseType_t a =
      xTaskCreatePinnedToCore(readerTaskEntry, "uac_rd", 3072, this, 4, nullptr, 1);
  BaseType_t b =
      xTaskCreatePinnedToCore(feederTaskEntry, "uac_fd", 3072, this, 5, nullptr, 0);

  return a == pdPASS && b == pdPASS;
}

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
}
