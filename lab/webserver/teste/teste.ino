// Sketch de teste do modulo FileServer (lab/webserver).
//
// Monta o SD_MMC exatamente como o firmware principal (1-bit, 20 MHz), sobe o
// AP "ESP32-MIC" e atende a pagina web. Nao grava audio de verdade: os
// callbacks de gravacao apenas alternam um estado simulado, para validar a
// interface e as rotas.
//
// Compilar: veja RELATORIO.md (o comando completo esta la).
// Resumo: arduino-cli compile --fqbn "<FQBN>" --library <lab/webserver>
//         <lab/webserver/teste>

#include <Arduino.h>
#include <SD_MMC.h>

#include "webserver.h"

// Mesmos pinos de board.h do firmware principal.
#define PIN_SD_CLK 40
#define PIN_SD_CMD 42
#define PIN_SD_D0 41

#define PIN_BAT_ADC 8
#define BAT_DIVIDER_RATIO 3.0f
#define BAT_CALIBRATION 0.990476f

FileServer web;

bool recording = false;
uint32_t recStartMs = 0;

bool cbIsRecording() { return recording; }

void cbStart() {
  if (recording) return;
  recording = true;
  recStartMs = millis();
  Serial.println("[TESTE] gravacao simulada iniciada");
}

void cbStop() {
  if (!recording) return;
  recording = false;
  Serial.printf("[TESTE] gravacao simulada parada (%lu s)\n",
                (unsigned long)((millis() - recStartMs) / 1000));
}

float cbVolts() {
  int mv = analogReadMilliVolts(PIN_BAT_ADC);
  return (mv * BAT_DIVIDER_RATIO / 1000.0f) / BAT_CALIBRATION;
}

bool cbBatteryOk() { return true; }

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== teste: servidor web + wifi ===");

  // Igual a Recorder::beginSd(): 1-bit, sem formatar, 20 MHz.
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, -1, -1, -1)) {
    Serial.println("[SD] setPins falhou");
  }
  if (!SD_MMC.begin("/sdcard", true, false, 20000)) {
    Serial.println("[SD] montagem falhou (a pagina continua no ar)");
  } else {
    if (!SD_MMC.exists("/AUDIO")) SD_MMC.mkdir("/AUDIO");
    Serial.printf("[SD] %.1f GB livres\n",
                  (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1073741824.0);
  }

  web.setRecorderCallbacks(cbIsRecording, cbStart, cbStop);
  web.setBatteryCallback(cbVolts, cbBatteryOk);
  web.begin();

  Serial.printf("[WEB] acesse http://%s/\n", web.ip().c_str());
}

void loop() {
  web.update();
  delay(2);
}
