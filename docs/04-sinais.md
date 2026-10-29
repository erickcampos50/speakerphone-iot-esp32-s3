# 04 — Sinais de estado (LEDs e bips)

> **Histórico:** o esquema abaixo não é o da última versão gravada. A UI nova
> separa LEDs em 2 saída / 2 microfone / 3 diagnóstico e reintegra apenas parte
> dos bips; veja `00-estado-atual.md` e `lab/speakerphone-ui/README.md`.

A placa comunica o estado por dois canais independentes: o **anel de LEDs** e o
**alto-falante** (bips). A ideia é que você saiba o que está acontecendo sem
precisar olhar para o computador.

## Anel de LEDs

7 × WS2812B no GPIO38, brilho limitado a 48/255 para economizar bateria.
O anel inteiro assume o estado (não há LED dedicado a uma função).

### Estados (por prioridade)

| # | Estado | Padrão | Cor |
|---|---|---|---|
| 1 | **Gravando** | VU meter nos 7 LEDs + LED 0 piscando a 2 Hz | verde → amarelo → vermelho |
| 2 | **Erro de SD** | 3 piscadas curtas + pausa, repetindo a cada 2 s | vermelho |
| 3 | **Bateria crítica** (< 5%) | pulso lento (~1,5 s) | vermelho |
| 4 | **Bateria baixa** (< 15%) | pulso rápido (~1,2 s) | laranja |
| 5 | **Ocioso na USB** | respiração lenta (~3 s) | azul |
| 6 | **Ocioso na bateria** | respiração lenta (~3 s) | verde |

### Sobreposições temporárias

| Evento | Duração | Padrão |
|---|---|---|
| Confirmação (amp ligado/desligado) | 150 ms | flash branco |
| Status pelo K2 | 4 s | 2 s barra de bateria + 2 s barra de espaço livre (ciano) |
| Nível da bateria (ocioso) | 2 s a cada 15 s | barra de 7 LEDs |

Cores da barra de bateria: **verde** > 50%, **laranja** 20–50%, **vermelho** < 20%.

### VU meter durante a gravação

O nível vem do pico do canal esquerdo a cada bloco de 8 KB (~128 ms), convertido
para uma escala de −60 dBFS a 0 dBFS. LEDs 0–3 são verdes, 4–6 vão para amarelo
e vermelho. O LED 0 pisca em vermelho para indicar "gravação ativa".

> Enquanto o BAT_ADC não estiver soldado, os estados 3, 4 e a barra periódica
> ficam inertes — o firmware detecta a ausência e usa apenas azul/verde.

## Bips (alto-falante)

Saída pelo ES8311 → amplificador NS4150B (EXIO8). Tons senoidais de 16 kHz,
com *fade* de 5 ms para não estalar. Volume padrão 60%.

| Evento | Sequência | Significado |
|---|---|---|
| **Boot** | 3 notas subindo: 523 → 659 → 784 Hz, 90 ms cada | "liguei e estou pronto" |
| **Início da gravação** | 2 bips agudos (1046 Hz, 70 ms) | "estou gravando" |
| **Fim da gravação** | 1 bip grave (392 Hz, 160 ms) | "parei" |
| **Bateria baixa** | 3 bips graves (330 Hz, 90 ms) | "carregue" |
| **Erro** | varredura descendente 880 → 330 Hz, 400 ms | "algo falhou" |
| **WiFi pronto** | 2 bips agudos (1318 Hz, 60 ms) | "a página web está no ar" |
| **Amp ligado** | bip curto 880 Hz, 40 ms | confirmação |
| **Amp desligado** | bip curto 440 Hz, 40 ms | confirmação |

### Regras

- Bips **nunca** tocam durante a gravação, exceto o de início (antes de abrir o
  arquivo) e o de fim (depois de fechar). Isso evita que o som entre no arquivo.
- Se o amplificador estiver desligado, o bip liga o amp, toca e desliga de novo.
- Se o ES8311 não responder, os bips são silenciosamente ignorados (o resto
  do firmware continua funcionando).
