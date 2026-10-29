# ESP32-S3 como microfone USB (UAC1) + CDC serial — viabilidade

**Veredito: VIÁVEL, sem recompilar o core.** O core `esp32:esp32@3.3.12` já traz
`USBAudioCard` (UAC1) e ele compõe com o CDC do `Serial` sem conflito de endpoints.
Nada em `firmware/` foi alterado; nada foi gravado na placa (apenas compilação).

- FQBN usado: `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600`
- Formato: 16 kHz / 16 bit / mono, UAC1 (full-speed — o S3 não tem high-speed, logo o core escolhe UAC1 sozinho).

---

## 1. Por que funciona

| Fato | Evidência |
|---|---|
| TinyUSB com classe de áudio habilitada na lib pré-compilada | `...\tools\esp32s3-libs\3.3.12\sdkconfig`: `CONFIG_TINYUSB_AUDIO_ENABLED=y`; `CFG_TUD_AUDIO` herda dele |
| Driver de áudio realmente registrado | `objdump -r -j .rodata._usbd_driver usbd.c.obj` → relocs em `0x70`/`0x74` = `audiod_init`/`audiod_deinit` |
| Símbolos presentes em `libarduino_tinyusb.a` | `audio_device.c.obj`: `audiod_*`, `tud_audio_n_available/write/set_ep_in_fifo_threshold` |
| Core escolhe UAC1 no S3 | `USBAudioCard.cpp:125-187` (`#if TUD_OPT_HIGH_SPEED` … `#else` → descritores `TUD_AUDIO10_*`); ramo mic-only em `:175-186` |
| Descritor de configuração é montado por callback fraco | `esp32-hal-tinyusb.c:419` (`tud_descriptor_configuration_cb` weak), montagem em `:743-781` |
| Endpoints não colidem | CDC pega OUT 3 / IN 4 / IN 5 (`esp32-hal-tinyusb.c:892-897`); áudio fica com IN `0x81`. `CFG_TUD_NUM_EPS=6`, `NUM_IN_EPS=5` |
| Exemplo oficial confirma o caminho | `libraries\USB\examples\AudioCard\AudioCard.ino` |

**Ponto crítico:** a instância `USBAudioCard` precisa ser **global**. Com
`CDCOnBoot=cdc`, `main.cpp:99-110` chama `Serial.begin()` + `USB.begin()` dentro de
`app_main()`; depois disso `tinyusb_enable_interface()` falha e o descritor não
sai. Construtor global roda no static-init, antes de `app_main()`. Ver
`usb-uac.ino:53`.

---

## 2. Duas variantes

### A — `usb-uac/usb-uac.ino` (recomendada)
`USBAudioCard uac(16000, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);`
- `producerTask` (1 kHz) gera senoide 440 Hz → ring buffer SPSC 8192 amostras (512 ms) → `feederTask` → `uac.write()` → FIFO TinyUSB (3072 B) → EP `0x81`.
- `tud_audio_n_set_ep_in_fifo_threshold(0, 256)` no `INTERFACE_ENABLE` reduz a latência de ~48 ms (padrão `depth/2`) para ~8 ms. Precisa ser reaplicado porque `audiod_set_itf()` reseta o threshold a cada SET_INTERFACE (`audio_device.c:1282`).
- `#include "esp32-hal-tinyusb.h"` + `"class/audio/audio_device.h"` são obrigatórios: `USBAudioCard.h` não puxa `tusb_config.h`, então `tud_audio_n_*` não seria declarado.

### B — `usb-uac-alt-descriptor/usb-uac-alt-descriptor.ino` (fallback)
Sobrescreve o símbolo fraco `tud_descriptor_configuration_cb()` com descritor
CDC+UAC1 montado à mão, declarando o EP isocrono como **ADAPTIVE (0x09)** em vez
de ASYNCHRONOUS (0x05). Usar só se o host recusar a captura na variante A.

---

## 3. Resultado da compilação

| Variante | Flash | RAM global |
|---|---|---|
| A `usb-uac` | 377 616 B (12%) | 72 256 B (22%) |
| B `usb-uac-alt-descriptor` | 373 580 B (11%) | 55 304 B (16%) |

Comando:
```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600" <pasta>
```
Descritor da variante B verificado byte a byte no ELF (`.rodata` @ `0x3c04a417`, 177 B):
`wTotalLength=177`, 4 interfaces, `bEndpointAddress=0x81`, `bmAttributes=0x09`,
`wMaxPacketSize=64`, `tSamFreq=0x003e80` (16000), CS AC `wTotalLength=41`.

---

## 4. Integração no firmware

1. Copiar `usb-uac.ino` para a pasta do sketch de produção (renomear para o nome da pasta, exigência do Arduino).
2. Manter `USBAudioCard` **no escopo global**, antes de qualquer `USB.begin()`.
3. Manter `CDCOnBoot=cdc` e `USBMode=default` no FQBN — é o que dá o composto CDC+UAC.
4. Trocar `producerTask` pela fonte real de áudio: entregar blocos de `kChunkBytes` = 32 B (16 amostras) por ms no ring buffer. O `feederTask` e o ring não mudam.
5. Registrar `onAudioEvent` **depois** de `uac.begin()` (o `onEvent` guarda o handler; a ordem importa só para não perder o primeiro `INTERFACE_ENABLE`).
6. Se o host não abrir o stream, trocar para a variante B (mesma API de escrita: `tud_audio_n_write(0, …)`).

---

## 5. Riscos e limitações

- **Maior risco:** o descritor UAC1 do core declara o EP de mic como ASYNCHRONOUS
  (`USBAudioCardDescriptors.h`, macro `TUD_AUDIO10_MICROPHONE_DESCRIPTOR`) **sem EP
  de feedback**. Hosts Windows podem recusar iniciar a captura. Mitigação = variante B.
- **Tamanho do EP é fixo em 96 B** (`TUD_AUDIO_EP_SIZE(false, 48000, 2, 1)`), mesmo
  pedindo 16 kHz. Funciona (folga de 3x), mas desperdiça banda; `audiod_calc_tx_packet_sz()`
  exige apenas ≥ 34 B para 16 kHz mono.
- **Senoide não é travada ao SOF USB**: drift de alguns ppm é absorvido pelo FIFO de
  3072 B, mas em sessões longas pode estourar. Para áudio real use I2S/ADC com PLL
  derivado do clock USB, ou aceite o FIFO como amortecedor.
- **Somente 16 kHz mono** neste protótipo; outros rates exigem mudar `kSampleRate` e
  reconferir `CFG_TUD_AUDIO_MAX_SAMPLE_RATE` (96000) e o EP size.
- **UAC2 não é possível no S3** por este caminho: exige high-speed, que o S3 não tem.
  UAC2 só com descritor manual em full-speed (fora do escopo) ou `arduino-audio-tools`
  (`USBAudioStream`, `config.begin_usb=false` para compor com CDC).
- **Sem teste em hardware** (gravação proibida). O que está provado é: compila,
  linka, e o descritor no binário está correto.
