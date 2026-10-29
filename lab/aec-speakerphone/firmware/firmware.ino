// ESP32-S3-AUDIO-Board - gravador de audio para cartao SD
// - LEDs sinalizam ligado / fonte de energia / bateria / gravacao / erro
// - Gravacao WAV 16 kHz, 16 bits, estereo em /AUDIO
// - Botoes: BOOT e K1 gravam, K2 status, K3 amplificador
// - Console serial a 115200

#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include "esp32-hal-tinyusb.h"

#include "ESP_I2S.h"
#include "battery.h"
#include "afe_probe.h"
#include "beeper.h"
#include "board.h"
#include "es7210.h"
#include "led_ring.h"
#include "recorder.h"
#include "rtc.h"
#include "tca9555.h"
#include "usbmic.h"

namespace {
constexpr const char *kFwVersion = "1.0.0";

Tca9555 exio;
Battery battery;
LedRing leds;
Recorder rec;
Es7210 codec;
RtcPcf85063 rtc;
I2SClass i2s;
Beeper beeper;

bool amp_on = false;
bool amp_before_record = false;
bool amp_before_beep = false;
int mic_gain = 10;  // 30 dB (enum do ES7210)
int mic_vol_db = 0;  // volume digital do ES7210 (-95..+32 dB)

// Tabela do enum de ganho do ES7210 (0..14) para dB.
constexpr float kGainDb[15] = {0.0f, 3.0f, 6.0f, 9.0f, 12.0f, 15.0f,
                               18.0f, 21.0f, 24.0f, 27.0f, 30.0f, 33.0f,
                               34.5f, 36.0f, 37.5f};

// ---------------------------------------------------------------------------
// Botoes
// ---------------------------------------------------------------------------
class Debouncer {
 public:
  // Retorna true uma unica vez, no instante em que o botao e solto depois de
  // ter ficado pressionado por pelo menos 30 ms. O primeiro estado lido e
  // adotado como referencia (sem disparo espurio no boot).
  bool released(bool down, uint32_t now) {
    if (!_init) {
      _init = true;
      _raw = down;
      _stable = down;
      _t = now;
      return false;
    }
    if (down != _raw) {
      _raw = down;
      _t = now;
    }
    if (now - _t >= 30 && _stable != _raw) {
      bool was_down = _stable;
      _stable = _raw;
      return was_down && !_stable;
    }
    return false;
  }

