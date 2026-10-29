// Servidor HTTP + WiFi do gravador de audio (ESP32-S3).
//
// HTTP feito direto sobre WiFiServer (sem a classe WebServer do core, cujo
// cabecalho <WebServer.h> colidiria com este webserver.h no Windows).
//
// Preocupacoes de memoria: a pagina HTML mora em PROGMEM e e enviada em
// pedacos (chunked), nunca como um String com a pagina toda. O download le o
// arquivo em blocos de 4 KB e escreve direto no socket.

#include "webserver.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char *kApSsid = "ESP32-MIC";
constexpr const char *kHostname = "esp32mic";
constexpr const char *kAudioDir = "/AUDIO";
constexpr uint32_t kStaTimeoutMs = 8000;  // tentativa maxima de STA
constexpr size_t kCopyChunk = 4096;       // bloco do download

// Prazos do atendimento HTTP (mantem o update() curto).
constexpr uint32_t kFirstByteMs = 250;  // espera o pedido comecar a chegar
constexpr uint32_t kRequestMs = 1000;   // prazo total para ler o pedido

// ---------------------------------------------------------------------------
// Pagina HTML (fragmentos em flash)
// ---------------------------------------------------------------------------
const char kPageHead[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang='pt-BR'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32-MIC - Gravador</title>
<style>
:root{--bg:#111418;--card:#1a1f26;--line:#2a323d;--fg:#e6edf3;--dim:#8b98a6;
--acc:#2f81f7;--ok:#3fb950;--bad:#f85149;--warn:#d29922}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:15px/1.45 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
main{max-width:900px;margin:0 auto;padding:18px 14px 40px}
h1{font-size:20px;margin:0}
.sub{color:var(--dim);font-size:13px;margin:2px 0 18px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));
gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
padding:10px 12px;min-width:0}
.lbl{display:block;color:var(--dim);font-size:11px;text-transform:uppercase;
letter-spacing:.06em}
.val{display:block;font-size:17px;font-weight:600;margin-top:3px;
word-break:break-all}
.rec{color:var(--bad)}
.ok{color:var(--ok)}
.bar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:16px 0}
button{font:inherit;font-weight:600;border-radius:8px;border:1px solid var(--line);
background:var(--card);color:var(--fg);padding:10px 16px;cursor:pointer}
button:hover{border-color:var(--acc)}
button:disabled{opacity:.5;cursor:default}
button.on{background:var(--bad);border-color:var(--bad);color:#fff}
button.off{background:var(--acc);border-color:var(--acc);color:#fff}
.msg{color:var(--dim);font-size:13px}
table{width:100%;border-collapse:collapse;margin-top:6px;font-size:14px}
th,td{text-align:left;padding:8px 6px;border-bottom:1px solid var(--line)}
th{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
td.n{font-variant-numeric:tabular-nums;white-space:nowrap;color:var(--dim)}
td.a{white-space:nowrap;text-align:right}
a{color:var(--acc);text-decoration:none}
a:hover{text-decoration:underline}
.del{background:none;border:1px solid var(--line);color:var(--bad);
padding:5px 10px;font-size:13px;margin-left:10px}
.warn{background:#2a2313;border:1px solid var(--warn);color:var(--warn);
padding:10px 12px;border-radius:8px;margin:14px 0}
.empty{color:var(--dim);padding:14px 6px}
@media(max-width:520px){th.hn,td.n{display:none}}
</style>
</head>
<body>
<main>
<h1>ESP32-MIC</h1>
<div class='sub'>Gravador de áudio &middot; cartão SD</div>
<div class='cards'>
)HTML";

const char kSdWarn[] PROGMEM =
    "<div id='sdwarn' class='warn'>Cartão indisponível — a lista de arquivos "
    "não está acessível. Verifique o cartão SD.</div>";

const char kSdWarnHidden[] PROGMEM =
    "<div id='sdwarn' class='warn' hidden>Cartão indisponível.</div>";

const char kPageMid[] PROGMEM = R"HTML(</div>
<div class='bar'>
<button id='btn-rec' class='off'>Iniciar gravação</button>
<span id='msg' class='msg'></span>
</div>
<table>
<thead><tr><th>Arquivo</th><th class='hn'>Tamanho</th>
<th style='text-align:right'>Ações</th></tr></thead>
<tbody id='files'>
)HTML";

const char kPageTail[] PROGMEM = R"HTML(</tbody>
</table>
</main>
<script>
(function(){
  var busy=false, offline=false, sig='', recState=false, lastCount=-1;
  function $(id){ return document.getElementById(id); }
  function msg(t){ $('msg').textContent = t || ''; }
  function fmtSize(b){
    if(b >= 1048576) return (b/1048576).toFixed(1).replace('.',',')+' MB';
    return Math.max(1, Math.round(b/1024))+' KB';
  }
  function fmtUp(s){
    var d=Math.floor(s/86400), h=Math.floor(s%86400/3600),
        m=Math.floor(s%3600/60), x=s%60;
    if(d>0) return d+'d '+h+'h';
    if(h>0) return h+'h '+m+'min';
    if(m>0) return m+'min '+x+'s';
    return x+'s';
  }
  function aplicar(j){
    recState = !!j.recording;
    var b=$('btn-rec');
    b.textContent = recState ? 'Parar gravação' : 'Iniciar gravação';
    b.className = recState ? 'on' : 'off';
    var e=$('st-rec');
    e.textContent = recState ? 'Gravando' : 'Parado';
    e.className = 'val' + (recState ? ' rec' : ' ok');
    $('st-bat').textContent = j.battery_ok
      ? j.battery_v.toFixed(2).replace('.',',')+' V' : 'n/d';
    $('st-free').textContent = (j.total_mb > 0)
      ? j.free_mb+' / '+j.total_mb+' MB' : 'n/d';
    $('st-up').textContent = fmtUp(j.uptime_s);
    $('sdwarn').hidden = j.total_mb > 0;
  }
  function render(files){
    var s = JSON.stringify(files);
    if(s === sig) return;
    sig = s;
    var tb = $('files');
    while(tb.firstChild) tb.removeChild(tb.firstChild);
    if(!files || !files.length){
      var tr0=document.createElement('tr'), td0=document.createElement('td');
      td0.className='empty'; td0.colSpan=3;
      td0.textContent='Nenhuma gravação em /AUDIO.';
      tr0.appendChild(td0); tb.appendChild(tr0);
      return;
    }
    for(var i=0;i<files.length;i++){
      var f=files[i];
      var tr=document.createElement('tr');
      var c1=document.createElement('td'); c1.textContent=f.name;
      var c2=document.createElement('td'); c2.className='n';
      c2.textContent=fmtSize(f.size);
      var c3=document.createElement('td'); c3.className='a';
      var a=document.createElement('a');
      a.href='/download?f='+encodeURIComponent(f.name);
      a.textContent='baixar';
      var d=document.createElement('button');
      d.className='del'; d.textContent='apagar';
      d.setAttribute('data-f', f.name);
      d.onclick=function(ev){ apagar(ev.target.getAttribute('data-f')); };
      c3.appendChild(a); c3.appendChild(d);
      tr.appendChild(c1); tr.appendChild(c2); tr.appendChild(c3);
      tb.appendChild(tr);
    }
  }
  function apagar(n){
    if(!n) return;
    if(!confirm('Apagar ' + n + '?')) return;
    msg('apagando...');
    fetch('/delete?f='+encodeURIComponent(n), {method:'POST'})
      .then(function(r){
        msg(r.ok ? 'arquivo apagado' : 'falha ao apagar ('+r.status+')');
        sig=''; lastCount=-1; atualizar(true);
      })
      .catch(function(){ msg('falha ao apagar'); });
  }
  function gravar(on){
    var b=$('btn-rec');
    b.disabled=true;
    fetch('/rec?on='+(on?1:0), {method:'POST'})
      .then(function(r){ return r.json(); })
      .then(function(j){
        aplicar(j); sig=''; lastCount=-1; atualizar(true);
      })
      .catch(function(){ msg('falha ao comandar a gravação'); })
      .then(function(){ b.disabled=false; });
  }
  function atualizar(force){
    if(busy) return;
    busy=true;
    fetch('/status', {cache:'no-store'})
      .then(function(r){ return r.json(); })
      .then(function(j){
        if(offline){ offline=false; msg(''); }
        aplicar(j);
        var precisa = force || (j.files !== lastCount);
        lastCount = j.files;
        if(!precisa) return null;
        return fetch('/list', {cache:'no-store'})
          .then(function(r){ return r.json(); })
          .then(render);
      })
      .catch(function(){
        if(!offline){ offline=true; msg('sem conexão com o gravador'); }
      })
      .then(function(){ busy=false; });
  }
  $('btn-rec').onclick = function(){ gravar(!recState); };
  atualizar(true);
  setInterval(function(){ atualizar(false); }, 3000);
})();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// Utilitarios de formato
// ---------------------------------------------------------------------------
void formatSize(char *out, size_t len, uint64_t bytes) {
  if (bytes >= 1048576ULL) {
    snprintf(out, len, "%.1f MB", (double)bytes / 1048576.0);
  } else {
    snprintf(out, len, "%.1f KB", (double)bytes / 1024.0);
  }
  for (char *p = out; *p != 0; p++) {
    if (*p == '.') *p = ',';
  }
}

void formatUptime(char *out, size_t len, uint32_t s) {
  uint32_t d = s / 86400;
  uint32_t h = (s % 86400) / 3600;
  uint32_t m = (s % 3600) / 60;
  if (d > 0) {
    snprintf(out, len, "%lud %luh", (unsigned long)d, (unsigned long)h);
  } else if (h > 0) {
    snprintf(out, len, "%luh %lumin", (unsigned long)h, (unsigned long)m);
  } else if (m > 0) {
    snprintf(out, len, "%lumin %lus", (unsigned long)m,
             (unsigned long)(s % 60));
  } else {
    snprintf(out, len, "%lus", (unsigned long)s);
  }
}

// Escapa o minimo para HTML (&, <, >, ", ').
void htmlEscape(const char *in, char *out, size_t len) {
  size_t o = 0;
  for (size_t i = 0; in[i] != 0 && o + 7 < len; i++) {
    const char *rep = nullptr;
    switch (in[i]) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
      default: break;
    }
    if (rep != nullptr) {
      size_t n = strlen(rep);
      memcpy(out + o, rep, n);
      o += n;
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = 0;
}

// Escapa aspas e barras invertidas para JSON (nomes vindos do cartao).
void jsonEscape(const char *in, char *out, size_t len) {
  size_t o = 0;
  for (size_t i = 0; in[i] != 0 && o + 3 < len; i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if ((unsigned char)c < 0x20) {
      out[o++] = ' ';
    } else {
      out[o++] = c;
    }
  }
  out[o] = 0;
}

// Percent-encoding para usar em href/query.
void urlEncode(const char *in, char *out, size_t len) {
  static const char *hex = "0123456789ABCDEF";
  size_t o = 0;
  for (size_t i = 0; in[i] != 0 && o + 4 < len; i++) {
    unsigned char c = (unsigned char)in[i];
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~';
    if (safe) {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0F];
    }
  }
  out[o] = 0;
}

const char *baseName(const char *path) {
  const char *slash = strrchr(path, '/');
  return (slash != nullptr) ? slash + 1 : path;
}

// Copia com limite de tamanho, sem o aviso de truncamento do snprintf.
void copyStr(char *dst, size_t len, const char *src) {
  size_t n = strlen(src);
  if (n >= len) n = len - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

const char *reasonFor(int code) {
  switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "OK";
  }
}

// ---------------------------------------------------------------------------
// Camada HTTP minima
// ---------------------------------------------------------------------------
// Le uma linha terminada em \n (sem o \r). false se estourar prazo/buffer.
bool readLine(NetworkClient &c, char *out, size_t len, uint32_t t0,
              uint32_t budget) {
  size_t n = 0;
  while (true) {
    if (millis() - t0 > budget) return false;
    int ch = c.read();
    if (ch < 0) {
      if (!c.connected()) return false;
      delay(1);
      continue;
    }
    if (ch == '\n') {
      out[n] = 0;
      if (n > 0 && out[n - 1] == '\r') out[n - 1] = 0;
      return true;
    }
    if (n + 1 < len) out[n++] = (char)ch;
  }
}

// Le a primeira linha e os cabecalhos; devolve metodo, caminho e query.
bool readRequest(NetworkClient &c, char *method, size_t methodLen, char *path,
                 size_t pathLen, char *query, size_t queryLen) {
  uint32_t t0 = millis();

  // O accept() pode acontecer antes do pedido chegar.
  while (!c.available()) {
    if (!c.connected() || millis() - t0 > kFirstByteMs) return false;
    delay(1);
  }

  char line[384];
  if (!readLine(c, line, sizeof(line), t0, kRequestMs)) return false;

  // "GET /caminho?query HTTP/1.1"
  char *sp1 = strchr(line, ' ');
  if (sp1 == nullptr) return false;
  *sp1 = 0;
  copyStr(method, methodLen, line);

  char *target = sp1 + 1;
  char *sp2 = strchr(target, ' ');
  if (sp2 != nullptr) *sp2 = 0;

  char *q = strchr(target, '?');
  if (q != nullptr) {
    *q = 0;
    copyStr(query, queryLen, q + 1);
  } else {
    query[0] = 0;
  }
  copyStr(path, pathLen, target);

  // Cabecalhos: so interessa o tamanho do corpo.
  long bodyLen = 0;
  for (int i = 0; i < 24; i++) {
    if (!readLine(c, line, sizeof(line), t0, kRequestMs)) return false;
    if (line[0] == 0) break;
    if (strncasecmp(line, "Content-Length:", 15) == 0) {
      bodyLen = strtol(line + 15, nullptr, 10);
    }
  }

  // Descarta um corpo pequeno (os POSTs do fetch vao sem corpo).
  if (bodyLen > 0) {
    size_t left = (bodyLen > 1024) ? 1024 : (size_t)bodyLen;
    while (left > 0 && c.connected() && millis() - t0 <= kRequestMs) {
      int ch = c.read();
      if (ch < 0) {
        delay(1);
        continue;
      }
      left--;
    }
  }
  return true;
}

// Extrai "chave=valor" da query, decodificando %XX e '+'.
bool queryParam(const char *query, const char *key, char *out, size_t len) {
  size_t klen = strlen(key);
  const char *p = query;
  while (p != nullptr && *p != 0) {
    const char *amp = strchr(p, '&');
    const char *eq = strchr(p, '=');
    if (eq != nullptr && (amp == nullptr || eq < amp) &&
        (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
      const char *v = eq + 1;
      const char *end = (amp != nullptr) ? amp : v + strlen(v);
      size_t o = 0;
      for (const char *s = v; s < end && o + 1 < len; s++) {
        if (*s == '+') {
          out[o++] = ' ';
        } else if (*s == '%' && s + 2 < end && isxdigit((int)s[1]) &&
                   isxdigit((int)s[2])) {
          char hex[3] = {s[1], s[2], 0};
          out[o++] = (char)strtol(hex, nullptr, 16);
          s += 2;
        } else {
          out[o++] = *s;
        }
      }
      out[o] = 0;
      return true;
    }
    p = (amp != nullptr) ? amp + 1 : nullptr;
  }
  return false;
}

void sendText(NetworkClient &c, int code, const char *type, const char *body) {
  char hdr[256];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Cache-Control: no-store\r\n"
                   "Connection: close\r\n\r\n",
                   code, reasonFor(code), type,
                   (unsigned)strlen(body != nullptr ? body : ""));
  c.write((const uint8_t *)hdr, n);
  if (body != nullptr && body[0] != 0) {
    c.write((const uint8_t *)body, strlen(body));
  }
}

void chunkBegin(NetworkClient &c, int code, const char *type) {
  char hdr[256];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "Cache-Control: no-store\r\n"
                   "Connection: close\r\n\r\n",
                   code, reasonFor(code), type);
  c.write((const uint8_t *)hdr, n);
}

