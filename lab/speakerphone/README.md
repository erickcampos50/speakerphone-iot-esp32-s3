# Protótipo de speakerphone USB

Esta cópia experimental de `firmware/` mantém intacto o código anterior de
microfone USB. Uma versão anterior foi gravada e testada parcialmente.

## Objetivo

- USB UAC1 composto com CDC, captura e reprodução mono PCM 16 kHz / 16 bits.
- Única tarefa de captura I2S; áudio USB recebido passa por fila e é duplicado
  para os dois slots I2S do ES8311. Amplificador ligado ao iniciar; K3 alterna.
- No primeiro teste gravado, o volume do DAC era 35%. O usuário relatou som
  baixo e ausência de controle pelo Windows. A versão gravada seguinte usa 60%;
  o usuário confirmou que consegue regular o volume pelo Windows. O código
  atual usa 100% no DAC para teste, gravado e ainda sem teste de áudio.
- A gravação no SD permanece experimental e não é prioridade desta versão.

## Limitações importantes

- **Sem cancelamento de eco acústico no firmware.** Em reuniões teste o
  cancelamento do aplicativo, começando com volume baixo. Não se deve presumir
  desempenho equivalente a um speakerphone comercial.
- Bips foram temporariamente desativados para impedir que escrevam no I2S TX
  simultaneamente com o áudio USB. Reintegrar depois por um único mixer.
- Ainda faltam validar qualidade, latência, overflow e estabilidade em chamada.
  A fila de saída
  é limitada e pode descartar dados; `usb` informa bytes descartados.
- PID experimental 303A:0003; precisa de revisão antes de distribuição.

## Compilação

Core Arduino ESP32 3.3.12, `USBMode=default,CDCOnBoot=cdc`, flash 16 MB,
PSRAM OPI, partição app3M. `--warnings all`: 566.044 bytes flash e 75.400
bytes RAM estática, sem contar alocações dinâmicas.

## Testes de bancada antes de integrar

Upload realizado em COM3 com hashes verificados. Após reset automático, a
placa ainda aparece no bootloader; é necessário reconectar sem pressionar BOOT
antes de verificar enumeração ou áudio.

Resultado informado pelo usuário após reconexão: Windows detectou entrada e
saída, música foi reproduzida com volume baixo e o controle de volume do
Windows aparentemente não teve efeito. A captura de voz e a reprodução
simultânea em chamada ainda não foram confirmadas. Versão diagnóstica com DAC
a 60% compilou com 566.248 bytes de flash e 75.400 bytes de RAM estática;
foi gravada com hashes verificados em COM3; usuário confirmou controle de volume.
Versão a 100% compilada, 566.248 bytes de flash, gravada em COM3 com hashes
verificados. Aguardando reconexão sem BOOT e teste de áudio pelo usuário.

Usuário informou que a versão a 100% distorce muito; cerca de 50% no controle
do Windows é seu limite aceitável. Objetivo para a próxima versão: aproximar
esse nível de 65% no Windows, medindo o controle e calibrando antes de afirmar
equivalência. Consulta inicial `usb`: master=-21 dB e canal=-21 dB; o percentual
do Windows correspondente não foi medido. O core aplica ambos os ganhos em
sequência, portanto não se deve assumir escala linear entre percentual e DAC.

### Plano de teste de eco e microfonia

1. Primeiro, com volume baixo e sem reunião, reproduzir fala gravada e ouvir a
   captação em outro dispositivo ou gravação de teste, sem monitoramento local.
2. Subir o volume gradualmente até o limite escolhido; interromper imediatamente
   se houver apito, crescimento contínuo do nível ou distorção.
3. Fazer chamada com outra pessoa usando fones no outro lado e cancelamento de
   eco do aplicativo ativado. Verificar se ela ouve a própria voz ou apitos,
   inclusive ao falar simultaneamente e após reconexão.
4. Repetir com supressão do aplicativo desligada somente em volume baixo, para
   caracterizar o problema; voltar a ativá-la. Não prometer ausência de eco em
   todas as salas ou aplicativos sem implementação e validação de AEC.

1. Conferir enumeração simultânea como entrada, saída e console CDC.
2. Conferir voz e som do computador separadamente, em volume baixo, e K3.
3. Testar ambos juntos e após reconexão; medir cortes, latência e fila USB.
4. Realizar chamada com segunda pessoa, verificando eco e realimentação.
5. Reintegrar bips via mixer, controle de volume e documentar resultados.