 private:
  bool _init = false;
  bool _raw = false;
  bool _stable = false;
  uint32_t _t = 0;
};

Debouncer btn_boot, btn_k1, btn_k2, btn_k3;

// ---------------------------------------------------------------------------
// Nome dos arquivos (RTC)
// ---------------------------------------------------------------------------
bool provideFileName(char *out, size_t len) {
  if (!rtc.present()) return false;
  return rtc.stamp(out, len);
}

// ---------------------------------------------------------------------------
// Gravacao
// ---------------------------------------------------------------------------
// O Beeper liga o amplificador so durante o bip e devolve o estado anterior,
// para nao desligar o amp que o usuario ligou no K3.
void ampForBeep(bool on) {
  if (on) {
    amp_before_beep = amp_on;
    amp_on = true;
    exio.write(EXIO_PA_CTRL, true);
  } else {
    amp_on = amp_before_beep;
    exio.write(EXIO_PA_CTRL, amp_before_beep);
  }
}

// Toca uma sequencia curta e espera terminar (usado fora da gravacao, para o
// som nao entrar no arquivo). O limite evita travar se o codec nao responder.
void beepAndWait(BeepEvent ev, uint32_t limit_ms = 1200) {
  beeper.play(ev);
  uint32_t t0 = millis();
  while (beeper.busy() && millis() - t0 < limit_ms) {
    beeper.update();
    delay(1);
  }
}

// A unica task de captura entrega uma copia para a fila do gravador.
void recorderTap(const int16_t *frames, size_t nframes) {
  rec.pushAudio(frames, nframes);
}

void startRecording() {
  if (rec.isRecording()) return;

  // Bip de "gravando" antes de abrir o arquivo, para nao ser captado.
  beepAndWait(BeepEvent::RecordStart);

  // Desliga o amplificador para evitar realimentacao.
  amp_before_record = amp_on;
  if (amp_on) {
    amp_on = false;
    exio.write(EXIO_PA_CTRL, false);
  }

  if (!rec.start(&i2s)) {
    Serial.println("[REC] falha ao iniciar gravacao");
    beeper.play(BeepEvent::Error);
  }
}

void stopRecording() {
  if (!rec.isRecording()) return;
  rec.stop();
  if (rec.busy()) return;
  if (amp_before_record) {
    amp_on = true;
    exio.write(EXIO_PA_CTRL, true);
  }
  if (rec.fileName()[0] != 0) rec.printStats(rec.fileName());
  beepAndWait(BeepEvent::RecordStop);
}

void toggleRecording() {
  if (rec.isRecording()) {
    stopRecording();
  } else {
    startRecording();
  }
}

void toggleAmp() {
  amp_on = !amp_on;
  exio.write(EXIO_PA_CTRL, amp_on);
  leds.flashConfirm();
  Serial.printf("[AMP] %s\n", amp_on ? "ligado" : "desligado");
  beeper.play(amp_on ? BeepEvent::AmpOn : BeepEvent::AmpOff);
}

void showStatus() {
  int pct = battery.available() ? battery.percent() : 0;
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  uint8_t free_pct =
      total ? (uint8_t)(((total - used) * 100) / total) : 0;

  leds.showStatus((uint8_t)pct, free_pct);

  if (battery.available()) {
    Serial.printf("[STATUS] bateria %d%% (%.2f V)\n", pct, battery.volts());
  } else {
    Serial.println("[STATUS] bateria: medicao indisponivel");
  }
  rec.printSdInfo();
}

// ---------------------------------------------------------------------------
// Console serial
// ---------------------------------------------------------------------------
void printTime() {
  if (!rtc.present()) {
    Serial.println("[RTC] PCF85063 nao respondeu");
    return;
  }
  struct tm t;
  if (rtc.read(t)) {
    Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d\n", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    Serial.println("[RTC] hora invalida (sem bateria de backup?)");
  }
}

void printInfo() {
  Serial.println("--------------------------------------------------");
  Serial.printf("Firmware : %s\n", kFwVersion);
  Serial.printf("Chip     : %s rev %d, %d nucleos @ %lu MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (unsigned long)getCpuFrequencyMhz());
  Serial.printf("Flash    : %.1f MB   PSRAM: %.1f MB\n",
                ESP.getFlashChipSize() / 1048576.0,
                ESP.getPsramSize() / 1048576.0);
  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("MAC      : %02X:%02X:%02X:%02X:%02X:%02X\n",
                (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
                (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
                (uint8_t)(mac >> 40));
  Serial.printf("Heap     : %lu bytes livres\n", (unsigned long)ESP.getFreeHeap());
  Serial.println("Fonte    : nao detectada (conexao USB nao indica alimentacao)");
  if (battery.available()) {
    Serial.printf("Bateria  : %.2f V (%d%%) no GPIO%d\n", battery.volts(),
                  battery.percent(), battery.pin());
  } else {
    Serial.println("Bateria  : medicao indisponivel (0R do BAT_ADC?)");
  }
  Serial.printf("SD       : %s\n", rec.sdReady() ? "montado" : "ausente");
  Serial.printf("MIC      : %s (PGA %.1f dB, volume %d dB)\n",
                codec.present() ? "ES7210 ok" : "ausente", kGainDb[mic_gain],
                mic_vol_db);
  Serial.printf("Amp      : %s\n", amp_on ? "ligado" : "desligado");
  Serial.printf("USB mic  : %s\n", usbmic.mounted() ? "montado" : "desconectado");
  printTime();
  Serial.println("--------------------------------------------------");
}

void printHelp() {
  Serial.println("Comandos:");
  Serial.println("  rec | stop        inicia/para a gravacao");
  Serial.println("  ls                lista /AUDIO");
  Serial.println("  rm <arquivo>      apaga um arquivo");
  Serial.println("  bat               tensao/percentual da bateria");
  Serial.println("  adc               leitura crua do BAT_ADC (GPIO1 e GPIO8)");
  Serial.println("  level [segundos]  medidor ao vivo do microfone (padrao 5 s)");
  Serial.println("  sd                espaco no cartao");
  Serial.println("  stats [arquivo]   analisa um WAV (RMS/pico por canal)");
  Serial.println("  gain <0-14>       ganho do ES7210 (0=0dB, 10=30dB)");
  Serial.println("  vol <dB>          volume digital do ES7210 (-95..32 dB)");
  Serial.println("  amp on|off        amplificador");
  Serial.println("  beep <evento>     testa um bip (boot/start/stop/low/error/wifi)");
  Serial.println("  usb               estado do microfone USB (UAC)");
  Serial.println("  time              le o RTC");
  Serial.println("  time set AAAA-MM-DD HH:MM:SS");
  Serial.println("  info              resumo do hardware");
  Serial.println("  reboot            reinicia");
}

// Medidor ao vivo: le o I2S por N segundos e imprime RMS/pico do canal 0
// a cada 0,5 s. Serve para conferir ganho e se o microfone capta som.
void liveLevel(uint32_t duration_ms) {
  (void)duration_ms;
  Serial.println("[MIC] level temporariamente indisponivel durante a integracao" );
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (cmd == nullptr) return;

  if (!strcmp(cmd, "help")) {
    printHelp();
  } else if (!strcmp(cmd, "rec")) {
    startRecording();
  } else if (!strcmp(cmd, "stop")) {
    stopRecording();
  } else if (!strcmp(cmd, "ls")) {
    rec.listFiles();
  } else if (!strcmp(cmd, "sd")) {
    rec.printSdInfo();
  } else if (!strcmp(cmd, "bat")) {
    if (battery.available()) {
      Serial.printf("[BAT] %.2f V (%d%%) GPIO%d\n", battery.volts(),
                    battery.percent(), battery.pin());
    } else {
      Serial.println("[BAT] medicao indisponivel");
    }
  } else if (!strcmp(cmd, "adc")) {
    for (int pin : {PIN_BAT_ADC_SCHEMATIC, PIN_BAT_ADC_DEMO}) {
      int raw = 0;
      for (int i = 0; i < 8; i++) raw += analogReadMilliVolts(pin);
      raw /= 8;
      Serial.printf("[ADC] GPIO%d: %d mV no pino -> %.2f V de bateria\n", pin,
                    raw, (raw * BAT_DIVIDER_RATIO / 1000.0f) / BAT_CALIBRATION);
    }
  } else if (!strcmp(cmd, "level")) {
    char *arg = strtok(nullptr, " ");
    uint32_t secs = arg ? (uint32_t)atoi(arg) : 5;
    if (secs < 1) secs = 1;
    if (secs > 30) secs = 30;
    liveLevel(secs * 1000);
  } else if (!strcmp(cmd, "stats")) {
    char *arg = strtok(nullptr, " ");
    const char *path = arg ? arg : rec.fileName();
    if (path != nullptr && path[0] != 0) {
      rec.printStats(path);
    } else {
      Serial.println("[WAV] informe um arquivo");
    }
  } else if (!strcmp(cmd, "rm")) {
    char *arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      Serial.println("[RM] informe o arquivo");
      return;
    }
    char path[80];
    if (arg[0] == '/') {
      snprintf(path, sizeof(path), "%s", arg);
    } else {
      snprintf(path, sizeof(path), "/AUDIO/%s", arg);
    }
    if (rec.isRecording()) {
      Serial.println("[RM] pare a gravacao primeiro");
    } else if (SD_MMC.remove(path)) {
      Serial.printf("[RM] removido %s\n", path);
    } else {
      Serial.printf("[RM] falha ao remover %s\n", path);
    }
  } else if (!strcmp(cmd, "gain")) {
    char *arg = strtok(nullptr, " ");
    int g = arg ? atoi(arg) : -1;
    if (g < 0 || g > 14) {
      Serial.println("[MIC] ganho deve ser 0..14");
    } else {
      mic_gain = g;
      bool ok = codec.configure(AUDIO_SAMPLE_RATE, 256, mic_gain, true);
      codec.setVolumeDb(mic_vol_db);
      Serial.printf("[MIC] PGA %.1f dB (%s)\n", kGainDb[mic_gain],
                    ok ? "ok" : "falhou");
    }
  } else if (!strcmp(cmd, "vol")) {
    char *arg = strtok(nullptr, " ");
    int v = arg ? atoi(arg) : mic_vol_db;
    if (v < -95 || v > 32) {
      Serial.println("[MIC] volume deve ser -95..32 dB");
    } else {
      mic_vol_db = v;
      bool ok = codec.setVolumeDb(mic_vol_db);
      Serial.printf("[MIC] volume digital %d dB (%s)\n", mic_vol_db,
                    ok ? "ok" : "falhou");
    }
  } else if (!strcmp(cmd, "amp")) {
    char *arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      Serial.printf("[AMP] %s\n", amp_on ? "ligado" : "desligado");
    } else if (!strcmp(arg, "on")) {
      amp_on = true;
      exio.write(EXIO_PA_CTRL, true);
      leds.flashConfirm();
    } else if (!strcmp(arg, "off")) {
      amp_on = false;
      exio.write(EXIO_PA_CTRL, false);
      leds.flashConfirm();
    }
  } else if (!strcmp(cmd, "time")) {
    char *arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      printTime();
    } else if (!strcmp(arg, "set")) {
      char *date = strtok(nullptr, " ");
      char *clock = strtok(nullptr, " ");
      int y, mo, d, h, mi, s;
      if (date && clock &&
          sscanf(date, "%d-%d-%d", &y, &mo, &d) == 3 &&
          sscanf(clock, "%d:%d:%d", &h, &mi, &s) == 3 &&
          rtc.set(y, mo, d, h, mi, s)) {
        Serial.println("[RTC] hora ajustada");
        printTime();
      } else {
        Serial.println("[RTC] formato: time set AAAA-MM-DD HH:MM:SS");
      }
    }
  } else if (!strcmp(cmd, "beep")) {
    char *arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      Serial.println("[BEEP] boot|start|stop|low|error|wifi|ampon|ampoff");
    } else if (!strcmp(arg, "boot")) {
      beeper.play(BeepEvent::Boot);
    } else if (!strcmp(arg, "start")) {
      beeper.play(BeepEvent::RecordStart);
    } else if (!strcmp(arg, "stop")) {
      beeper.play(BeepEvent::RecordStop);
    } else if (!strcmp(arg, "low")) {
      beeper.play(BeepEvent::LowBattery);
    } else if (!strcmp(arg, "error")) {
      beeper.play(BeepEvent::Error);
    } else if (!strcmp(arg, "wifi")) {
      beeper.play(BeepEvent::WifiReady);
    } else if (!strcmp(arg, "ampon")) {
      beeper.play(BeepEvent::AmpOn);
    } else if (!strcmp(arg, "ampoff")) {
      beeper.play(BeepEvent::AmpOff);
    } else {
      Serial.println("[BEEP] evento desconhecido");
    }
  } else if (!strcmp(cmd, "usb")) {
    usbmic.printStatus();
  } else if (!strcmp(cmd, "afe")) {
    afeProbePrint();
  } else if (!strcmp(cmd, "info")) {
    printInfo();
  } else if (!strcmp(cmd, "reboot")) {
    Serial.println("[SYS] reiniciando...");
    delay(100);
    ESP.restart();
  } else {
    Serial.printf("comando desconhecido: %s (use 'help')\n", cmd);
  }
}