void sendChunk(NetworkClient &c, const char *data, size_t len) {
  if (len == 0) return;
  char sz[12];
  int n = snprintf(sz, sizeof(sz), "%x\r\n", (unsigned)len);
  c.write((const uint8_t *)sz, n);
  c.write((const uint8_t *)data, len);
  c.write((const uint8_t *)"\r\n", 2);
}

// Envia um trecho que esta em flash (PROGMEM): no ESP32 o ponteiro ja e
// legivel, entao basta copiar para o socket.
void sendChunkFlash(NetworkClient &c, const char *pgm, size_t len) {
  sendChunk(c, pgm, len);
}

void chunkEnd(NetworkClient &c) {
  c.write((const uint8_t *)"0\r\n\r\n", 5);
}

}  // namespace

// ---------------------------------------------------------------------------
// Configuracao
// ---------------------------------------------------------------------------
void FileServer::setRecorderCallbacks(bool (*isRecording)(), void (*start)(),
                                      void (*stop)()) {
  _isRec = isRecording;
  _recStart = start;
  _recStop = stop;
}

void FileServer::setBatteryCallback(float (*volts)(), bool (*available)()) {
  _battVolts = volts;
  _battOk = available;
}

bool FileServer::recording() const { return (_isRec != nullptr) && _isRec(); }

