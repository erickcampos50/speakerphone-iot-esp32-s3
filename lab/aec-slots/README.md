# Diagnóstico experimental dos slots ES7210

Esta versão é uma cópia de teste, não o firmware de speakerphone validado.
Usa I2S estéreo com slots de 32 bits, interpreta cada palavra de 32 bits como
dois segmentos de 16 bits e registra somente pico e contagem por segmento.
O comando `slots` retorna os quatro picos desde a leitura anterior e zera os
contadores. A ordem e o alinhamento dos segmentos ainda não foram confirmados.

Saída usa amostras de 32 bits para compatibilidade com os slots e o DAC é
iniciado a 35% para testes. A entrada USB usa uma seleção provisória e pode
não funcionar corretamente. Não usar essa versão em reuniões ou gravações.
O AFE é inicializado para diagnóstico, mas não processa o som: **AEC e NS não
estão ativos no fluxo**.

Comparar picos com silêncio, fala perto de cada microfone e reprodução de tom
conhecido em baixo volume, com o ambiente quieto. Uma variação associada ao tom
identifica apenas um candidato a referência: validar com correlação, rota
física, alinhamento temporal e mapeamento de palavras antes de integrar AEC.

Compilação: 1.951.296 bytes flash e 89.880 bytes RAM estática. O wrapper ESP_SR
emite aviso sobre partição de modelos; o AFE de diagnóstico anterior criou o
objeto com a partição atual. Gravado em COM3 com hashes verificados; aguardando
reconexão sem BOOT para testar os segmentos em hardware.
Após tentativa de reconexão o usuário relatou erro e o computador não listou
nenhuma interface USB da placa. Não foi possível obter logs ou confirmar a
causa; o diagnóstico não deve ser considerado funcional. A recuperação proposta
é entrar no bootloader manualmente e restaurar a versão anterior do speakerphone.
Posteriormente o binário `build-speakerphone-max` foi regravado em COM3 e o
hash foi verificado. Falta reconectar sem BOOT e confirmar funcionamento.
Após reconexão, o Windows voltou a listar entrada/saída e CDC COM7; `info` e
`usb` responderam. A causa da falha anterior permanece desconhecida.
Hipótese não comprovada: o leitor do diagnóstico adicionou buffers locais à
pilha de 3072 bytes. Preparada uma variante experimental com pilha de 8192
bytes e consulta de margem de pilha em `slots`; compilou (1.951.420 bytes).
Gravada em COM3 com hash verificado; aguardando reinicialização normal. Pode
não resolver a falha. Não é AEC funcional.

Após reconexão, placa enumerou em COM7 e `afe` reportou objeto criado; a task
leitora apresentou margem mínima de pilha de 4900 bytes. Em um intervalo de
silêncio, `slots` relatou picos 12,302,5,304 em 38.400 frames. Durante uma
tentativa de tom de 440 Hz enviado à saída USB, relatou 12,366,5,369 em 51.584
frames. Volume do host foi reportado como -21 dB no master e -21 dB no canal.
O resultado não identifica referência: não se comprovou que o tom foi audível
nem que a reprodução realmente chegou ao DAC. Mais testes controlados necessários.
O usuário confirmou que não ouviu o tom. Portanto não se pode inferir que a
referência física falhou. A saída do diagnóstico usa I2S de 32 bits e DAC 35%,
distinta da versão de 16 bits que reproduziu música; também havia atenuação
do host. Investigar e comprovar reprodução antes de interpretar os canais.

Investigação suspensa a pedido do usuário. Restaurar o binário previamente
testado `build-speakerphone-max`; AEC e redução de ruído não ficam ativos.
O protótipo e as medições permanecem guardados para uma etapa futura.