void pollSerial() {
  static char buf[128];
  static size_t len = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = 0;
      if (len > 0) handleCommand(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// Botoes / LEDs
// ---------------------------------------------------------------------------
void pollButtons() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 15) return;
  last = now;

  bool boot = digitalRead(PIN_BTN_BOOT) == LOW;

  // Uma unica leitura de I2C por varredura (as 3 teclas vem do mesmo port).
  int ports = exio.readPort();
  if (ports < 0) {
    if (btn_boot.released(boot, now)) toggleRecording();
    return;
  }

  bool k1 = (ports & (1u << EXIO_KEY1)) == 0;
  bool k2 = (ports & (1u << EXIO_KEY2)) == 0;
  bool k3 = (ports & (1u << EXIO_KEY3)) == 0;

  if (btn_boot.released(boot, now) || btn_k1.released(k1, now)) {
    toggleRecording();
  }

  if (btn_k2.released(k2, now)) showStatus();
  if (btn_k3.released(k3, now)) toggleAmp();
}

void updateLeds() {
  uint32_t now = millis();

  if (rec.isRecording()) {
    leds.setMode(LedMode::Recording);
  } else if (!rec.sdReady()) {
    leds.setMode(LedMode::Error);
  } else if (battery.available() && battery.critical()) {
    leds.setMode(LedMode::Critical);
  } else if (battery.available() && battery.low()) {
    leds.setMode(LedMode::LowBattery);
  } else {
    leds.setMode(static_cast<bool>(Serial) ? LedMode::IdleUsb
                                      : LedMode::IdleBattery);
  }

  leds.setLevel(rec.level());

  // No ocioso, mostra a barra de bateria a cada 15 s.
  static uint32_t last_bat = 0;
  bool idle = !rec.isRecording() && rec.sdReady() && battery.available();
  if (idle && now - last_bat > 15000) {
    last_bat = now;
    leds.showBattery((uint8_t)battery.percent());
  }

  leds.update(now);
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
}  // namespace

