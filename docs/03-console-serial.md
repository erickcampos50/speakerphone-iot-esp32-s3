# 03 — Console serial

> **Histórico:** este documento descreve o gravador anterior, não a UI de
> speakerphone. A porta pode mudar; `level` está indisponível nos protótipos
> recentes. Consulte `00-estado-atual.md` antes de usar exemplos.

Conexão: **115200 8N1**, porta `COM3` (USB-Serial/JTAG).
Cada comando é uma linha terminada em `\n`.

## Comandos

| Comando | O que faz |
|---|---|
| `help` | Lista os comandos |
| `rec` | Inicia a gravação |
| `stop` | Para a gravação (corrige o cabeçalho e mostra a taxa média) |
| `ls` | Lista os arquivos de `/AUDIO` com tamanho |
| `rm <arquivo>` | Apaga um arquivo (`rm REC_0003.WAV` ou `rm /AUDIO/REC_0003.WAV`) |
| `bat` | Tensão e percentual da bateria (ou aviso de indisponível) |
| `adc` | Leitura crua do BAT_ADC em GPIO1 e GPIO8, em mV |
| `level [s]` | Medidor ao vivo do microfone: RMS e pico a cada 0,5 s (padrão 5 s, máx. 30 s) |
| `sd` | Espaço total, usado, livre e minutos de gravação disponíveis |
| `stats [arquivo]` | Analisa um WAV: RMS, pico e DC por segundo e por canal |
| `gain <0-14>` | Ganho do PGA do ES7210 (0 = 0 dB, 10 = 30 dB, 14 = 37,5 dB) |
| `vol <dB>` | Volume digital do ES7210 (−95 a +32 dB, padrão 0) |
| `amp on\|off` | Liga/desliga o amplificador (EXIO8) |
| `time` | Mostra a hora do RTC |
| `time set AAAA-MM-DD HH:MM:SS` | Acerta o RTC |
| `info` | Resumo do hardware e do estado |
| `reboot` | Reinicia a placa |

## Exemplos

### Conferir o microfone

```
level 8
```

Saída esperada (falando a ~30 cm):

```
[MIC] medindo por 8.0 s... fale perto da placa
  RMS=   842 ( -31.8 dBFS)  pico= 4210
  RMS=  1930 ( -24.6 dBFS)  pico= 9870
```

Silêncio absoluto fica em RMS ≈ 100. Se a fala não passar de RMS ≈ 300,
aumente o ganho:

```
gain 14     # 37,5 dB de PGA (máximo do ES7210)
vol 20      # +20 dB digitais
```

### Acertar o relógio

```
time set 2026-10-29 09:15:00
time
[RTC] 2026-10-29 09:15:02
```

Sem isso (ou sem bateria de backup no RTC) os arquivos caem no padrão
`REC_0001.WAV`.

### Verificar a qualidade do que foi gravado

```
stats
[WAV] /AUDIO/2026-10-29_091502.wav
      16000 Hz, 2 ch, 16 bits, 409600 bytes (6.4 s)
        1s  L: RMS=   842 pico= 4210 | R: RMS=   830 pico= 4102
        2s  L: RMS=  1930 pico= 9870 | R: RMS=  1910 pico= 9650
      analisados 102400 frames (6.4 s)
      canal 0: RMS=1386.2  pico=9870 ( -10.4 dBFS)  DC=-0.3
      canal 1: RMS=1370.5  pico=9650 ( -10.6 dBFS)  DC=-0.3
```

- **RMS** alto e variando entre segundos → tem fala.
- **RMS** constante e baixo (~100) → só ruído de fundo.
- **DC** longe de 0 → problema no ADC (offset).
- **Pico** em 32767 → saturação; reduza o ganho.

### Diagnóstico de bateria

```
adc
[ADC] GPIO1: 2976 mV no pino -> 9.01 V de bateria
[ADC] GPIO8: 2991 mV no pino -> 9.06 V de bateria
```

Leituras próximas de 3 V indicam pino flutuando (divisor desconectado).
O esperado com o divisor ligado é **1,0 a 1,45 V** no pino.

## Códigos de retorno

O console não usa códigos numéricos; cada ação imprime uma linha com prefixo:

| Prefixo | Origem |
|---|---|
| `[IO]` | TCA9555 |
| `[BAT]` | bateria |
| `[ADC]` | leitura crua dos pinos |
| `[SD]` | cartão |
| `[REC]` | gravação |
| `[WAV]` | análise de arquivo |
| `[MIC]` | ES7210 |
| `[AMP]` | amplificador |
| `[RTC]` | relógio |
| `[SYS]` | sistema |
