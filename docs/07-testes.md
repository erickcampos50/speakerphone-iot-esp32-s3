# 07 — Diário de testes

Registro cronológico do que foi medido na bancada. Números reais, não estimativas.

## 2026-10-28 — Teste 1 (primeira versão do firmware)

**Firmware:** 1.0.0 · 480.008 bytes · 25.520 bytes de RAM

| Verificação | Resultado |
|---|---|
| Boot | OK — ESP32-S3 rev 2, 16 MB flash, 8 MB PSRAM, heap 321.536 B |
| TCA9555 | OK |
| ES7210 | OK, ganho 30 dB |
| SD | montado, 7,68 GB, SDHC, 2147 min livres |
| RTC | hora inválida (OS flag setada) |
| Bateria | indisponível nos dois pinos |
| 1ª gravação | **8.192 bytes em 8 s** ❌ |
| 2ª gravação | 606.208 bytes em 9,5 s ✅ (taxa correta) |
| Áudio | RMS 65 / pico 373 (−38,9 dBFS) |

### Sintomas estranhos no log

`[STATUS]` e `[AMP] ligado` apareceram sozinhos, sem ninguém tocar nos botões.

### Diagnóstico

O `Debouncer` inicializava `_raw = _stable = true` e disparava um evento falso
30 ms depois da primeira leitura. No boot isso acionou BOOT (inicia gravação) e
K1 (para gravação) em sequência → o arquivo fechou com **um único bloco de
8 KB**. K2 e K3 geraram o status e o amp "fantasmas".

---

## 2026-10-28 — Teste 2 (correção do debounce + diagnóstico)

**Firmware:** 1.0.1 · 482.120 bytes

| Verificação | Resultado |
|---|---|
| Disparos falsos no boot | ✅ eliminados |
| Gravação de 5 s | 409.600 bytes em 6,4 s |
| Taxa média | **64.524 B/s** (esperado 64.000) ✅ |
| Leituras curtas de I²S | **0** ✅ |
| Cabeçalho WAV | 16000 Hz, 2 ch, 16 bits, tamanho correto ✅ |
| `time set` + nome por RTC | `2026-10-28_213003.wav` ✅ |
| `adc` | GPIO1 = 2976 mV, GPIO8 = 2991 mV → divisor ausente |

### Nível de áudio medido (sala silenciosa)

```
 1s  L: RMS=  99 pico= 535 | R: RMS=  98 pico= 636
 2s  L: RMS= 108 pico= 462 | R: RMS= 105 pico= 409
 3s  L: RMS=  84 pico= 323 | R: RMS=  83 pico= 297
 4s  L: RMS= 145 pico= 723 | R: RMS= 140 pico= 745   ← algum som real
 5s  L: RMS=  76 pico= 315 | R: RMS=  76 pico= 314
 6s  L: RMS=  74 pico= 326 | R: RMS=  72 pico= 317
canal 0: RMS=99.7  pico=723 (-33.1 dBFS)  DC=-0.3
canal 1: RMS=97.3  pico=745 (-32.9 dBFS)  DC=-0.3
```

**Interpretação:** os microfones captam (há variação entre segundos e os canais
são independentes), mas o nível é baixo para voz. DC próximo de zero indica ADC
saudável. Ainda falta o teste com fala/palma para definir o ganho final.

---

## Bugs corrigidos

| # | Bug | Causa | Correção |
|---|---|---|---|
| 1 | 1ª gravação com 8.192 bytes | debounce disparando no boot | estado inicial adotado como referência; ação na soltura |
| 2 | `[STATUS]`/`[AMP]` espontâneos | mesmo bug | idem |
| 3 | ganho exibido como "45 dB" | fórmula `15 + g*3` errada | tabela `kGainDb[]` do enum do ES7210 |
| 4 | `readPort()` confundia erro de I²C com "tudo pressionado" | retorno 0 em falha | retorna −1 em erro |

---

## Pendências de teste

- [ ] Nível do microfone com fala e palmas (`level 12`)
- [ ] Definir ganho final (PGA × volume digital)
- [ ] Botões físicos BOOT/K1/K2/K3
- [ ] Observação visual de cada estado do anel
- [ ] Rotação de 60 min (pode ser reduzida temporariamente)
- [ ] Gravação longa (> 1 h) para checar estabilidade do SD
- [ ] Microfone USB no Windows/Zoom
- [ ] Página web: listar, baixar, apagar
- [ ] Bips: cada sequência
- [ ] Consumo e autonomia na bateria

