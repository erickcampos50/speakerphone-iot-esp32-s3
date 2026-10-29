// Descritor experimental: agrupa AC e AS na mesma funcao para usbccgp.
#include <Arduino.h>
#include "esp32-hal-tinyusb.h"
#include "USBAudioCardDescriptors.h"

namespace {
constexpr uint16_t kConfigLen = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 8 +
                                TUD_AUDIO10_HEADSET_MONO_DESC_LEN(1);
uint8_t kConfig[] = {
  TUD_CONFIG_DESCRIPTOR(1, 5, 0, kConfigLen, 0, 500),
  TUD_CDC_DESCRIPTOR(0, 0, 0x85, 64, 0x03, 0x84, 64),
  // AC, reproduzir e capturar sao uma unica funcao de audio.
  8, TUSB_DESC_INTERFACE_ASSOCIATION, 2, 3, TUSB_CLASS_AUDIO, 0, 0, 0,
  TUD_AUDIO10_HEADSET_MONO_DESCRIPTOR(2, 0, 0x01, 0x81, 48000, 1, 1, 2, 16, 16000)
};
static_assert(sizeof(kConfig) == kConfigLen, "tamanho do descritor USB");

const uint8_t kDevice[] = {
  18, TUSB_DESC_DEVICE, 0x00, 0x02,
  0xef, 0x02, 0x01, 64, // USB composite/IAD
  0x3a, 0x30, 0x03, 0x00, // PID experimental distinto do microfone anterior
  0x01, 0x01, 1, 2, 3, 1
};
}

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  // O template do core marca IN assincrono; manter a variante adaptativa
  // utilizada no microfone experimental, sem alterar o core instalado.
  for (size_t p = 0; p + 3 < sizeof(kConfig);) {
    size_t n = kConfig[p];
    if (n < 2 || p + n > sizeof(kConfig)) break;
    if (kConfig[p + 1] == TUSB_DESC_ENDPOINT && n >= 4 && kConfig[p + 2] == 0x81)
      kConfig[p + 3] = (uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ADAPTIVE;
    p += n;
  }
  return index == 0 ? kConfig : nullptr;
}
extern "C" const uint8_t *tud_descriptor_device_cb(void) { return kDevice; }
