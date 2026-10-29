/*
 * usb-uac.ino - ESP32-S3 como MICROFONE USB (UAC1) + CDC serial (composto)
 * ---------------------------------------------------------------------------
 * Protótipo minimo, sem alterar o core.
 *
 * Abordagem: usa a classe USBAudioCard que JA EXISTE no core
 * esp32:esp32@3.3.12 (libraries/USB/src/USBAudioCard.cpp). Ela registra a
 * interface USB_INTERFACE_AUDIO via tinyusb_enable_interface() e monta o
 * descritor UAC1 (TUD_AUDIO10_MICROPHONE_DESCRIPTOR) no descritor de
 * configuracao composto junto com o CDC do Serial.
 *
 * Como TUD_OPT_HIGH_SPEED == 0 no ESP32-S3 (full-speed), o core escolhe
 * UAC1 automaticamente (USBAudioCard.cpp:125-187).
 *
 * Fluxo de dados:
 *   producerTask (1 kHz) --> ring buffer SPSC --> feederTask --> uac.write()
 *   --> FIFO interno do TinyUSB (CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ = 3072 B)
 *   --> EP IN 0x81 isocrono, 1 pacote por SOF.
 *
 * Compilar (NAO gravar):
 *   arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600" .
 */

#include <Arduino.h>
#include <math.h>

#include "USB.h"
#include "USBAudioCard.h"
// USBAudioCard.h nao inclui os headers da classe de audio do TinyUSB.
// esp32-hal-tinyusb.h traz tusb_option.h + tusb_config.h (CFG_TUD_AUDIO=1),
// e ai sim audio_device.h declara tud_audio_n_available()/set_ep_in_fifo_threshold().
#include "esp32-hal-tinyusb.h"
#include "class/audio/audio_device.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// Parametros de audio
// ---------------------------------------------------------------------------
static const uint32_t kSampleRate     = 16000;             // Hz
static const uint16_t kFramesPerMs    = kSampleRate / 1000; // 16 amostras/ms
static const uint16_t kBytesPerFrame  = 2;                  // mono, 16 bits
static const uint16_t kChunkBytes     = kFramesPerMs * kBytesPerFrame; // 32 B/ms
static const float    kToneHz         = 440.0f;
static const float    kAmplitude      = 8000.0f;            // ~ -12 dBFS

// ---------------------------------------------------------------------------
// Instancia global: o construtor roda no static-init, ANTES de app_main()
// chamar Serial.begin()/USB.begin() (CDCOnBoot=cdc). Isso e obrigatorio:
// tinyusb_enable_interface() falha depois que o TinyUSB iniciou.
// ---------------------------------------------------------------------------
USBAudioCard uac(kSampleRate, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);

// ---------------------------------------------------------------------------
// Ring buffer SPSC (produtor: producerTask / consumidor: feederTask)
// ---------------------------------------------------------------------------
static const uint32_t kRingSamples = 8192;      // 512 ms @ 16 kHz
static const uint32_t kRingMask    = kRingSamples - 1; // potencia de 2
static int16_t  g_ring[kRingSamples];
static volatile uint32_t g_ringHead = 0; // escrito so pelo produtor
static volatile uint32_t g_ringTail = 0; // escrito so pelo consumidor
static portMUX_TYPE g_ringMux = portMUX_INITIALIZER_UNLOCKED;

static inline uint32_t ringCount() {
  portENTER_CRITICAL(&g_ringMux);
  uint32_t n = (g_ringHead - g_ringTail) & kRingMask;
  portEXIT_CRITICAL(&g_ringMux);
  return n;
}

static inline uint32_t ringFree() { return kRingSamples - 1 - ringCount(); }

static inline bool ringPush(int16_t s) {
  bool ok = false;
  portENTER_CRITICAL(&g_ringMux);
  uint32_t next = (g_ringHead + 1) & kRingMask;
  if (next != g_ringTail) {
    g_ring[g_ringHead] = s;
    g_ringHead = next;
    ok = true;
  }
  portEXIT_CRITICAL(&g_ringMux);
  return ok;
}

static inline uint32_t ringPop(int16_t *dst, uint32_t maxSamples) {
  portENTER_CRITICAL(&g_ringMux);
  uint32_t n = (g_ringHead - g_ringTail) & kRingMask;
  if (n > maxSamples) n = maxSamples;
  for (uint32_t i = 0; i < n; i++) {
    dst[i] = g_ring[(g_ringTail + i) & kRingMask];
  }
  g_ringTail = (g_ringTail + n) & kRingMask;
  portEXIT_CRITICAL(&g_ringMux);
  return n;
}

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
static volatile bool     g_streaming = false;   // alt setting != 0
static volatile uint32_t g_underruns = 0;
static volatile uint32_t g_dropped   = 0;