// O firmware monta o SD_MMC em recorder.cpp. totalBytes() devolve 0 quando o
// cartao nao esta montado (sem log), entao serve como sonda barata e segura.
bool FileServer::sdReady() { return SD_MMC.totalBytes() > 0; }

String FileServer::ip() const {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (_apActive) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

// Aceita somente nome simples dentro de /AUDIO (sem barras nem "..").
bool FileServer::pathFor(const char *name, char *out, size_t len) {
  size_t n = strlen(name);
  if (n == 0 || n > 64) return false;
  for (size_t i = 0; i < n; i++) {
    char ch = name[i];
    if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
        ch == '"' || ch == '<' || ch == '>' || ch == '|') {
      return false;
    }
    if ((unsigned char)ch < 0x20) return false;
  }
  if (strstr(name, "..") != nullptr) return false;
  snprintf(out, len, "%s/%s", kAudioDir, name);
  return true;
}

uint32_t FileServer::countFiles() {
  if (!sdReady()) return 0;
  File dir = SD_MMC.open(kAudioDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }
  uint32_t n = 0;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) n++;
    f = dir.openNextFile();
  }
  dir.close();
  return n;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void FileServer::begin() {
  if (_started) return;

  // Nao grava a configuracao de volta na NVS a cada WiFi.begin().
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  _apActive = WiFi.softAP(kApSsid);  // aberto: sem senha
  if (_apActive) {
    Serial.printf("[WEB] AP aberto \"%s\" em http://%s/\n", kApSsid,
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[WEB] falha ao subir o AP");
  }

  Preferences prefs;
  prefs.begin("wifi", true);  // somente leitura
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length() > 0) {
    Serial.printf("[WEB] tentando STA \"%s\" (max %lu s)\n", ssid.c_str(),
                  (unsigned long)(kStaTimeoutMs / 1000));
    WiFi.begin(ssid.c_str(), pass.c_str());
    _staPending = true;
    _staDeadline = millis() + kStaTimeoutMs;
  } else {
    Serial.println("[WEB] sem credenciais STA na NVS (so AP)");
  }

  _srv.setNoDelay(true);
  _srv.begin();
  _started = true;
  Serial.println("[WEB] HTTP na porta 80");
}

