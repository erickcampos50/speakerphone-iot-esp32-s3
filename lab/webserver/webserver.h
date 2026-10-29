#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Servidor HTTP do gravador de audio.
//
// - Sobe sempre um AP ABERTO (sem senha) com SSID "ESP32-MIC" (192.168.4.1).
// - Se houver credenciais na NVS (Preferences, namespace "wifi", chaves
//   "ssid"/"pass"), tenta tambem o modo STA por no maximo 8 s, sem bloquear
//   o boot e sem reiniciar se falhar.
// - mDNS "esp32mic.local" e publicado quando o STA conecta.
// - Nao ha autenticacao: quem estiver na rede pode baixar e apagar arquivos.
//
// O objeto nao monta o SD: usa o SD_MMC que o firmware ja montou e trata o
// cartao ausente como "indisponivel" (as rotas respondem, nada trava).
//
// Nota: este modulo NAO usa a classe WebServer do core. No Windows o nome
// webserver.h colide com <WebServer.h> (sistema de arquivos insensivel a
// maiusculas) e o include quebraria. O HTTP aqui e feito sobre WiFiServer.
class FileServer {
 public:
  // Callbacks do gravador. Podem ser nulos: nesse caso /rec responde 503.
  void setRecorderCallbacks(bool (*isRecording)(), void (*start)(),
                            void (*stop)());

  // Callbacks da bateria. Podem ser nulos: /status devolve battery_ok=false.
  void setBatteryCallback(float (*volts)(), bool (*available)());

  void begin();   // WiFi + rotas + HTTP (retorna rapido)
  void update();  // chamar no loop: atende HTTP e cuida do STA/mDNS

  bool apActive() const { return _apActive; }
  String ip() const;
  uint32_t requests() const { return _requests; }

 private:
  void serve(NetworkClient &c);
  void pollSta();
  void startMdns();

  bool recording() const;
  bool sdReady();
  bool pathFor(const char *name, char *out, size_t len);
  uint32_t countFiles();

  void handleRoot(NetworkClient &c);
  void handleStatus(NetworkClient &c);
  void handleList(NetworkClient &c);
  void handleDownload(NetworkClient &c, const char *query);
  void handleDelete(NetworkClient &c, const char *query);
  void handleRec(NetworkClient &c, const char *query);

  WiFiServer _srv{80, 4};

  bool (*_isRec)() = nullptr;
  void (*_recStart)() = nullptr;
  void (*_recStop)() = nullptr;

  float (*_battVolts)() = nullptr;
  bool (*_battOk)() = nullptr;

  bool _started = false;
  bool _apActive = false;
  bool _staPending = false;
  bool _mdnsStarted = false;
  uint32_t _staDeadline = 0;
  uint32_t _requests = 0;
};
