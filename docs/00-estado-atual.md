# Estado atual e retomada

Este documento prevalece sobre descrições antigas de comportamento planejado.
Projeto experimental para Waveshare ESP32-S3-AUDIO-Board, Arduino-ESP32 3.3.12.

## Versões e o que está na placa

| Caminho | Estado |
|---|---|
| `lab/speakerphone-ui/firmware` | Última versão gravada, hash verificado. Entrada/saída USB, LEDs agrupados e controles locais implementados; **não houve confirmação de inicialização nem teste dos novos controles após esse upload**. |
| `lab/speakerphone/firmware` | Base anterior de speakerphone; usuário confirmou reprodução, microfone e controle de volume Windows. Binário `build-speakerphone-max` foi testado com DAC a 100%. |
| `firmware/` | Integração experimental anterior de gravador + microfone USB. **Não é a versão atual de speakerphone** e não deve ser tomada como versão final. |
| `lab/aec-probe`, `lab/aec-speakerphone`, `lab/aec-slots` | Pesquisas e diagnósticos de AEC; suspensos por escolha do usuário. Não são firmware de reunião. |
| `lab/webserver` | Protótipo isolado; compila, não integrado nem validado na placa. |

As pastas `lab/` são snapshots completos deliberados, não necessariamente
sincronizados. A versão de UI não foi promovida para `firmware/`. A string de
versão interna `1.0.0` não identifica esses snapshots de forma confiável.

## Situação funcional

- **USB:** Windows reconheceu entrada e saída na base de speakerphone. O usuário
  confirmou áudio e controle de volume; a versão UI posterior precisa ser testada.
- **Gravação:** WAV 16 kHz/16-bit estéreo funcionou em firmware anterior. A
  implementação compartilhada posterior e o uso simultâneo não foram validados;
  gravação é opcional, sem início automático por USB, e não é prioridade atual.
- **AEC/NS:** não estão ativos no áudio. Existe circuito analógico de referência
  no esquemático; AFE criou objeto em teste, mas o mapeamento I²S e o fluxo
  processado não foram validados. Investigação suspensa. Aplicativos podem ter
  seu próprio cancelamento, sem garantia de resultado.
- **Bateria:** medição indisponível nesta placa, R78 não instalado. Conexão ou
  sessão USB não é uma medição confiável de alimentação.
- **WiFi/web:** não integrado nem ativo. AP aberto e rotas são propostas de
  laboratório, não recursos da versão gravada.
- **Bluetooth:** ESP32-S3 não oferece Bluetooth Classic HFP/HSP para uso como
  microfone de reunião. Não foi implementado.

## UI experimental gravada

K1 diminui volume local, K2 aumenta, K3 liga/desliga amplificador, BOOT controla
gravação experimental. Volume local e Windows são independentes; o local inicia
em 100% e o DAC em 100%. Começar com volume Windows baixo; o usuário relatou
distorção em níveis altos. LEDs lógicos 0–1 saída, 2–3 entrada, 4 USB, 5 fluxo
mic e 6 amp/erro. Posição física e resposta ainda não foram confirmadas. Há tons
de boot e confirmação limitados, misturados na saída; outros bips não foram
reintegrados. Detalhes e ressalvas: `lab/speakerphone-ui/README.md`.

## Compilar e gravar

Requer `arduino-cli`, core `esp32:esp32@3.3.12` e `Adafruit NeoPixel`.
No ambiente original o CLI fica no diretório temporário aprovado. Ajuste o
caminho e a porta à instalação local:

```powershell
$cli = "$env:LOCALAPPDATA\Temp\opencode\esp32s3-audio\arduino-cli\arduino-cli.exe"
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=cdc,UploadSpeed=921600'
$sketch = 'lab/speakerphone-ui/firmware'
& $cli compile --warnings all --fqbn $fqbn $sketch
& $cli upload -p COM3 --fqbn $fqbn $sketch
```

Com TinyUSB a placa pode precisar de bootloader manual: desligar USB, segurar
BOOT ao conectar, soltar após alguns segundos; confirmar a porta com
`arduino-cli board list`. Após upload, pode ser necessário reconectar **sem**
BOOT. A porta CDC pode mudar. Não executar upload enquanto uma gravação está
aberta. Não se deve sobrescrever a tabela de partições ou formatar o SD como
medida de diagnóstico sem entender o impacto e ter backup.

## Próximos testes

1. Iniciar a versão UI e validar USB, botões, LEDs, bips e volume baixo.
2. Testar uma chamada real, reconexão e estabilidade; não presumir ausência de eco.
3. Quando autorizado, retomar AEC/NS em laboratório e validar referência,
   sincronização, CPU e qualidade antes de integrá-lo.
4. Retomar gravação, website e calibração de volume em etapas posteriores.

O dump da flash original foi registrado anteriormente fora do projeto em uma
pasta temporária local; não é versionado nem garantia de backup durável.