void FileServer::startMdns() {
  if (_mdnsStarted) return;
  if (!MDNS.begin(kHostname)) {
    Serial.println("[WEB] mDNS indisponivel");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  _mdnsStarted = true;
  Serial.printf("[WEB] mDNS: http://%s.local/\n", kHostname);
}

// Chamado no update(): nao bloqueia, so verifica prazos.
void FileServer::pollSta() {
  if (_staPending) {
    if (WiFi.status() == WL_CONNECTED) {
      _staPending = false;
      Serial.printf("[WEB] STA conectado: %s\n",
                    WiFi.localIP().toString().c_str());
      startMdns();
      return;
    }
    // Comparacao em int32 cobre o estouro do millis().
    if ((int32_t)(millis() - _staDeadline) >= 0) {
      _staPending = false;
      WiFi.disconnect(false, false);  // desiste: o AP continua no ar
      Serial.println("[WEB] STA nao conectou em 8 s; seguindo so com o AP");
      return;
    }
  }
  // Reconexao posterior (queda do roteador) tambem publica o mDNS.
  if (!_mdnsStarted && WiFi.status() == WL_CONNECTED) startMdns();
}

void FileServer::update() {
  if (!_started) return;
  pollSta();

  NetworkClient c = _srv.accept();
  if (!c) return;
  serve(c);
  c.stop();
}

// ---------------------------------------------------------------------------
// Despacho
// ---------------------------------------------------------------------------
void FileServer::serve(NetworkClient &c) {
  char method[8];
  char path[96];
  char query[192];
  if (!readRequest(c, method, sizeof(method), path, sizeof(path), query,
                   sizeof(query))) {
    return;  // cliente sumiu ou pedido invalido: fecha sem responder
  }
  _requests++;

  bool get = (strcmp(method, "GET") == 0);
  bool post = (strcmp(method, "POST") == 0);

  if (get && strcmp(path, "/") == 0) {
    handleRoot(c);
  } else if (get && strcmp(path, "/status") == 0) {
    handleStatus(c);
  } else if (get && strcmp(path, "/list") == 0) {
    handleList(c);
  } else if (get && strcmp(path, "/download") == 0) {
    handleDownload(c, query);
  } else if (post && strcmp(path, "/delete") == 0) {
    handleDelete(c, query);
  } else if (post && strcmp(path, "/rec") == 0) {
    handleRec(c, query);
  } else {
    sendText(c, 404, "text/plain; charset=utf-8",
             "404 - rota nao encontrada");
  }
  c.flush();
}

// ---------------------------------------------------------------------------
// GET /
// ---------------------------------------------------------------------------
void FileServer::handleRoot(NetworkClient &c) {
  bool sd = sdReady();
  bool rec = recording();

  uint32_t totalMb = 0;
  uint32_t freeMb = 0;
  if (sd) {
    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    totalMb = (uint32_t)(total / 1048576ULL);
    freeMb = (uint32_t)((total - used) / 1048576ULL);
  }

  char bat[24];
  if (_battOk != nullptr && _battVolts != nullptr && _battOk()) {
    snprintf(bat, sizeof(bat), "%.2f V", _battVolts());
    for (char *p = bat; *p != 0; p++) {
      if (*p == '.') *p = ',';
    }
  } else {
    snprintf(bat, sizeof(bat), "n/d");
  }

  char freeStr[48];
  if (sd) {
    snprintf(freeStr, sizeof(freeStr), "%lu / %lu MB", (unsigned long)freeMb,
             (unsigned long)totalMb);
  } else {
    snprintf(freeStr, sizeof(freeStr), "n/d");
  }

  char upStr[32];
  formatUptime(upStr, sizeof(upStr), millis() / 1000);

  char ipStr[24];
  copyStr(ipStr, sizeof(ipStr), ip().c_str());

  chunkBegin(c, 200, "text/html; charset=utf-8");
  sendChunkFlash(c, kPageHead, sizeof(kPageHead) - 1);

  // Cards com os valores atuais (o JS atualiza a cada 3 s).
  {
    char buf[224];
    snprintf(buf, sizeof(buf),
             "<div class='card'><span class='lbl'>Estado</span>"
             "<span class='val%s' id='st-rec'>%s</span></div>",
             rec ? " rec" : " ok", rec ? "Gravando" : "Parado");
    sendChunk(c, buf, strlen(buf));

    snprintf(buf, sizeof(buf),
             "<div class='card'><span class='lbl'>Bateria</span>"
             "<span class='val' id='st-bat'>%s</span></div>",
             bat);
    sendChunk(c, buf, strlen(buf));

    snprintf(buf, sizeof(buf),
             "<div class='card'><span class='lbl'>Espaço livre</span>"
             "<span class='val' id='st-free'>%s</span></div>",
             freeStr);
    sendChunk(c, buf, strlen(buf));

    snprintf(buf, sizeof(buf),
             "<div class='card'><span class='lbl'>IP</span>"
             "<span class='val' id='st-ip'>%s</span></div>",
             ipStr);
    sendChunk(c, buf, strlen(buf));

    snprintf(buf, sizeof(buf),
             "<div class='card'><span class='lbl'>Ligado há</span>"
             "<span class='val' id='st-up'>%s</span></div>",
             upStr);
    sendChunk(c, buf, strlen(buf));
  }

  sendChunkFlash(c, sd ? kSdWarnHidden : kSdWarn,
                 sd ? sizeof(kSdWarnHidden) - 1 : sizeof(kSdWarn) - 1);
  sendChunkFlash(c, kPageMid, sizeof(kPageMid) - 1);

  // Linhas da tabela de /AUDIO.
  uint32_t n = 0;
  if (sd) {
    File dir = SD_MMC.open(kAudioDir);
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          const char *name = baseName(f.name());
          char esc[128];
          char url[160];
          char size[24];
          char row[640];
          htmlEscape(name, esc, sizeof(esc));
          urlEncode(name, url, sizeof(url));
          formatSize(size, sizeof(size), (uint64_t)f.size());
          snprintf(row, sizeof(row),
                   "<tr><td>%s</td><td class='n'>%s</td>"
                   "<td class='a'><a href='/download?f=%s'>baixar</a>"
                   "<button class='del' data-f='%s'>apagar</button></td></tr>",
                   esc, size, url, esc);
          sendChunk(c, row, strlen(row));
          n++;
        }
        f = dir.openNextFile();
      }
    }
    if (dir) dir.close();
  }

  if (n == 0) {
    const char *empty =
        sd ? "<tr><td class='empty' colspan='3'>"
             "Nenhuma gravação em /AUDIO.</td></tr>"
           : "<tr><td class='empty' colspan='3'>"
             "Cartão indisponível — nada para listar.</td></tr>";
    sendChunk(c, empty, strlen(empty));
  }

  sendChunkFlash(c, kPageTail, sizeof(kPageTail) - 1);
  chunkEnd(c);
}

