// Descritor experimental: agrupa AC e AS na mesma funcao para usbccgp.
#include <Arduino.h>
#include "esp32-hal-tinyusb.h"
#include "USBAudioCardDescriptors.h"

namespace {
constexpr uint16_t kConfigLen = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 8 +
                                TUD_AUDIO10_MICROPHONE_DESC_LEN(1);
const uint8_t kConfig[] = {
  TUD_CONFIG_DESCRIPTOR(1, 4, 0, kConfigLen, 0, 500),
  TUD_CDC_DESCRIPTOR(0, 0, 0x85, 64, 0x03, 0x84, 64),
  // IAD: as interfaces 2 e 3 pertencem a uma unica funcao de audio.
  8, TUSB_DESC_INTERFACE_ASSOCIATION, 2, 2, TUSB_CLASS_AUDIO, 0, 0, 0,
  TUD_AUDIO10_DESC_STD_AC(2, 0, 0),
  TUD_AUDIO10_DESC_CS_AC(0x0100, TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN, 3),
  TUD_AUDIO10_DESC_INPUT_TERM(UAC1_ENTITY_MIC_INPUT_TERMINAL, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0, 1, AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED, 0, 0),
  TUD_AUDIO10_DESC_OUTPUT_TERM(UAC1_ENTITY_MIC_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, 0, UAC1_ENTITY_MIC_INPUT_TERMINAL, 0),
  TUD_AUDIO10_DESC_STD_AS_INT(3, 0, 0, 0),
  TUD_AUDIO10_DESC_STD_AS_INT(3, 1, 1, 0),
  TUD_AUDIO10_DESC_CS_AS_INT(UAC1_ENTITY_MIC_OUTPUT_TERMINAL, 1, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(1, 2, 16, 16000),
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x81, (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ADAPTIVE), 64, 1, 0),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 1)
};
static_assert(sizeof(kConfig) == kConfigLen, "tamanho do descritor USB");

const uint8_t kDevice[] = {
  18, TUSB_DESC_DEVICE, 0x00, 0x02,
  0xef, 0x02, 0x01, 64, // USB composite/IAD
  0x3a, 0x30, 0x02, 0x00, // Espressif VID / PID experimental do core
  0x01, 0x01, 1, 2, 3, 1
};
}

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? kConfig : nullptr;
}
extern "C" const uint8_t *tud_descriptor_device_cb(void) { return kDevice; }
