// Verificacao de compilacao/linkagem do AEC ESP-SR; nao gravar na placa.
#include <Arduino.h>
#include <ESP_SR.h>
#include <esp_aec.h>
#include <esp_afe_config.h>
#include <esp_afe_sr_models.h>

void setup() {
  Serial.begin(115200);
  aec_handle_t *aec = aec_create(16000, 4, 1, AEC_MODE_FD_LOW_COST);
  if (!aec) {
    Serial.println("AEC indisponivel");
    return;
  }
  Serial.printf("AEC: %d amostras por quadro\n", aec_get_chunksize(aec));
  aec_destroy(aec);
  // Apenas prova de link; inicializacao sem modelos precisa ser validada.
  afe_config_t *cfg = afe_config_init("MR", nullptr, AFE_TYPE_VC, AFE_MODE_LOW_COST);
  if (cfg) {
    cfg->aec_init = true;
    cfg->ns_init = true;
    cfg->afe_ns_mode = AFE_NS_MODE_WEBRTC;
    cfg->wakenet_init = false;
    cfg->vad_init = false;
    Serial.printf("AFE: aec=%d ns=%d\n", cfg->aec_init, cfg->ns_init);
    const esp_afe_sr_iface_t *iface = esp_afe_handle_from_config(cfg);
    if (iface) {
      esp_afe_sr_data_t *data = iface->create_from_config(cfg);
      if (data) {
        Serial.printf("AFE: feed=%d fetch=%d\n", iface->get_feed_chunksize(data), iface->get_fetch_chunksize(data));
        iface->destroy(data);
      }
    }
    afe_config_free(cfg);
  }
}
void loop() { delay(1000); }
