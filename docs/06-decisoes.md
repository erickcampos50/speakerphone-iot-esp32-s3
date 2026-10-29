# 06 — Registro de decisões (ADRs)

> Registro histórico: detalhes de implementação e afirmações de funcionamento
> de ADRs antigas podem ter sido superados. `00-estado-atual.md` prevalece.

Cada decisão registra o contexto, a escolha e a consequência. Quando uma decisão
mudar, adicione uma nova entrada em vez de reescrever a antiga.

---

## ADR-001 — Linguagem: Arduino C++

**Contexto:** a placa já vinha com um firmware Arduino-ESP32 (IDF 5.5.5) e a
comunidade tem drivers prontos para ES7210/ES8311.
**Decisão:** usar Arduino C++ com `arduino-cli` e core `esp32:esp32@3.3.12`.
**Consequência:** build simples e reproduzível; acesso ao IDF quando necessário
via componentes pré-compilados. Custo: menos controle fino que IDF puro.

---

## ADR-002 — Formato de gravação: WAV PCM estéreo 16 kHz/16 bits

**Contexto:** alternativas como IMA ADPCM (4:1) e Opus (20:1) foram avaliadas.
**Decisão:** manter PCM estéreo sem compressão (~230 MB/h).
**Consequência:** máxima compatibilidade e zero CPU de codificação; ocupa muito
espaço (33 h no cartão de 7,68 GB). Reavaliar se a autonomia de arquivo virar
problema.

---

## ADR-003 — LEDs: anel inteiro por estado

**Contexto:** havia a opção de reservar um LED para a fonte de energia.
**Decisão:** o anel inteiro representa o estado, com sobreposições temporárias
para bateria e status.
**Consequência:** leitura à distância e estética uniforme; perde-se a indicação
simultânea de "gravando" e "fonte" — a fonte volta a aparecer quando a gravação
termina.

---

## ADR-004 — Botões

**Decisão:** BOOT e K1 iniciam/param; K2 mostra status; K3 liga/desliga o amp.
**Consequência:** dois botões redundantes para gravar (facilita o uso sem olhar).
A ação ocorre na **soltura**, com debounce de 30 ms.

---

## ADR-005 — Rotação de 60 min e nome pelo RTC

**Decisão:** um arquivo por hora, nomeado `AAAA-MM-DD_HHMMSS.wav`, com fallback
`REC_0001.WAV` quando o RTC não tem hora válida.
**Consequência:** arquivos de ~230 MB, abaixo do limite de 4 GB do FAT32, e
ordenação cronológica natural. Depende de o RTC estar acertado.

---

## ADR-006 — Bateria: seguir sem medição por enquanto

**Contexto:** o divisor do BAT_ADC chega ao GPIO1 apenas com o R78 (0R) soldado,
que não vem de fábrica. Medições confirmaram os pinos flutuando.
**Decisão:** seguir sem percentual de bateria; o firmware detecta e avisa.
**Consequência:** sem aviso de bateria baixa. A distinção USB × bateria continua
via `Serial.isConnected()`. Para habilitar: soldar R78 e regravar (nada de
software precisa mudar).

---

## ADR-007 — Bluetooth: descartado como microfone

**Contexto:** o pedido era usar a placa como microfone Bluetooth em reuniões.
**Decisão:** **não implementar**. O ESP32-S3 tem apenas Bluetooth LE 5.0 — sem
Bluetooth Classic (HFP/HSP) e sem LE Audio (canais isócronos).
**Consequência:** um serviço BLE GATT proprietário existiria, mas não apareceria
como microfone no Windows/Zoom/Teams. O caminho funcional é USB (UAC) + WiFi.

---

## ADR-008 — Microfone USB via TinyUSB (UAC + CDC composto)

**Contexto:** a classe de áudio do TinyUSB está compilada na lib do core
(`libarduino_tinyusb.a` contém `tud_audio_*`), mas o core não expõe UAC.
**Decisão:** trocar `USBMode=hwcdc` por `default` (TinyUSB) e montar um
descritor composto **CDC (console) + UAC1 (microfone 16 kHz/16 bits)**.
**Consequência:** a placa aparece como microfone padrão no PC e o console
continua disponível na mesma porta. Custo: descritor próprio e ring buffer de
áudio compartilhado com o gravador.

---

## ADR-009 — WiFi: AP próprio + STA opcional, sem autenticação

**Decisão:** AP aberto `ESP32-MIC` (192.168.4.1) sempre ativo; STA opcional via
NVS; HTTP sem senha.
**Consequência:** acesso garantido em qualquer lugar; sem proteção alguma.
Aceitável apenas em fase de testes (decisão explícita do usuário).

---

## ADR-010 — Bips de estado pelo ES8311

**Decisão:** tons senoidais não bloqueantes, com sequências distintas por evento,
tocando no I2S em full-duplex com a captura.
**Consequência:** feedback sonoro sem olhar para a placa. Custo: precisa do
amplificador ligado (EXIO8) no momento do bip, o que é feito automaticamente.

---

## ADR-011 — Uso de subagentes no desenvolvimento

**Contexto:** o projeto cresceu e a janela de contexto ficou apertada.
**Decisão:** módulos independentes (USB/UAC, ES8311+bips, servidor web) são
desenvolvidos por subagentes em `lab/`, com integração e revisão feitas no
firmware principal.
**Consequência:** mais paralelismo; exige contrato de interface bem definido e
revisão de código na integração.

---

## ADR-012 — Gravação é sempre escolha do usuário; USB não a dispara

**Contexto:** o microfone USB (UAC) e a gravação no cartão podem rodar ao mesmo
tempo. Faltava definir se conectar o USB deveria iniciar a gravação
automaticamente.
**Decisão:** **não**. O microfone USB fica sempre disponível e é independente da
gravação. Gravar é uma ação explícita do usuário, por qualquer um dos três
canais:
- botão **BOOT** ou **K1** (aperta e solta);
- botão **Iniciar gravação** na página web (`POST /rec?on=1`);
- comando `rec` no console serial.

Parar segue a mesma regra (`stop`, `POST /rec?on=0`, BOOT/K1).
**Consequência:**
- dá para usar a placa como microfone de reunião o dia inteiro sem encher o
  cartão;
- a gravação é sempre intencional — não há risco de gravar uma conversa sem
  querer por causa de um cabo conectado;
- o estado `recording` é único e compartilhado: o que for iniciado no botão
  aparece na página web e vice-versa;
- o USB continua funcionando normalmente durante a gravação (os dois consomem
  o mesmo fluxo do ES7210, cada um com seu buffer).