## 2026-10-29 — Compilação parcial da integração

- Firmware experimental com USB e bips: compilou em modo TinyUSB/CDC,
  560.576 bytes de flash e 75.136 bytes de RAM estática.
- Sketch isolado do servidor web: compilou, 1.079.707 bytes de flash e
  81.956 bytes de RAM estática. O servidor ainda não foi integrado.
- Não houve gravação na placa nem validação de áudio, WiFi ou bips nesta etapa.
- Bloqueios para gravação da versão integrada: revisão da troca de dono do I2S,
  encerramento do gravador por erro, controle do buffer USB e integração e
  teste do servidor web. Compilação não comprova funcionamento simultâneo.

### Refatoração da captura compartilhada

- Uma única tarefa lê o I2S; entrega áudio ao USB e, quando ativa, à fila
  limitada do gravador. A fila cheia encerra a gravação e registra a perda.
- O alimentador USB mantém dados parciais em buffer local, sem recuar a fila.
- `level` fica temporariamente indisponível, para não disputar o I2S.
- Compilação com TinyUSB/CDC e `--warnings all`: 560.372 bytes de flash,
  75.136 bytes de RAM estática; a fila é alocada dinamicamente no boot.
- Não foi gravado nem testado na placa; servidor web ainda não integrado.
  Validar perdas, inicialização, início/parada, encerramento por fila cheia,
  desconexão USB e arquivos WAV antes de considerar a integração concluída.

### Gravação da versão parcial na placa

- Compilação: 560.372 bytes de flash; 75.136 bytes de RAM estática.
- Upload em COM3 concluído com hashes verificados; após reiniciar o console
  enumerou em COM5 e respondeu aos comandos `info` e `usb`.
- `info`: SD montado, ES7210 detectado, bateria indisponível e RTC inválido.
- Windows identificou duas interfaces `TinyUSB UAC1`, ambas com código de erro
  10 (dispositivo não pode iniciar). O microfone USB ainda não está funcional
  no Windows; causa exata não confirmada. O diagnóstico `usb` também mostrou
  desconectado/stream não, e precisa de revisão.
- Não foi iniciada nenhuma gravação de áudio neste teste. Bips, WAV e captura
  simultânea ainda precisam de validação. WiFi e servidor web não integrados.

### Investigação do erro USB

- Windows 10 22H2 build 19045.6466: interfaces de áudio MI_02 e MI_03 foram
  enumeradas separadamente, ambas com erro 10; CDC em COM5 funciona.
- A Microsoft documenta falhas UAC 1.0 com erro 10 que podem afetar Windows 10
  22H2: https://learn.microsoft.com/en-us/windows/release-health/status-windows-11-25h2
  A aplicabilidade a esta instalação e a causa exata não estão comprovadas.
- Criado `firmware/usb_descriptor.cpp` experimental com IAD para agrupar as
  interfaces de áudio e identificação USB composta; compilou (560.576 bytes).
- Tentativa de upload via COM5 falhou: `No serial data received`. O binário
  experimental não foi gravado; placa precisa entrar manualmente no bootloader.
- Após entrada manual no bootloader, upload experimental em COM3 concluído e
  hashes verificados. Após reset automático, Windows ainda mostra dispositivo
  USB JTAG/serial de bootloader em COM3; inicialização do firmware e detecção
  de áudio ainda não verificadas. Necessário reconectar sem pressionar BOOT.
- Após reconexão sem BOOT, Windows enumerou `USB\VID_303A&PID_0002&MI_02`
  com serviço `usbaudio`, sem erro (código 0), e criou o dispositivo de entrada
  `Microfone (ESP32S3_DEV)`. Console CDC em COM6 responde; SD e ES7210 detectados.
  A mudança no agrupamento das interfaces resolveu a detecção neste PC; não
  comprova, por si só, qualidade ou funcionamento do fluxo de áudio.
- O comando `usb` ainda mostrou `desconectado` com o dispositivo enumerado;
  o indicador de conexão precisa de correção. Fluxo e áudio simultâneo com
  gravação ainda não foram testados.
