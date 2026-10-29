#include "afe_probe.h"
#include <Arduino.h>
#include <ESP_SR.h>
#include <esp_afe_config.h>
#include <esp_afe_sr_models.h>

namespace {
const esp_afe_sr_iface_t *iface = nullptr;
esp_afe_sr_data_t *data = nullptr;
int stage = 0;
int inputFrames = 0, inputChannels = 0, outputFrames = 0;
bool aecConfigured = false, nsConfigured = false;
uint32_t heapBefore = 0, heapAfter = 0;
}

void afeProbeBegin() {
  heapBefore = ESP.getFreeHeap();
  afe_config_t *cfg = afe_config_init("MR", nullptr, AFE_TYPE_VC, AFE_MODE_LOW_COST);
  if (!cfg) return;
  stage = 1;
  cfg->aec_init = true;
  cfg->ns_init = true;
  cfg->afe_ns_mode = AFE_NS_MODE_WEBRTC;
  cfg->wakenet_init = false;
  cfg->vad_init = false;
  cfg->agc_init = false;
  aecConfigured = cfg->aec_init;
  nsConfigured = cfg->ns_init;
  iface = esp_afe_handle_from_config(cfg);
  if (iface) {
    stage = 2;
    data = iface->create_from_config(cfg);
    if (data) {
      stage = 3;
      inputFrames = iface->get_feed_chunksize(data);
      inputChannels = iface->get_feed_channel_num(data);
      outputFrames = iface->get_fetch_chunksize(data);
    }
  }
  afe_config_free(cfg);
  heapAfter = ESP.getFreeHeap();
}

void afeProbePrint() {
  Serial.printf("[AFE TESTE] etapa=%d AEC_solicitado=%d NS_solicitado=%d\n", stage, aecConfigured, nsConfigured);
  Serial.printf("[AFE TESTE] feed=%d canais=%d fetch=%d heap_antes=%lu heap_depois=%lu\n",
                inputFrames, inputChannels, outputFrames, (unsigned long)heapBefore, (unsigned long)heapAfter);
  Serial.println("[AFE TESTE] audio USB ainda nao processado; nao e cancelamento ativo");
}
