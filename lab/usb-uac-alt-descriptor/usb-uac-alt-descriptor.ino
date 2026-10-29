/*
 * usb-uac-alt-descriptor.ino
 * ---------------------------------------------------------------------------
 * VARIANTE B (fallback) do protótipo de microfone UAC1.
 *
 * Diferenca para ../usb-uac/usb-uac.ino:
 *   - NAO usa USBAudioCard para montar o descritor. Em vez disso sobrescreve
 *     o simbolo FRACO `tud_descriptor_configuration_cb()` do core
 *     (cores/esp32/esp32-hal-tinyusb.c:419, __attribute__((weak))) e devolve
 *     um descritor composto CDC + UAC1 montado a mao.
 *   - O endpoint isocrono IN e declarado ADAPTIVE (0x09) em vez de
 *     ASYNCHRONOUS (0x05). Um mic UAC1 assincrono, sem endpoint de feedback,
 *     e tecnicamente invalido; alguns hosts (Windows) recusam iniciar o
 *     stream. ADAPTIVE e a escolha segura quando nao existe EP de feedback.
 *   - wMaxPacketSize = 64 (precisa ser >= 34 = 17 amostras * 2 B, que e o
 *     packet_sz_tx_max calculado pelo TinyUSB para 16 kHz mono 16-bit).
 *
 * Use esta variante se o host enumerar mas nao abrir o microfone.
 * O resto (geracao da senoide, ring buffer, tasks) e identico.
 *
 * Compilar (NAO gravar):
 *   arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600" .
 */

#include <Arduino.h>
#include <math.h>

#include "USB.h"
#include "USBAudioCard.h"   // apenas pelos tipos de evento ARDUINO_USB_AUDIO_CARD_*
#include "esp32-hal-tinyusb.h"
#include "class/audio/audio_device.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// Descriptor composto: CDC (itf 0,1) + UAC1 (itf 2 AC, itf 3 AS)
// ---------------------------------------------------------------------------
#define ITF_CDC 0
#define ITF_AUDIO_AC 2
#define ITF_AUDIO_AS 3

#define EP_CDC_NOTIF 0x85
#define EP_CDC_OUT   0x03
#define EP_CDC_IN    0x84
#define EP_AUDIO_IN  0x81

#define UAC_CTRL_BM (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME)

#define AUDIO_DESC_LEN                                                                     \
  (TUD_AUDIO10_DESC_STD_AC_LEN + TUD_AUDIO10_DESC_CS_AC_LEN(1) + TUD_AUDIO10_DESC_INPUT_TERM_LEN + \
   TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1) + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN + TUD_AUDIO10_DESC_STD_AS_LEN + \
   TUD_AUDIO10_DESC_STD_AS_LEN + TUD_AUDIO10_DESC_CS_AS_INT_LEN + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(1) + \
   TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + AUDIO_DESC_LEN)

static const uint8_t kConfigDescriptor[] = {
  // num configs, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),

  // CDC: itf 0 (control) + itf 1 (data), EP 0x85 notify / 0x03 out / 0x84 in
  TUD_CDC_DESCRIPTOR(ITF_CDC, 0, EP_CDC_NOTIF, 64, EP_CDC_OUT, EP_CDC_IN, 64),

  // ---- UAC1 Audio Control (itf 2) ----
  TUD_AUDIO10_DESC_STD_AC(ITF_AUDIO_AC, 0x00, 0x00),
  TUD_AUDIO10_DESC_CS_AC(
    0x0100,
    (TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1) + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN),
    ITF_AUDIO_AS
  ),
  // Mic input terminal -> feature unit -> USB streaming output terminal
  TUD_AUDIO10_DESC_INPUT_TERM(0x01, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0x00, 0x01, AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED, 0x00, 0x00),
  TUD_AUDIO10_DESC_FEATURE_UNIT(0x02, 0x01, 0x00, UAC_CTRL_BM, UAC_CTRL_BM),
  TUD_AUDIO10_DESC_OUTPUT_TERM(0x03, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, 0x02, 0x00),

  // ---- UAC1 Audio Streaming (itf 3) ----
  TUD_AUDIO10_DESC_STD_AS_INT(ITF_AUDIO_AS, 0x00, 0x00, 0x00),  // alt 0: sem banda
  TUD_AUDIO10_DESC_STD_AS_INT(ITF_AUDIO_AS, 0x01, 0x01, 0x00),  // alt 1: streaming
  TUD_AUDIO10_DESC_CS_AS_INT(0x03, 0x01, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(0x01, 0x02, 16, 16000),        // mono, 2 B, 16 bit, 16 kHz
  // EP isocrono IN ADAPTIVE (0x09) -- ver comentario no topo
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(
    EP_AUDIO_IN, (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ADAPTIVE), 64, 0x01, 0x00
  ),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 0x0001),
};