- Teste WASAPI da primeira versão com IAD: dispositivo abriu, mas nenhum frame
  chegou e o console indicou `stream=nao`. Causa ainda não identificada.
- Versão experimental com endpoint isócrono ADAPTIVE compilou e foi gravada
  em COM3 com hashes verificados. Após reset, a placa permaneceu no bootloader;
  necessário reconectar sem BOOT para testar o fluxo. Não há resultado ainda.
- Após reconexão: USB reconhecido sem erro, console em COM6. WASAPI compartilhado
  e MME abriram mas não entregaram frames em 5 s; WASAPI exclusivo entregou
  79.360 frames em cerca de 5 s, `stream=sim`, sem quedas relatadas pelo ring.
  Em teste separado exclusivo, 47.680 frames em cerca de 3 s com pico 358/32767;
  não houve teste controlado de fala, portanto não comprova captação de voz.
  `underruns=4000` conta tentativas com buffer vazio, não necessariamente
  pacotes perdidos; precisa de métrica apropriada. `desconectado` é indicação
  incorreta do firmware. A barra e aplicações comuns ainda não foram validadas.
- Investigação posterior, após nova reconexão, na mesma sessão e sem gravar
  áudio em arquivo: WASAPI compartilhado antes do exclusivo entregou 0 frames;
  exclusivo entregou 55.680 frames (pico 6028); compartilhado depois do
  exclusivo entregou 55.520 frames (pico 1126). Durante o primeiro teste o
  firmware indicou `stream=nao`, e nos dois seguintes `stream=sim`. Isso
  reproduz o padrão relatado pelo usuário: abrir em exclusivo parece ativar
  a captura compartilhada até a desconexão. Causa raiz ainda não confirmada.
- Usuário confirmou que o microfone voltou a funcionar após essa intervenção.
  Driver local `usbaudio.sys` versão 10.0.19041.5794 (14/05/2025); versão
  registrada para investigação, sem conclusão sobre responsabilidade do driver.
- Hipótese: TinyUSB é iniciado antes de `setup()`, mas o callback do evento de
  ativação de áudio só era registrado mais tarde, após outras inicializações;
  um evento antecipado poderia ser perdido. Firmware experimental agora
  desconecta USB no início de `setup()` e reconecta após registrar callbacks e
  iniciar captura. `usb` consulta `tud_mounted()` para o estado de conexão.
  Compilou (560.612 bytes); hipótese ainda não confirmada em hardware, alteração
  ainda não gravada na placa. Testar compartilhado logo após reconexões, sem
  abertura exclusiva, e também após reinicialização e em outro computador.
- Posteriormente, o binário `build-usb-start` foi gravado em COM3 com hashes
  verificados. Após o reset automático, a placa permaneceu no bootloader;
  aguardando reconexão sem BOOT para testar a hipótese, sem usar modo exclusivo.
- Após reconectar sem BOOT, usuário confirmou que o microfone começou a
  funcionar imediatamente, sem a intervenção em modo exclusivo. Resultado
  positivo para esta reconexão; ainda faltam testes repetidos, em reunião,
  com gravação simultânea e em outros computadores.

## Speakerphone e interface — continuidade

- Usuário confirmou detecção USB de entrada e saída, reprodução e, na versão
  com DAC a 60%, controle de volume pelo Windows. Com DAC a 100%, relatou boa
  audibilidade, mas distorção acima de aproximadamente 50% no Windows.
- AFE para comunicação foi criado no hardware (feed 256 amostras/canal,
  dois canais, fetch 512; heap após criação 89.640 bytes), mas não conectado
  ao fluxo. Referência analógica do ES8311 para MIC3 do ES7210 aparece no
  esquemático. Diagnóstico de slots teve uma falha inicial de enumeração;
  variante com mais pilha inicializou, mas tom de teste não foi ouvido e o
  mapeamento não foi comprovado. Investigação AEC/NS suspensa pelo usuário.
- Versão `lab/speakerphone-ui/firmware` compilou com 565.720 bytes de flash e
  75.416 bytes de RAM estática e foi gravada com hashes verificados. Ainda não
  há teste confirmado de boot normal, botões, LEDs agrupados, bips ou chamada.
- Não confundir essa versão com o firmware `firmware/` ou com os protótipos AEC.