// ---------------------------------------------------------------------------
// Produtor: 440 Hz, 16 amostras a cada 1 ms
// ---------------------------------------------------------------------------
static void producerTask(void *arg) {
  (void)arg;
  const float twoPi = 6.28318530718f;
  const float inc   = twoPi * kToneHz / (float)kSampleRate;
  float phase = 0.0f;

  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
    if (!g_streaming) {
      continue; // nao enche o ring enquanto o host nao abre o stream
    }
    for (uint16_t i = 0; i < kFramesPerMs; i++) {
      int16_t s = (int16_t)(sinf(phase) * kAmplitude);
      if (!ringPush(s)) {
        g_dropped = g_dropped + 1;
      }
      phase += inc;
      if (phase >= twoPi) phase -= twoPi;
    }
  }
}

// ---------------------------------------------------------------------------
// Consumidor: drena o ring para o FIFO do TinyUSB
// ---------------------------------------------------------------------------
static void feederTask(void *arg) {
  (void)arg;
  static int16_t staging[256];

  for (;;) {
    if (!g_streaming) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    uint32_t n = ringPop(staging, 256);
    if (n == 0) {
      g_underruns = g_underruns + 1;
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    uint16_t total = (uint16_t)(n * sizeof(int16_t));
    uint16_t off   = 0;
    while (off < total) {
      uint16_t w = uac.write((const uint8_t *)staging + off, (uint16_t)(total - off));
      if (w == 0) break; // FIFO cheio; o resto fica para a proxima volta
      off += w;
    }
    if (off < total) {
      // devolve o que nao coube para nao perder amostras
      portENTER_CRITICAL(&g_ringMux);
      uint32_t back = (total - off) / sizeof(int16_t);
      g_ringTail = (g_ringTail - back) & kRingMask;
      portEXIT_CRITICAL(&g_ringMux);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

// ---------------------------------------------------------------------------
// Eventos do audio (volume/mute/alt setting)
// ---------------------------------------------------------------------------
static void onAudioEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  arduino_usb_audio_card_event_data_t *p = (arduino_usb_audio_card_event_data_t *)data;
  if (id == ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT) {
    g_streaming = p->interface_enable.enable;
    if (g_streaming) {
      // Reduz a latencia do FIFO interno: o padrao e depth/2 (~48 ms @ 16 kHz mono).
      // 256 B = 8 ms de folga. audiod_set_itf() reseta para depth/2 a cada SET_INTERFACE,
      // por isso reaplicamos aqui.
      tud_audio_n_set_ep_in_fifo_threshold(0, 256);
    }
  } else if (id == ARDUINO_USB_AUDIO_CARD_SAMPLE_RATE_EVENT) {
    Serial.printf("[uac] host set sample rate = %lu Hz\n", (unsigned long)p->sample_rate.rate);
  }
}

// ---------------------------------------------------------------------------
static void onUsbEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  (void)data;
  if (id == ARDUINO_USB_STARTED_EVENT) {
    Serial.println("[usb] mounted");
  } else if (id == ARDUINO_USB_STOPPED_EVENT) {
    g_streaming = false;
    Serial.println("[usb] unmounted");
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("=== ESP32-S3 USB UAC1 microphone prototype ===");
  Serial.printf("tone      : %.0f Hz\n", (double)kToneHz);
  Serial.printf("format    : %lu Hz / 16 bit / mono (UAC1, full-speed)\n", (unsigned long)kSampleRate);
  Serial.printf("uac cfg   : rate=%lu bps=%u bytes/sample=%u\n",
                (unsigned long)uac.sampleRate(), (unsigned)uac.bitsPerSample(), (unsigned)uac.bytesPerSample());
  Serial.printf("streaming : %s\n", g_streaming ? "yes" : "no");

  // USB.begin() ja foi chamado por app_main() (CDCOnBoot=cdc). begin() aqui
  // apenas aloca o buffer de speaker (no-op no modo mic-only).
  uac.begin();
  uac.onEvent(ARDUINO_USB_AUDIO_CARD_ANY_EVENT, onAudioEvent);
  USB.onEvent(onUsbEvent);

  xTaskCreatePinnedToCore(producerTask, "uac_prod", 3072, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(feederTask, "uac_feed", 3072, NULL, 5, NULL, 1);

  Serial.println("[uac] tasks started");
}

void loop() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs >= 1000) {
    lastMs = now;
    Serial.printf("[uac] streaming=%d ring=%lu/%lu underruns=%lu dropped=%lu avail=%u\n",
                  (int)g_streaming, (unsigned long)ringCount(), (unsigned long)kRingSamples,
                  (unsigned long)g_underruns, (unsigned long)g_dropped,
                  (unsigned)tud_audio_n_available(0));
  }
  delay(10);
}