// ---------------------------------------------------------------------------
// GET /status
// ---------------------------------------------------------------------------
void FileServer::handleStatus(NetworkClient &c) {
  bool sd = sdReady();
  uint32_t totalMb = 0;
  uint32_t freeMb = 0;
  if (sd) {
    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    totalMb = (uint32_t)(total / 1048576ULL);
    freeMb = (uint32_t)((total - used) / 1048576ULL);
  }

  bool batOk = (_battOk != nullptr) && _battOk();
  float volts = (batOk && _battVolts != nullptr) ? _battVolts() : 0.0f;

  char buf[224];
  snprintf(buf, sizeof(buf),
           "{\"recording\":%s,\"battery_v\":%.2f,\"battery_ok\":%s,"
           "\"free_mb\":%lu,\"total_mb\":%lu,\"uptime_s\":%lu,\"files\":%lu}",
           recording() ? "true" : "false", volts, batOk ? "true" : "false",
           (unsigned long)freeMb, (unsigned long)totalMb,
           (unsigned long)(millis() / 1000),
           (unsigned long)countFiles());
  sendText(c, 200, "application/json", buf);
}

// ---------------------------------------------------------------------------
// GET /list
// ---------------------------------------------------------------------------
void FileServer::handleList(NetworkClient &c) {
  chunkBegin(c, 200, "application/json");
  sendChunk(c, "[", 1);

  bool first = true;
  if (sdReady()) {
    File dir = SD_MMC.open(kAudioDir);
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          const char *name = baseName(f.name());
          char esc[192];
          char item[256];
          jsonEscape(name, esc, sizeof(esc));
          snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"size\":%lu}",
                   first ? "" : ",", esc, (unsigned long)f.size());
          sendChunk(c, item, strlen(item));
          first = false;
        }
        f = dir.openNextFile();
      }
    }
    if (dir) dir.close();
  }

  sendChunk(c, "]", 1);
  chunkEnd(c);
}

