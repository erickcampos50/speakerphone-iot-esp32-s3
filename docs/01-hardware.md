# 01 — Hardware

> Referência de pinagem histórica; consulte `00-estado-atual.md` para limites e
> comportamento atual. Detecção de sessão USB não identifica fonte de energia.

## Placa

**Waveshare ESP32-S3-AUDIO-Board** (revisão 1.1).

| Item | Valor |
|---|---|
| SoC | ESP32-S3 (QFN56), rev v0.2, 2 núcleos Xtensa LX7 @ 240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB octal (OPI) |
| MAC | `28:84:85:B2:B1:64` |
| USB | USB-Serial/JTAG nativo (VID `303A` / PID `1001`) — `COM3` no Windows |
| Áudio | 2 microfones analógicos + ES7210 (ADC) + ES8311 (DAC) + NS4150B (amplificador) |

## Pinagem

### I²C — compartilhado por todos os periféricos

| Sinal | GPIO |
|---|---|
| SDA | 11 |
| SCL | 10 |
| Frequência | 100 kHz |

| Dispositivo | Endereço | Função |
|---|---|---|
| ES7210 | `0x40` | ADC dos microfones (MIC1/MIC2) |
| ES8311 | `0x18` | DAC / saída de áudio |
| TCA9555 | `0x20` | Expansor de 16 IOs |
| PCF85063 | `0x51` | RTC com bateria de backup |

### I²S — barramento único, full-duplex

| Sinal | GPIO | Direção |
|---|---|---|
| MCLK | 12 | ESP32 → codecs |
| BCLK | 13 | ESP32 → codecs |
| LRCK | 14 | ESP32 → codecs |
| DIN | 15 | ES7210 → ESP32 (microfone) |
| DOUT | 16 | ESP32 → ES8311 (alto-falante) |

Formato: **16 kHz, 16 bits, estéreo**, MCLK = 256 × fs = 4,096 MHz.
O ES7210 opera em modo **TDM 1×FS** com 16 bits (mesma configuração do demo
oficial da Waveshare).

### Expansor TCA9555 (EXIO)

| EXIO | Uso | Estado |
|---|---|---|
| 3 | D3/CS do cartão SD | **alto** (libera o cartão) |
| 7 | Roteamento do USB (GPIO19/20) | **alto** (mantém o USB nativo) |
| 8 | PA_CTRL — amplificador NS4150B | alto liga o alto-falante |
| 9 | Tecla K1 | entrada, pull-up 10 K |
| 10 | Tecla K2 | entrada, pull-up 10 K |
| 11 | Tecla K3 | entrada, pull-up 10 K |

Teclas são **ativas em nível baixo** (R7, R44 e R77 de 10 K para 3V3).

### Cartão SD (SDMMC 1-bit)

| Sinal | GPIO |
|---|---|
| CLK | 40 |
| CMD | 42 |
| D0 | 41 |

### LEDs e botões

| Item | GPIO |
|---|---|
| Anel de 7 × WS2812B | 38 |
| Botão BOOT | 0 (pull-up interno, ativo em nível baixo) |

## Bateria (⚠️ pendência de hardware)

O divisor é **R2 = 200 K / R11 = 100 K** com C10/C11 de 100 nF, ou seja
`V_adc = V_bat / 3`. O nó `BAT_ADC` chega ao **GPIO1** somente se o resistor
**R78 (0R)** estiver soldado. De fábrica ele vem **aberto**, e o GPIO1 ainda é
compartilhado com `CAM_HREF`.

Medições feitas na placa (2026-10-28), com bateria conectada:

| Pino | Leitura no pino | Tensão calculada |
|---|---|---|
| GPIO1 | 2976 mV | 9,01 V (inválido) |
| GPIO8 | 2991 mV | 9,06 V (inválido) |

Os dois pinos estão **flutuando em ~3 V** (saturação do ADC), o que confirma
que nenhum divisor está ligado. Consequências enquanto o R78 não for soldado:

- `bat` responde "medição indisponível";
- não há barra de bateria, aviso de bateria baixa nem parada por bateria crítica;
- a distinção USB × bateria continua funcionando, via `Serial.isConnected()`.

> O demo oficial da Waveshare lê `BAT_ADC_PIN 8`, mas o esquemático desta
> revisão liga o nó em GPIO1. O firmware testa os dois pinos no boot e usa o
> que apresentar tensão plausível (2,8 V a 4,6 V após o divisor).

## Memória e partições

| Região | Endereço | Tamanho |
|---|---|---|
| nvs | `0x009000` | 24 KB |
| otadata | `0x00E000` | 8 KB |
| app0 | `0x010000` | 3 MB |
| app1 | `0x310000` | 3 MB |
| ffat | `0x610000` | ~9,4 MB |
| coredump | `0xFF0000` | 64 KB |

O sketch atual ocupa ~482 KB (15% da app). Há folga confortável para WiFi,
servidor HTTP e USB.

## Consumo e autonomia (estimativa)

Ainda não medida. Para referência, gravar PCM estéreo 16 kHz/16-bit consome
**64 KB/s ≈ 230 MB/h**, o que dá cerca de **33 h** de gravação no cartão de
7,68 GB livres.
