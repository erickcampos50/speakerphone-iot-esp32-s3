# Speakerphone — interface de teste

Cópia experimental baseada no speakerphone USB anterior; a versão anterior
permanece em `lab/speakerphone/firmware`. Não contém AEC nem redução de ruído.

## Botões

- K1: volume local −10%; K2: volume local +10%, limite 0–100%.
- K3: desliga/liga o amplificador, sem alterar o volume do Windows.
- BOOT: inicia/para gravação experimental no cartão SD.

O volume local e o volume do Windows são controles independentes e ambos
afetam a saída. O local começa em 100% e não é persistido após reiniciar.
O DAC também está em 100%: começar o teste com volume baixo no Windows.

## Anel de LEDs (índices lógicos 0–6)

- 0–1 azul: nível do som do computador após volume local; não mede pressão
  acústica. 2–3 verde/amarelo: pico do primeiro canal do microfone.
- 4 azul: USB conectado; âmbar: não conectado.
- 5 verde: fluxo USB do microfone ativo; âmbar: fluxo inativo.
- 6 ciano: amplificador ligado; âmbar: desligado; vermelho piscante: ES7210
  não detectado. Posição física dos índices precisa ser confirmada na placa.

## Sinais sonoros

Um tom de boot, um tom curto nos ajustes de volume e ao religar o amplificador;
tom distinto ao alcançar 100%. A sinalização é misturada ao áudio do computador
pela única task I2S TX, com saturação limitada. No mudo do amplificador ou em
volume local zero não se promete bip audível. A implementação anterior de bips
de gravação e outros eventos ainda não foi reintegrada; não utilizar este
protótipo para validar a gravação. Bips podem ser captados pelo microfone.

## Build e testes

Arduino ESP32 3.3.12, `USBMode=default,CDCOnBoot=cdc`, 16 MB/PSRAM OPI.
Compilação: 565.720 bytes de flash e 75.416 de RAM estática, com `--warnings all`.
Ainda não testada em placa: validar detecção, volume e mudo, posição e resposta
dos LEDs, bips durante reprodução e captura, reconexão e estabilidade em chamada.
Gravada em COM3 com hashes verificados; aguardando reconexão sem BOOT e testes.
