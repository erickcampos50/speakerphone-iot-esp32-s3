#pragma once

// ---------------------------------------------------------------------------
// Mapa de hardware - Waveshare ESP32-S3-AUDIO-Board (rev 1.1)
// Fonte: esquematico oficial + demo da Waveshare
// ---------------------------------------------------------------------------

// I2C (codecs ES8311/ES7210, TCA9555, PCF85063)
#define PIN_I2C_SDA   11
#define PIN_I2C_SCL   10
#define I2C_FREQ_HZ   100000

// I2S compartilhado (ES7210 = ADC/mic, ES8311 = DAC/saida)
#define PIN_I2S_MCLK  12
#define PIN_I2S_BCLK  13
#define PIN_I2S_LRCK  14
#define PIN_I2S_DIN   15   // ES7210 -> ESP32
#define PIN_I2S_DOUT  16   // ESP32  -> ES8311

// LED ring: 7x WS2812B
#define PIN_LED_DATA  38
#define LED_COUNT     7

// Botoes
#define PIN_BTN_BOOT  0    // BOOT, ativo em nivel baixo

// SD card (SDMMC 1-bit)
#define PIN_SD_CLK    40
#define PIN_SD_CMD    42
#define PIN_SD_D0     41

// TCA9555 (expansor de IO)
#define TCA9555_ADDR      0x20
#define EXIO_SD_D3        3    // alto habilita o cartao SD
#define EXIO_PA_CTRL      8    // alto liga o amplificador NS4150B
#define EXIO_KEY1         9    // K1 (entrada)
#define EXIO_KEY2         10   // K2 (entrada)
#define EXIO_KEY3         11   // K3 (entrada)

// Bateria (divisor 200K/100K -> fator 3.0)
// OBS: no esquematico o BAT_ADC e o GPIO1, mas vem desabilitado de fabrica
// (precisa soldar um resistor 0R e conflita com a camera).
// O demo oficial da Waveshare le o GPIO8. O firmware detecta qual responde.
#define PIN_BAT_ADC_SCHEMATIC  1
#define PIN_BAT_ADC_DEMO       8
#define BAT_DIVIDER_RATIO      3.0f
#define BAT_CALIBRATION        0.990476f

// Codecs
#define ES7210_ADDR   0x40
#define ES8311_ADDR   0x18

// Audio
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_CHANNELS      2
#define AUDIO_BITS          16