void setup() {
  // CDCOnBoot inicia TinyUSB antes de setup(). Desconecta ate que o audio
  // registre seus eventos; assim o host nao pode abrir um fluxo cedo demais.
  tud_disconnect();
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== ESP32-S3-AUDIO-Board | gravador WAV ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

  if (exio.begin(TCA9555_ADDR)) {
    Serial.println("[IO] TCA9555 ok");
  } else {
    Serial.println("[IO] TCA9555 NAO respondeu (0x20)");
  }

  leds.begin();
  leds.bootAnimation();

  battery.begin();
  rtc.begin();
  rec.setNameProvider(provideFileName);
  exio.write(EXIO_SD_D3, true);  // libera D3/CS do cartao
  delay(10);
  rec.beginSd();

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN,
              PIN_I2S_MCLK);
  i2s.setTimeout(100);
  if (i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO)) {
    Serial.println("[I2S] ok 16 kHz / 16 bits / estereo");
  } else {
    Serial.println("[I2S] falha ao iniciar");
  }

  if (codec.begin(ES7210_ADDR)) {
    bool ok = codec.configure(AUDIO_SAMPLE_RATE, 256, mic_gain, true);
    codec.setVolumeDb(mic_vol_db);
    Serial.printf("[MIC] ES7210 %s (erros=%u)\n", ok ? "configurado" : "falhou",
                  codec.errorCount());
  } else {
    Serial.println("[MIC] ES7210 nao respondeu (0x40)");
  }

  // Saida de audio (DAC + bips). Precisa vir depois do i2s.begin().
  beeper.setAmpControl(ampForBeep);
  if (beeper.begin(&i2s)) {
    beeper.setVolume(100);
    Serial.println("[OUT] ES8311 ok (bips ativos)");
  } else {
    Serial.println("[OUT] ES8311 nao respondeu (0x18)");
  }

  // Speakerphone: amplificador habilitado por padrao; K3 permite desligar.
  amp_on = true;
  exio.write(EXIO_PA_CTRL, true);

  // Microfone USB (UAC1) + console CDC no mesmo dispositivo.
  if (!rec.beginCaptureQueue()) Serial.println("[REC] falha ao criar fila de audio");
  usbmic.setCaptureTap(recorderTap);
  if (usbmic.begin(&i2s)) {
    Serial.println("[USB] microfone UAC pronto (16 kHz/16 bits/mono)");
  } else {
    Serial.println("[USB] falha ao criar as tasks do UAC");
  }

  // Diagnostico de memoria e criacao AFE; nao processa o audio nesta versao.
  afeProbeBegin();

  // Reenumera somente depois de inicializar a captura e os callbacks de audio.
  tud_connect();

  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

  Serial.println();
  printInfo();
  printHelp();

  beeper.play(BeepEvent::Boot);
}

void loop() {
  pollSerial();
  pollButtons();
  battery.update(millis());
  beeper.update();

  if (rec.isRecording() && battery.available() && battery.critical()) {
    Serial.println("[BAT] bateria critica: parando gravacao");
    stopRecording();
  }

  updateLeds();
  delay(5);
}