static_assert(sizeof(kConfigDescriptor) == CONFIG_TOTAL_LEN, "config descriptor length mismatch");

// Substitui o simbolo fraco do core.
// A declaracao com linkage C ja vem de esp32-hal-tinyusb.h.
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return kConfigDescriptor;
}

// ---------------------------------------------------------------------------
// Geracao de audio (identica a variante A)
// ---------------------------------------------------------------------------
static const uint32_t kSampleRate    = 16000;
static const uint16_t kFramesPerMs   = kSampleRate / 1000;
static const uint16_t kChunkBytes    = kFramesPerMs * 2;
static const float    kToneHz        = 440.0f;
static const float    kAmplitude     = 8000.0f;

static volatile bool g_streaming = false;

// Declarado localmente em USBCDC.cpp/USBAudioCard.cpp; nao esta em header publico.
esp_err_t arduino_usb_event_handler_register_with(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg);

static void producerTask(void *arg) {
  (void)arg;
  const float twoPi = 6.28318530718f;
  const float inc   = twoPi * kToneHz / (float)kSampleRate;
  float phase = 0.0f;
  int16_t pcm[kFramesPerMs];

  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
    if (!g_streaming) {
      continue;
    }
    for (uint16_t i = 0; i < kFramesPerMs; i++) {
      pcm[i] = (int16_t)(sinf(phase) * kAmplitude);
      phase += inc;
      if (phase >= twoPi) phase -= twoPi;
    }
    uint16_t off = 0;
    while (off < kChunkBytes) {
      uint16_t w = tud_audio_n_write(0, (const uint8_t *)pcm + off, (uint16_t)(kChunkBytes - off));
      if (w == 0) break;
      off += w;
    }
  }
}

static void onAudioEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  arduino_usb_audio_card_event_data_t *p = (arduino_usb_audio_card_event_data_t *)data;
  if (id == ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT) {
    g_streaming = p->interface_enable.enable;
    if (g_streaming) {
      tud_audio_n_set_ep_in_fifo_threshold(0, 256);  // ~8 ms de folga
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("=== ESP32-S3 USB UAC1 mic (variant B: custom descriptor) ===");
  Serial.printf("config descriptor: %u bytes, %u interfaces\n",
                (unsigned)sizeof(kConfigDescriptor), 4u);

  // A interface de audio NAO e registrada via tinyusb_enable_interface:
  // o descritor devolvido por tud_descriptor_configuration_cb() manda.
  // ARDUINO_USB_AUDIO_CARD_EVENTS e postado pelo driver do core
  // (USBAudioCard.cpp, sempre compilado na lib USB).
  arduino_usb_event_handler_register_with(ARDUINO_USB_AUDIO_CARD_EVENTS, ARDUINO_USB_AUDIO_CARD_ANY_EVENT, onAudioEvent, NULL);

  xTaskCreatePinnedToCore(producerTask, "uac_prod", 3072, NULL, 5, NULL, 1);
  Serial.println("[uac] producer started");
}

void loop() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs >= 1000) {
    lastMs = now;
    Serial.printf("[uac] streaming=%d fifo=%u\n", (int)g_streaming, (unsigned)tud_audio_n_available(0));
  }
  delay(10);
}
