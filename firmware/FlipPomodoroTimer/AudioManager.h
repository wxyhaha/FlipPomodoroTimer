#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "ESP_I2S.h"
#include "Arduino_GFX_Library.h"
#include "es8311.h"

class AudioManager {
public:
  bool isReady = false;

  bool init(Arduino_XCA9554SWSPI *exp) {
    expander = exp;
    if (expander) {
      expander->pinMode(BOARD_EXPANDER_AMP_CTRL, OUTPUT);
      expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, LOW); // 默认关闭功放省电
    }

    i2s.setPins(PIN_ES7210_BCLK, PIN_ES7210_LRCK, PIN_ES8311_DOUT, PIN_ES7210_DIN, PIN_ES7210_MCLK);
    if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
      isReady = false;
      return false;
    }

    es8311_handle_t es_handle = es8311_create(0, ES8311_ADDRRES_0);
    if (!es_handle) {
      isReady = false;
      return false;
    }

    const es8311_clock_config_t es_clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = 16000 * 256,
      .sample_frequency = 16000
    };

    if (es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK &&
        es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency) == ESP_OK &&
        es8311_voice_volume_set(es_handle, 100, NULL) == ESP_OK) { // 硬件音量直接调至最大 100
      isReady = true;
      return true;
    }

    isReady = false;
    return false;
  }

  void playTone(uint16_t freqHz, uint16_t durationMs) {
    if (!isReady || !expander) return;

    // 打开功放
    expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, HIGH);
    delay(5);

    const size_t sampleRate = 16000;
    const size_t totalSamples = (sampleRate * durationMs) / 1000;
    const int16_t amplitude = 28000; // 满幅 16 位正弦波，实现最大音量输出
    int16_t buf[256 * 2];

    uint32_t phase = 0;
    const uint32_t phase_step = (uint32_t)(((uint64_t)freqHz << 32) / sampleRate);

    size_t samplesGenerated = 0;
    while (samplesGenerated < totalSamples) {
      size_t batch = (totalSamples - samplesGenerated < 256) ? (totalSamples - samplesGenerated) : 256;
      for (size_t i = 0; i < batch; ++i) {
        phase += phase_step;
        int16_t sample = (phase & 0x80000000U) ? amplitude : -amplitude;
        buf[i * 2] = sample;
        buf[i * 2 + 1] = sample;
      }
      i2s.write((uint8_t *)buf, batch * 4);
      samplesGenerated += batch;
    }

    // 关闭功放省电与防爆音
    delay(5);
    expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, LOW);
  }

  void playStartTone() {
    if (!isReady) return;
    // 计时开始/恢复/切面提示音：清脆升调双音和弦 (C5 523Hz -> G5 784Hz)
    playTone(523, 70);
    delay(15);
    playTone(784, 110);
  }

  void playCompletionChime() {
    if (!isReady) return;
    // 计时完成提醒：响亮高分贝三连音警报 (E5 659Hz -> G5 784Hz -> C6 1046Hz)
    playTone(659, 100);
    delay(20);
    playTone(784, 100);
    delay(20);
    playTone(1046, 250);
  }

  void playClickTone() {
    if (!isReady) return;
    // 极短高频轻脆按键音 (1500Hz, 15ms)，消除延迟感
    playTone(1500, 15);
  }

private:
  I2SClass i2s;
  Arduino_XCA9554SWSPI *expander = nullptr;
};
