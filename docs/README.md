# ESP32-S3-AUDIO-Board — Gravador e microfone de mesa

> **Leia primeiro [Estado atual e retomada](00-estado-atual.md).** As tabelas e
> comandos abaixo registram uma fase anterior e não descrevem a última versão
> gravada. A UI experimental está em `lab/speakerphone-ui/firmware`, não em
> `firmware/`; WiFi, AEC e NS não estão ativos.

Firmware sob medida para a placa **Waveshare ESP32-S3-AUDIO-Board**, usado como
gravador de áudio e microfone de mesa para reuniões.

## Estado atual

| Recurso | Estado |
|---|---|
| Gravação WAV 16 kHz/16 bits/estéreo no SD | ✅ funcionando |
| LEDs de estado (anel de 7 WS2812B) | ✅ funcionando |
| Botões BOOT / K1 / K2 / K3 | ✅ implementados |
| RTC PCF85063 (nome dos arquivos por data/hora) | ✅ funcionando |
| Console serial (USB CDC) | ✅ funcionando |
| Medição de bateria | ⚠️ bloqueada por hardware (0R do BAT_ADC não soldado) |
| Microfone USB (UAC) | 🚧 em desenvolvimento |
| Página web no WiFi | 🚧 em desenvolvimento |
| Bips de estado pelo alto-falante | 🚧 em desenvolvimento |
| Bluetooth como microfone | ❌ impossível neste chip (ver ADR-007) |

## Índice da documentação

| Documento | Conteúdo |
|---|---|
| [01-hardware.md](01-hardware.md) | Pinagem, periféricos, limitações elétricas |
| [02-arquitetura.md](02-arquitetura.md) | Módulos, fluxo de áudio, máquina de estados |
| [03-console-serial.md](03-console-serial.md) | Todos os comandos do console |
| [04-sinais.md](04-sinais.md) | Linguagem dos LEDs e dos bips |
| [05-rede-e-web.md](05-rede-e-web.md) | WiFi, página web e rotas HTTP |
| [06-decisoes.md](06-decisoes.md) | Registro de decisões (ADRs) |
| [07-testes.md](07-testes.md) | Diário de bancada com números medidos |

## Estrutura de pastas

```
esp32-s3-audio-firmware/
├── docs/                     documentação (este diretório)
├── firmware/                 firmware principal (sketch Arduino)
│   ├── firmware.ino          setup/loop, botões, console, estados
│   ├── board.h               pinagem da placa
│   ├── recorder.*            gravação WAV + SD
│   ├── es7210.*              ADC dos microfones
│   ├── rtc.*                 PCF85063
│   ├── battery.*             leitura do BAT_ADC
│   ├── led_ring.*            animações do anel
│   └── tca9555.*             expansor de IO (EXIO)
├── lab/                      módulos em desenvolvimento (subagentes)
│   ├── usb-uac/              microfone USB
│   ├── es8311-beeper/        DAC + bips
│   └── webserver/            WiFi + HTTP
└── backup/                   (fora do repositório) dump da flash original
```

## Compilação e gravação

```powershell
$cli = "$env:LOCALAPPDATA\Temp\opencode\esp32s3-audio\arduino-cli\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600"

& $cli compile --fqbn $fqbn "D:\Documents\00FERRAMENTAS\esp32-s3-audio-firmware\firmware"
& $cli upload  -p COM3 --fqbn $fqbn "D:\Documents\00FERRAMENTAS\esp32-s3-audio-firmware\firmware"
```

> Este exemplo compila `firmware/`, versão experimental anterior. Para a
> versão UI veja as instruções específicas em `00-estado-atual.md`.

## Backup

O dump completo da flash original (16 MB) está em
`C:\Users\erick\AppData\Local\Temp\opencode\esp32s3-audio\backup\flash_original_16MB.bin`.
Para restaurar:

```powershell
python -m esptool -p COM3 -b 921600 write-flash 0 flash_original_16MB.bin
```
