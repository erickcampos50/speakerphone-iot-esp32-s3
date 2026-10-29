# Operação: LEDs, botões, bipes e console

> **Histórico:** refere-se ao gravador anterior, não à interface experimental
> atual. Veja `00-estado-atual.md` e `lab/speakerphone-ui/README.md`.

## LEDs (anel de 7 × WS2812B, GPIO38)

Brilho limitado a 48/255 para economizar corrente na bateria. O anel inteiro assume
o estado — a prioridade é de cima para baixo:

| Prioridade | Estado | Padrão | Cor |
|---|---|---|---|
| 1 | Boot | varredura + 2 piscadas | verde |
| 2 | Gravando | VU meter nos 7 LEDs + LED 0 piscando a 2 Hz | verde → amarelo → vermelho |
| 3 | Erro de SD | 3 piscadas + pausa (ciclo de 2 s) | vermelho |
| 4 | Bateria crítica (< 5%) | pulso lento (~1,5 s) | vermelho |
| 5 | Bateria baixa (< 15%) | pulso rápido (~1,2 s) | laranja |
| 6 | Ocioso na USB | respiração lenta (~3 s) | azul |
| 7 | Ocioso na bateria | respiração lenta (~3 s) | verde |

Sobreposições (temporárias, têm prioridade sobre o estado):

| Evento | Padrão | Duração |
|---|---|---|
| Barra de bateria periódica (só ocioso) | barra de 7 LEDs: verde >50%, laranja 20–50%, vermelho <20% | 2 s, a cada 15 s |
| K2 — status | bateria (2 s) e depois espaço livre (2 s) | 4 s |
| Confirmação do amplificador | flash branco | 150 ms |

> As animações 4, 5 e a barra periódica dependem do BAT_ADC. Sem o resistor 0R
> soldado elas ficam inertes e o firmware avisa no console.

## Botões

Todos ativos em nível baixo, com debounce de 30 ms. A ação dispara **na soltura**
do botão (evita repetição e toques acidentais).

| Botão | Origem | Ação |
|---|---|---|
| **BOOT** | GPIO0 | Inicia / para a gravação |
| **K1** | EXIO9 (TCA9555) | Inicia / para a gravação |
| **K2** | EXIO10 | Mostra status: barra de bateria + barra de espaço livre (4 s) e imprime no console |
| **K3** | EXIO11 | Liga / desliga o amplificador (EXIO8), com flash branco |

## Bipes (em desenvolvimento)

| Evento | Padrão |
|---|---|
| Boot | 1 bip 880 Hz, 60 ms |
| Início da gravação | 2 bipes 1200 Hz, 70 ms |
| Fim da gravação | 1 bip 600 Hz, 220 ms |
| Erro (SD/codec) | 3 bipes 400 Hz, 90 ms |
| WiFi conectado | 2 bipes ascendentes 900 → 1400 Hz |
| WiFi em modo AP | 1 bip 700 Hz, 150 ms |
| Toque de status (K2) | 1 bip curto 1000 Hz, 30 ms |

## Console serial (115200 8N1)

| Comando | Função |
|---|---|
| `help` | lista os comandos |
| `rec` / `stop` | inicia / para a gravação |
| `ls` | lista `/AUDIO` |
| `rm <arquivo>` | apaga um arquivo |
| `sd` | espaço total/usado/livre e minutos restantes |
| `stats [arquivo]` | analisa o WAV: RMS e pico por segundo, DC, resumo por canal |
| `bat` | tensão e percentual da bateria |
| `adc` | leitura crua do BAT_ADC em GPIO1 e GPIO8 |
| `level [s]` | medidor ao vivo do microfone (RMS/pico a cada 0,5 s) |
| `gain <0-14>` | PGA do ES7210 (0 = 0 dB, 10 = 30 dB, 14 = 37,5 dB) |
| `vol <dB>` | volume digital do ES7210 (−95 a +32 dB) |
| `amp on\|off` | amplificador |
| `time` | lê o RTC |
| `time set AAAA-MM-DD HH:MM:SS` | acerta o RTC |
| `info` | resumo do hardware |
| `reboot` | reinicia |

### Exemplos

```
time set 2026-10-28 21:30:00
gain 14
vol 20
level 10
rec
stats /AUDIO/2026-10-28_213003.wav
```

## Comandos previstos (em desenvolvimento)

| Comando | Função |
|---|---|
| `wifi status` | estado da conexão, IP e SSID |
| `wifi ssid <nome>` / `wifi pass <senha>` | grava credenciais na NVS |
| `wifi connect` | reconecta |
| `wifi forget` | apaga as credenciais e volta ao modo AP |
