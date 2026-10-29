# 02 — Arquitetura do firmware

> **Histórico / parcialmente desatualizado.** O diagrama e a descrição de
> gravação abaixo misturam versões anteriores e planos não implementados.
> Não há servidor WiFi integrado; a tarefa de gravação atual não lê o I2S
> diretamente. Para o estado e os caminhos atuais, veja `00-estado-atual.md`.

## Visão geral

```
                    ┌──────────────── ESP32-S3 ────────────────┐
   MIC1 ─┐          │                                          │
   MIC2 ─┴─ ES7210 ─┤ DIN 15 ── I2S RX ──┐                     │
                    │                    │                     │
                    │              task "rec" (core 1, prio 6)  │
                    │                    │  buffer 8 KB         │
                    │                    ▼                     │
                    │              Recorder ── SD_MMC 1-bit ───┼──► /AUDIO/*.wav
                    │                    ▲                     │
                    │              header a cada 5 s           │
                    │                                          │
                    │  I2S TX ──► ES8311 ──► NS4150B ──► alto-falante
                    │      ▲                                   │
                    │   Beeper (bips de estado)                │
                    │                                          │
                    │  USB OTG ──► TinyUSB (UAC mic + CDC)     │
                    │  WiFi    ──► AP + HTTP (arquivos)        │
                    └──────────────────────────────────────────┘
```

## Módulos

| Arquivo | Responsabilidade |
|---|---|
| `firmware.ino` | `setup()`/`loop()`, botões, console serial, máquina de estados, orquestração |
| `board.h` | Toda a pinagem e constantes de áudio |
| `tca9555.*` | Driver do expansor de IO (EXIO) |
| `es7210.*` | Inicialização do ADC dos microfones via I²C |
| `recorder.*` | Task de gravação, WAV, rotação de arquivos, listagem, estatísticas |
| `rtc.*` | PCF85063: leitura, acerto e geração do carimbo de data/hora |
| `battery.*` | Auto-detecção do pino do BAT_ADC e curva de descarga Li-ion |
| `led_ring.*` | Animações do anel de 7 LEDs |
| `es8311.*` | *(novo)* Inicialização do DAC |
| `beeper.*` | *(novo)* Sequências de bips não bloqueantes |
| `webserver.*` | *(novo)* WiFi AP/STA + HTTP para listar e baixar arquivos |
| `usbmic.*` | *(novo)* Interface UAC: a placa aparece como microfone no PC |

## Fluxo de áudio da gravação

1. `setup()` liga o EXIO3 (libera o cartão) e monta o SD em 1-bit a 20 MHz.
2. Configura o I²S em RX (16 kHz/16 bits/estéreo) e inicializa o ES7210.
3. `startRecording()`:
   - desliga o amplificador (evita realimentação acústica);
   - abre `/AUDIO/<carimbo>.wav` (ou `REC_0001.WAV` se o RTC não tiver hora);
   - cria a task `rec` no **core 1**, prioridade 6, com 8 KB de pilha.
4. A task lê blocos de 8 KB (~128 ms) do I²S e grava no SD.
5. A cada 5 s o cabeçalho WAV é reescrito com o tamanho atual — se faltar
   energia, perde-se no máximo 5 s de metadados (o áudio já está no arquivo).
6. A cada 60 min o arquivo é fechado e um novo é aberto com o horário corrente.
7. `stop()` sinaliza a task, espera no máximo 5 s, corrige o cabeçalho e fecha.

### Nome dos arquivos

- RTC válido (flag `OS` do PCF85063 limpa): `2026-10-28_213003.wav`
- RTC inválido ou ausente: `REC_0001.WAV`, `REC_0002.WAV`, ...

Se já existir um arquivo com o mesmo carimbo (dois `rec` no mesmo segundo),
o firmware acrescenta `_1`, `_2`, ...

## Distribuição do áudio do microfone (em integração, não validada)

O mesmo fluxo do ES7210 alimenta **dois consumidores independentes**:

```
I2S RX (16 kHz / 16 bits / estéreo)
   └──► única tarefa de captura
        ├──► buffer USB ──► UAC (quando o host abre o fluxo)
        └──► fila de 64 blocos ──► tarefa de gravação ──► SD (quando ativa)
```

- O **USB não inicia nem para a gravação** (ADR-012). Ele é um caminho de saída
  sempre disponível: plugou, aparece como microfone no Windows.
- A gravação só existe depois de uma ação explícita: BOOT, K1, `POST /rec?on=1`
  ou comando `rec`.
- O objetivo é permitir ambos simultaneamente. A captura é independente da
  escrita no SD; a fila de gravação comporta aproximadamente um segundo. Se
  lotar, a gravação é encerrada e a perda é informada. Concorrência, perdas e
  latência ainda precisam de teste em hardware.
- A captura permanece ativa mesmo sem host USB ou gravação.
- O comando `level` está temporariamente indisponível na versão experimental.

### Estado compartilhado

`recording` é uma variável única do firmware. Botão, página web e console leem
e escrevem a mesma máquina de estados, então:

| Ação | Efeito nos outros canais |
|---|---|
| BOOT inicia | a página web passa a mostrar "gravando" no próximo `/status` |
| Página web para | o LED volta ao estado ocioso e o console mostra `[REC] parado` |
| Console `rec` | idem |

Chamar `rec` com a gravação já ativa (ou `stop` com ela parada) é inofensivo:
o firmware ignora e responde o estado atual.

## Máquina de estados dos LEDs

Prioridade (a primeira que casar vence):

```
Gravando            → VU meter + LED 0 piscando
SD ausente/erro     → 3 piscadas vermelhas
Bateria crítica     → vermelho lento
Bateria baixa       → laranja rápido
USB conectado       → azul respirando
Bateria             → verde respirando
```

Sobreposições (duração curta, têm precedência momentânea):

```
Flash de confirmação (amp)    150 ms branco
Barra de status (K2)          4 s  (2 s bateria + 2 s espaço livre)
Barra de bateria periódica    2 s a cada 15 s no ocioso
```

## Concorrência

| Task | Core | Prioridade | Observação |
|---|---|---|---|
| `loop` (Arduino) | 1 | 1 | console, botões, LEDs |
| `rec` | 1 | 6 | gravação; preempta o loop |
| WiFi/lwIP | 0 | — | criadas pelo core |
| TinyUSB | 0 | — | criada pelo core |

O buffer de 8 KB cobre ~128 ms de áudio. Se o SD travar mais que isso, há
perda de amostras — o `stop()` informa a taxa média e o número de leituras
curtas para detectar isso.

## Tratamento de erros

| Situação | Ação |
|---|---|
| SD não monta | LEDs em erro; `rec` recusa iniciar; a página web continua no ar |
| Falha de escrita no SD | encerra a gravação e sinaliza |
| Cartão cheio (< 2 MB) | encerra a gravação |
| ES7210 não responde | segue gravando (o I²S entrega silêncio) e avisa no console |
| Bateria crítica | encerra a gravação |