// ---------------------------------------------------------------------------
// GET /download?f=NOME
// ---------------------------------------------------------------------------
void FileServer::handleDownload(NetworkClient &c, const char *query) {
  char name[80];
  char path[96];
  if (!queryParam(query, "f", name, sizeof(name)) ||
      !pathFor(name, path, sizeof(path))) {
    sendText(c, 400, "text/plain; charset=utf-8", "nome invalido");
    return;
  }
  if (!sdReady()) {
    sendText(c, 503, "text/plain; charset=utf-8", "cartao indisponivel");
    return;
  }

  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sendText(c, 404, "text/plain; charset=utf-8", "arquivo nao encontrado");
    return;
  }

  uint8_t *buf = (uint8_t *)malloc(kCopyChunk);
  if (buf == nullptr) {
    f.close();
    sendText(c, 503, "text/plain; charset=utf-8", "sem memoria");
    return;
  }

  if (recording()) {
    Serial.println("[WEB] download durante a gravacao (pode cair a taxa)");
  }

  size_t total = (size_t)f.size();
  char hdr[384];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/octet-stream\r\n"
                   "Content-Disposition: attachment; filename=\"%s\"\r\n"
                   "Content-Length: %lu\r\n"
                   "Cache-Control: no-store\r\n"
                   "Connection: close\r\n\r\n",
                   name, (unsigned long)total);
  c.write((const uint8_t *)hdr, n);

  size_t sent = 0;
  while (sent < total && c.connected()) {
    size_t r = f.read(buf, kCopyChunk);
    if (r == 0) break;
    size_t w = c.write(buf, r);
    if (w == 0) break;
    sent += w;
    delay(0);  // cede o CPU entre blocos (nao dorme)
  }

  free(buf);
  f.close();
  Serial.printf("[WEB] download %s (%lu de %lu bytes)\n", path,
                (unsigned long)sent, (unsigned long)total);
}

