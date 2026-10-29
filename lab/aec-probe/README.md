# Investigação ESP-SR: cancelamento de eco e redução de ruído

## Estado

O speakerphone atualmente gravado **não tem AEC nem supressão de ruído**.
O usuário solicitou priorizá-los; calibração de volume e outras ações foram
suspensas. A placa não foi regravada durante esta investigação.

O core Arduino ESP32 3.3.12 instalado inclui `esp_aec.h`, configuração AFE e
bibliotecas ESP-SR. O sketch `aec-probe.ino` compila e liga chamadas do AEC e
do AFE em modo de comunicação com `MR`, NS WebRTC e WakeNet/VAD desativados.
Resultado: 1.759.804 bytes de flash, 69.384 bytes de RAM estática. Esse sketch
**não foi executado**: compilação não prova inicialização, memória disponível,
qualidade nem viabilidade de tempo real. A compilação limpa inicial do wrapper
ESP_SR também emitiu aviso sobre partição para modelos; avaliar necessidade
efetiva para AFE sem modelos e NS WebRTC antes de mudar a tabela de partições.
Não formatar nem sobrescrever NVS/dados sem avaliação.

## Referência do fabricante

O exemplo oficial `ESP-IDF/esp_sr_02/main/hardeware_driver/bsp_board.c`
configura quatro canais e descreve `RMNM`; a alimentação ao AFE passa por
`esp_get_feed_data`. `speech_det_driver/mic_speech.c` usa `AFE_TYPE_SR` e define
`ns_init = false`. Isso é um exemplo de reconhecimento, não demonstra por si
só o speakerphone USB nem prova que a configuração de referência dele seja
equivalente à captura estéreo atual. Verificar o roteamento físico e os slots
TDM antes de tratar qualquer canal como referência válida.

O esquemático oficial de revisão 1.1 identifica um circuito `AEC` ligando
`OUTP`/`OUTN` do ES8311 às entradas `ADC_MIC3_P`/`ADC_MIC3_N` do ES7210.
Portanto há um caminho analógico de referência antes do amplificador; ele
precisa ser identificado e medido nos slots do I2S antes da integração. O
exemplo usa I2S padrão em dois slots de 32 bits e interpreta quatro canais de
16 bits; isso difere da leitura atual de dois slots de 16 bits.

## Diagnóstico de inicialização no speakerphone

Uma cópia em `lab/aec-speakerphone/firmware` cria o AFE em modo comunicação,
solicita AEC e NS WebRTC e expõe `afe` no console, mas **não alimenta o AFE nem
envia sua saída ao USB**. Serve somente para conferir inicialização, heap e
tamanhos de quadro sem substituir o firmware anterior no disco. Compilação com
o FQBN atual: 1.950.928 bytes flash e 89.856 bytes RAM estática. O wrapper
ESP_SR ainda avisa sobre partição de modelos; não se sabe se AFE WebRTC sem
modelos funciona sem essa partição. Não foi gravado nem testado em hardware.
Atualização: diagnóstico gravado em COM3 com hashes verificados; aguardando
reconexão sem BOOT para checar inicialização e comando `afe`. A saída USB
continua sem processamento AEC/NS nesta versão.
Após reconexão, `afe` retornou etapa 3 (objeto criado), AEC e NS solicitados,
feed 256 amostras/canal, 2 canais, fetch 512 amostras; heap livre antes
124.416 bytes e depois 89.640 bytes. Isso não prova que os algoritmos foram
efetivamente executados ou que sua saída está conectada ao USB. `info` relatou
SD montado, ES7210 presente e amplificador ligado; `usb` relatou stream ativo.

## Integração necessária

1. Determinar empiricamente os canais dos microfones e se o canal de referência
   do demo é real no hardware. Alternativa: referência digital obtida do áudio
   efetivamente enviado ao DAC após volume, mute e silêncio, não antes da fila.
2. Alinhar a referência e o microfone em amostras; considerar buffers DMA,
   latência de conversão, overflow, drift e reconexões. Medir o atraso com sinal
   controlado e reservar margens; não usar os dados de USB como se fossem
   automaticamente sincronizados com a captura.
3. Instanciar AFE de comunicação ou full-duplex em 16 kHz, habilitar AEC e NS,
   desabilitar recursos de reconhecimento não usados, tratar erros e avaliar
   CPU, PSRAM, heap, watchdog e latência. Testar disponibilidade de modelos.
4. Enviar ao USB a saída processada, nunca o canal cru enquanto AEC for ativo;
   definir fallback seguro e informar explicitamente falhas de inicialização.
5. Testar eco residual, voz próxima, fala simultânea, ruído e variação de volume;
   comparar AEC ligado/desligado e verificar integridade, sem alegar cancelamento
   perfeito nem usar volume alto durante calibração.

Fontes: https://docs.waveshare.com/ESP32-S3-AUDIO-Board e
https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/README.html
