# Módulo de áudio de saída — ES8311 + Beeper

Laboratório: `esp32-s3-audio-firmware/lab/es8311-beeper/`
Nada do firmware principal foi alterado.

## 1. Status

Compila com o FQBN pedido (`esp32:esp32:esp32s3`, core 3.3.12) e **zero
warnings** (`--warnings all`):

```
Sketch uses 361688 bytes (11%) of program storage space. Maximum is 3145728 bytes.
Global variables use 25032 bytes (7%) of dynamic memory, leaving 302648 bytes for local variables. Maximum is 327680 bytes.
```

Comando usado:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc,UploadSpeed=921600" `
  "D:\Documents\00FERRAMENTAS\esp32-s3-audio-firmware\lab\es8311-beeper\teste"
```

Os dois `.cpp` do módulo foram compilados como unidades de tradução
separadas (`es8311.cpp.o`, `beeper.cpp.o`), exatamente como serão compilados
dentro de `firmware/`. **Nada foi gravado na placa** (COM3 em uso).

## 2. Arquivos

| Arquivo | Conteúdo |
|---|---|
| `es8311.h` / `es8311.cpp` | driver mínimo do DAC via `Wire`: `begin(addr = 0x18)`, `setVolume(0..100)`, `present()`, `lastError()` |
| `beeper.h` / `beeper.cpp` | gerador de tons não bloqueante, fila de eventos, controle do amplificador |
| `teste/teste.ino` | instancia I2S + Beeper e toca as 8 sequências em ordem |
| `teste/src/*.{h,cpp}` | **cópias idênticas** dos 4 arquivos acima (o `arduino-cli` só compila fontes dentro da pasta do sketch; a pasta `src/` é o mecanismo padrão para isso) |

## 3. O que funciona

- **Driver ES8311**: sequência de init portada do driver oficial da Espressif
  (`es8311.c` do demo Waveshare), sem inventar registrador nenhum:
  reset `0x1F` → `0x00` → `0x80`, clock manager (`0x01` = `0x3F`, `0x02..0x08`
  com os coeficientes de 16 kHz / MCLK 4,096 MHz = 256·fs), formato
  (slave, 16 bits, I2S), power-up analógico (`0x0D`, `0x0E`, `0x12`, `0x13`),
  bypass dos equalizadores (`0x1C`, `0x37`) e volume (`0x32`).
  Volume: mesma escala do driver oficial — `0` = mudo, `100` = `0xFF`;
  60 % → `0x98`.
- **Beeper não bloqueante**: `play()` só enfileira; `update()` gera no máximo
  160 frames (10 ms) por chamada, sem `delay()`.
- **Seno 16 kHz / 16 bits / estéreo** com envelope trapezoidal de 5 ms
  (fade-in e fade-out por nota), amplitude 12000 (~-8,7 dBFS).
- **Sequências** (com 60 ms de silêncio na entrada e 120 ms na saída):

  | Evento | Conteúdo | Duração total |
  |---|---|---|
  | Boot | 523 / 659 / 784 Hz, 90 ms cada | 450 ms |
  | RecordStart | 1046 Hz 70 ms, silêncio 70 ms, 1046 Hz 70 ms | 390 ms |
  | RecordStop | 392 Hz 160 ms | 340 ms |
  | LowBattery | 330 Hz 90 ms × 3, silêncio 120 ms entre eles | 690 ms |
  | Error | varredura 880 → 330 Hz em 400 ms | 580 ms |
  | WifiReady | 1318 Hz 60 ms, silêncio 60 ms, 1318 Hz 60 ms | 360 ms |
  | AmpOn / AmpOff | 880 Hz / 440 Hz, 40 ms | 220 ms |

- **Fila**: até 4 eventos; se `play()` for chamado tocando, o som atual não é
  cortado (o novo entra na fila e emenda sem desligar o amplificador). Fila
  cheia = evento novo descartado (sem alocação dinâmica).
- **Amplificador**: `setAmpControl(fn)` recebe `true` no começo e `false` no fim
  de cada sequência. O callback só é chamado uma vez por sequência, mesmo com
  a fila emendando — assim o firmware pode salvar/restaurar o estado do EXIO8.
- **Timeout do I2S**: `write()` usa 25 ms e o valor anterior é restaurado logo
  depois (`getTimeout()`/`setTimeout()`), para não encurtar o timeout do RX
  do microfone.

## 4. O que NÃO foi possível validar sem a placa

- Se o som realmente sai no alto-falante (volume audível, chiado, nível).
- Se o MCLK/BCLK reais chegam ao ES8311 com a polaridade esperada (o demo
  oficial usa a mesma configuração, então a expectativa é alta).
- Nível de clique nos ataques/relaxamentos e o tempo de acomodação real do
  NS4150B (usei 60 ms de lead-in, o demo usa 50 ms de `vTaskDelay`).
- Latência percebida: o buffer DMA do TX guarda ~90 ms (6 × 240 frames), então
  o bip sai ~90 ms depois do `play()`.
- Coexistência com a gravação: o TX e o RX compartilham o mesmo periférico e a
  mesma taxa (16 kHz), então o full-duplex deve funcionar, mas **não foi
  testado** se o `write()` do TX atrapalha a captura em andamento.
- Se `configureTX()` funciona quando o `begin()` foi feito sem `DIN`
  (nesse caso o canal TX nem existe e `Beeper::begin()` devolve `false`).

## 5. Decisões de buffer

| Parâmetro | Valor | Motivo |
|---|---|---|
| Bloco por `update()` | 160 frames (640 bytes, 10 ms) | atende ao limite de ~10 ms por chamada; bloco pequeno = bloqueio curto no `write()` |
| Timeout do `write()` | 25 ms (restaurado depois) | 10 ms de áudio + folga; nunca trava a gravação |
| Buffer do bloco | `int16_t _buf[320]` (640 B) | estático no objeto, sem `malloc`, sem STL |
| Lead-in | 60 ms de silêncio | amplificador (EXIO8) precisa de ~50 ms para acomodar |
| Tail | 120 ms de silêncio | maior que o buffer DMA (~90 ms), senão o fim do bip é cortado quando o amp desliga |
| Fade | 5 ms (80 amostras) por nota | evita clique no ataque e no fim de cada nota |
| Amplitude | 12000 | ~-8,7 dBFS: sobra margem para o AGC/DRC do codec não clipar |
| Fila | 4 eventos (`BeepEvent[4]`) | ring buffer fixo, sem alocação |

## 6. Como integrar no firmware principal

1. Copie `es8311.h`, `es8311.cpp`, `beeper.h`, `beeper.cpp` para
   `firmware/` (o Arduino compila todo `.cpp` da pasta do sketch).

2. `firmware.ino`, junto dos outros includes (depois da linha 12,
   `#include "battery.h"`):

   ```cpp
   #include "beeper.h"
   ```

3. `firmware.ino`, depois da linha 31 (`bool amp_on = false;`):

   ```cpp
   Beeper beeper;
   bool amp_before_beep = false;
   ```

4. `firmware.ino`, depois da linha 34 (`int mic_vol_db = 0;`) — o callback
   guarda e devolve o estado do amplificador, para o bip não desligar o amp
   que o usuário ligou no K3:

   ```cpp
   // O Beeper liga o amplificador so durante o bip e devolve o estado anterior.
   void ampForBeep(bool on) {
     if (on) {
       amp_before_beep = amp_on;
       amp_on = true;
       exio.write(EXIO_PA_CTRL, true);
     } else {
       amp_on = amp_before_beep;
       exio.write(EXIO_PA_CTRL, amp_before_beep);
     }
   }
   ```

5. `firmware.ino`, no `setup()`, depois do bloco do ES7210 (depois da linha
   507, antes do `pinMode(PIN_BTN_BOOT, ...)`). **Precisa vir depois do
   `i2s.begin()`** (que cria o canal TX) e do `Wire.begin()`:

   ```cpp
   beeper.setAmpControl(ampForBeep);
   if (beeper.begin(&i2s)) {
     beeper.setVolume(60);
     Serial.println("[OUT] ES8311 ok");
   } else {
     Serial.println("[OUT] ES8311 nao respondeu (0x18)");
   }
   ```

6. `firmware.ino`, no `loop()`, depois da linha 519
   (`battery.update(millis());`):

   ```cpp
   beeper.update();
   ```

7. Chamadas dos eventos:

   ```cpp
   // fim do setup(), depois do beeper.begin():
   beeper.play(BeepEvent::Boot);

   // startRecording(), depois do rec.start() bem-sucedido:
   beeper.play(BeepEvent::RecordStart);

   // stopRecording(), depois do rec.stop():
   beeper.play(BeepEvent::RecordStop);

   // loop(), no trecho de bateria critica (depois de stopRecording()):
   beeper.play(BeepEvent::LowBattery);

   // toggleAmp():
   beeper.play(amp_on ? BeepEvent::AmpOn : BeepEvent::AmpOff);

   // erro de SD / falha de gravacao:
   beeper.play(BeepEvent::Error);
   ```

   Nenhuma dessas chamadas bloqueia; a ordem em que são feitas não importa
   para o `loop()`.

## 7. Cuidados de integração

- **Bip dentro da gravação**: `startRecording()` desliga o amplificador para
  evitar realimentação, mas `beeper.play(RecordStart)` o religa por ~390 ms.
  O microfone vai captar esse bip no começo do WAV. Duas saídas:
  (a) chamar `beeper.play(RecordStart)` **antes** de `rec.start()` e só iniciar
  a gravação quando `!beeper.busy()`; ou (b) não tocar `RecordStart` (usar só
  `RecordStop`).
- **AmpOn/AmpOff**: como o callback restaura `amp_before_beep`, tocar
  `AmpOn` com o amplificador já ligado mantém tudo consistente; o `amp_on` do
  firmware continua sendo a única fonte de verdade.
- **Não chamar `i2s.begin()` de novo** nem mexer em `setPins()`: o módulo só
  usa `configureTX()` com os mesmos parâmetros do RX (16 kHz/16 bits/estéreo),
  que na prática retorna `true` sem reconfigurar clock nenhum.
- **Volume**: `setVolume()` grava no registrador `0x32` do DAC; o padrão do
  módulo é 60 %. `setVolume(0)` é mudo (útil para "modo silencioso").
- **Cópias em `teste/src/`**: se você editar o módulo no laboratório, recopie
  os 4 arquivos para `teste/src/` antes de recompilar o sketch de teste.