// ---------------------------------------------------------------------------
// POST /delete?f=NOME
// ---------------------------------------------------------------------------
void FileServer::handleDelete(NetworkClient &c, const char *query) {
  char name[80];
  char path[96];
  if (!queryParam(query, "f", name, sizeof(name)) ||
      !pathFor(name, path, sizeof(path))) {
    sendText(c, 400, "application/json", "{\"ok\":false,\"error\":\"nome\"}");
    return;
  }
  if (!sdReady()) {
    sendText(c, 503, "application/json",
             "{\"ok\":false,\"error\":\"cartao\"}");
    return;
  }
  // Apagar o arquivo aberto pela gravacao destruiria o audio em curso.
  if (recording()) {
    sendText(c, 409, "application/json",
             "{\"ok\":false,\"error\":\"gravando\"}");
    return;
  }
  if (!SD_MMC.exists(path)) {
    sendText(c, 404, "application/json",
             "{\"ok\":false,\"error\":\"nao encontrado\"}");
    return;
  }
  if (!SD_MMC.remove(path)) {
    sendText(c, 500, "application/json",
             "{\"ok\":false,\"error\":\"falha\"}");
    return;
  }
  sendText(c, 200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// POST /rec?on=1 | on=0
// ---------------------------------------------------------------------------
void FileServer::handleRec(NetworkClient &c, const char *query) {
  char on[8];
  if (!queryParam(query, "on", on, sizeof(on)) ||
      (strcmp(on, "0") != 0 && strcmp(on, "1") != 0)) {
    sendText(c, 400, "application/json",
             "{\"ok\":false,\"error\":\"use on=0 ou on=1\"}");
    return;
  }
  if (_isRec == nullptr) {
    sendText(c, 503, "application/json",
             "{\"ok\":false,\"error\":\"sem gravador\"}");
    return;
  }

  if (strcmp(on, "1") == 0) {
    if (!_isRec() && _recStart != nullptr) _recStart();
  } else {
    if (_isRec() && _recStop != nullptr) _recStop();
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"recording\":%s}",
           recording() ? "true" : "false");
  sendText(c, 200, "application/json", buf);
}
