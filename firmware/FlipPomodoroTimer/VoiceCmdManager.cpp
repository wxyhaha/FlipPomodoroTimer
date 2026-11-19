#include "VoiceCmdManager.h"
#include "I2CLock.h"
#include "esp_heap_caps.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

extern "C" {
extern const esp_mn_iface_t esp_sr_multinet7_quantized;
}

namespace {
srmodel_list_t *gModels = nullptr;
const esp_afe_sr_iface_t *gAfeHandle = nullptr;
esp_afe_sr_data_t *gAfeData = nullptr;
const esp_mn_iface_t *gMultinet = nullptr;
model_iface_data_t *gModelData = nullptr;
}

bool VoiceCmdManager::writeES7210Reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(es7210Addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool VoiceCmdManager::initES7210() {
  I2CLock lock;
  const uint8_t addrs[2] = {0x40, 0x41};
  es7210Addr = 0;
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      es7210Addr = a;
      break;
    }
  }
  if (es7210Addr == 0) {
    Serial.println("[VOICE] ES7210 not found on I2C (0x40/0x41)");
    return false;
  }

  // 复位并初始化 ES7210 为 16kHz 16-bit Standard I2S 从机（接收来自 ESP32 的 MCLK/BCLK/LRCK）
  bool ok = true;
  ok &= writeES7210Reg(0x00, 0xFF);
  delay(5);
  ok &= writeES7210Reg(0x00, 0x32);
  ok &= writeES7210Reg(0x09, 0x30);
  ok &= writeES7210Reg(0x0A, 0x30);
  ok &= writeES7210Reg(0x23, 0x2A);
  ok &= writeES7210Reg(0x22, 0x0A);
  ok &= writeES7210Reg(0x21, 0x2A);
  ok &= writeES7210Reg(0x20, 0x0A);
  ok &= writeES7210Reg(0x08, 0x10); // MODE_CFG (默认 0x10, I2S Slave)
  // MCLK = 16000 * 256 = 4.096MHz: es7210 coeff_div (adc_div=1, dll=1, doubler=1 -> 0xC1)
  ok &= writeES7210Reg(0x02, 0xC1);
  ok &= writeES7210Reg(0x03, 0x01); // MST_CLK_CTL (BCLK=MCLK/8=512kHz)
  ok &= writeES7210Reg(0x04, 0x01); // LRCK_DIV_H
  ok &= writeES7210Reg(0x05, 0x00); // LRCK_DIV_L (256)
  ok &= writeES7210Reg(0x06, 0x00); // 取消数字掉电
  ok &= writeES7210Reg(0x07, 0x20); // OSR=32
  ok &= writeES7210Reg(0x11, 0x60); // Standard I2S, 16-bit
  ok &= writeES7210Reg(0x12, 0x00); // 非 TDM 立体声 (ADC1->L, ADC2->R)
  ok &= writeES7210Reg(0x14, 0x00); // 解除 ADC12 静音
  ok &= writeES7210Reg(0x15, 0x00); // 解除 ADC34 静音
  // 开启模拟电源与 MIC1/MIC2 偏置、PGA 增益 (+27dB)
  ok &= writeES7210Reg(0x40, 0x43);
  ok &= writeES7210Reg(0x41, 0x70);
  ok &= writeES7210Reg(0x42, 0x70);
  ok &= writeES7210Reg(0x43, 0x1E); // MIC1 PGA max gain + enable
  ok &= writeES7210Reg(0x44, 0x1E); // MIC2 PGA max gain + enable
  ok &= writeES7210Reg(0x45, 0x1B);
  ok &= writeES7210Reg(0x46, 0x1B);
  ok &= writeES7210Reg(0x47, 0x08);
  ok &= writeES7210Reg(0x48, 0x08);
  ok &= writeES7210Reg(0x49, 0x08);
  ok &= writeES7210Reg(0x4A, 0x08);
  ok &= writeES7210Reg(0x4B, 0x00); // 清除掉电标志，开启 MIC1/2 电源
  ok &= writeES7210Reg(0x4C, 0x00); // 开启 MIC3/4 电源
  ok &= writeES7210Reg(0x01, 0x00); // 开启全部内部时钟 (CLK_OFF=0)
  ok &= writeES7210Reg(0x00, 0x41); // 释放复位，进入正常工作状态

  auto readReg = [&](uint8_t r) -> int {
    Wire.beginTransmission(es7210Addr);
    Wire.write(r);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(es7210Addr, static_cast<uint8_t>(1)) != 1) return -1;
    return Wire.read();
  };
  Serial.printf("[VOICE] ES7210 regs: 00=%02X 01=%02X 02=%02X 08=%02X 11=%02X 14=%02X 40=%02X 43=%02X 4B=%02X\n",
                readReg(0x00), readReg(0x01), readReg(0x02), readReg(0x08),
                readReg(0x11), readReg(0x14), readReg(0x40), readReg(0x43), readReg(0x4B));

  Serial.printf("[VOICE] ES7210 at 0x%02X init %s\n", es7210Addr, ok ? "OK" : "FAILED");
  return ok;
}

bool VoiceCmdManager::begin(AudioManager &audioMgr) {
  audio = &audioMgr;
  if (!audio || !audio->isReady) {
    Serial.println("[VOICE] AudioManager not ready");
    return false;
  }
  if (!initES7210()) {
    return false;
  }

  gModels = esp_srmodel_init("model");
  if (!gModels) {
    Serial.println("[VOICE] esp_srmodel_init('model') failed (check esp_sr_16 partition)");
    return false;
  }

  char *mnName = esp_srmodel_filter(gModels, ESP_MN_PREFIX, ESP_MN_CHINESE);
  if (!mnName) {
    Serial.println("[VOICE] No Chinese MultiNet model found in srmodels.bin");
    return false;
  }

  afe_config_t *afeCfg = afe_config_init("MN", gModels, AFE_TYPE_SR, AFE_MODE_LOW_COST);
  if (!afeCfg) {
    Serial.println("[VOICE] afe_config_init failed");
    return false;
  }
  afeCfg->wakenet_init = false;
  afeCfg->vad_mode = VAD_MODE_2;
  afeCfg->vad_min_speech_ms = 64;
  afeCfg->vad_min_noise_ms = 180;
  afeCfg->vad_delay_ms = 128;
  afeCfg->agc_init = true;
  afeCfg->afe_perferred_core = 0;
  afeCfg->afe_perferred_priority = 3;
  afeCfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
  gAfeHandle = esp_afe_handle_from_config(afeCfg);
  gAfeData = gAfeHandle ? gAfeHandle->create_from_config(afeCfg) : nullptr;
  afe_config_free(afeCfg);

  if (!gAfeHandle || !gAfeData) {
    Serial.println("[VOICE] AFE create failed");
    return false;
  }

  gMultinet = esp_mn_handle_from_name(mnName);
  if (!gMultinet) {
    gMultinet = &esp_sr_multinet7_quantized;
  }
  gModelData = gMultinet ? gMultinet->create(mnName, 3500) : nullptr;
  if (!gMultinet || !gModelData) {
    Serial.printf("[VOICE] MultiNet create failed for %s\n", mnName);
    return false;
  }
  if (gMultinet->switch_loader_mode && strncmp(mnName, "mn5", 3) != 0) {
    gMultinet->switch_loader_mode(gModelData, ESP_MN_LOAD_FROM_PSRAM);
  }

  esp_mn_commands_alloc(gMultinet, gModelData);
  esp_mn_commands_add(VOICE_CMD_CONFIRM, "que ding");
  esp_mn_commands_add(VOICE_CMD_CONFIRM, "kai shi");
  esp_mn_commands_add(VOICE_CMD_CONFIRM, "que ren");
  esp_mn_commands_add(VOICE_CMD_SKIP, "tiao guo");
  esp_mn_commands_add(VOICE_CMD_SKIP, "qu xiao");
  esp_mn_commands_update();

  if (xTaskCreatePinnedToCore(feedTaskEntry, "voice-feed", 6144, this, 2, nullptr, 0) != pdPASS ||
      xTaskCreatePinnedToCore(detectTaskEntry, "voice-det", 12288, this, 1, nullptr, 1) != pdPASS) {
    Serial.println("[VOICE] Failed to create background tasks");
    return false;
  }

  isReady = true;
  Serial.printf("[VOICE] Chinese MultiNet ready (%s): '确定/开始' & '跳过/取消'\n", mnName);
  return true;
}

void VoiceCmdManager::setListening(bool enable) {
  if (!isReady) return;
  if (enable && !listening) {
    pendingCmd = VOICE_CMD_NONE;
    resetRequested = true;
    listening = true;
    Serial.println("[VOICE] Listening started");
  } else if (!enable && listening) {
    listening = false;
    speechActive = false;
    Serial.println("[VOICE] Listening stopped");
  }
}

VoiceCommand VoiceCmdManager::pollCommand() {
  uint8_t cmd = pendingCmd;
  if (cmd != VOICE_CMD_NONE) {
    pendingCmd = VOICE_CMD_NONE;
  }
  return static_cast<VoiceCommand>(cmd);
}

void VoiceCmdManager::feedTaskEntry(void *arg) {
  static_cast<VoiceCmdManager *>(arg)->feedLoop();
}

void VoiceCmdManager::detectTaskEntry(void *arg) {
  static_cast<VoiceCmdManager *>(arg)->detectLoop();
}

void VoiceCmdManager::feedLoop() {
  int chunk = gAfeHandle->get_feed_chunksize(gAfeData);
  int ch = gAfeHandle->get_feed_channel_num(gAfeData);
  if (ch < 2) ch = 2;
  size_t bytesNeeded = static_cast<size_t>(chunk) * static_cast<size_t>(ch) * sizeof(int16_t);
  int16_t *buf = static_cast<int16_t *>(heap_caps_malloc(bytesNeeded, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  Serial.printf("[VOICE] feedTask chunk=%d ch=%d bytes=%u\n", chunk, ch, static_cast<unsigned>(bytesNeeded));
  if (!buf) {
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    // 非监听状态或扬声器正在播放提示音时，完全暂停 I2S RX 读取与 AFE 喂入，
    // 避免与 Core 0 的音频播放任务争抢 DMA/CPU 导致提示音杂音，也防止回声进入识别器
    if (!listening || audio->isBusy()) {
      resetRequested = true;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t got = audio->readMicPcm(reinterpret_cast<uint8_t *>(buf), bytesNeeded);
    if (got < bytesNeeded) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    for (int i = 0; i + 1 < chunk * ch; i += 2) {
      // 数字增益 x4（带饱和钳位）
      int32_t sL = static_cast<int32_t>(buf[i]) * 4;
      int32_t sR = static_cast<int32_t>(buf[i + 1]) * 4;
      if (sL > 32767) sL = 32767; else if (sL < -32768) sL = -32768;
      if (sR > 32767) sR = 32767; else if (sR < -32768) sR = -32768;
      buf[i] = static_cast<int16_t>(sL);
      buf[i + 1] = static_cast<int16_t>(sR);
    }
    gAfeHandle->feed(gAfeData, buf);
    vTaskDelay(1);
  }
}

void VoiceCmdManager::detectLoop() {
  bool inSpeech = false;
  int silenceAfterSpeech = 0;
  int lastVad = -1;
  while (true) {
    if (!listening || audio->isBusy()) {
      inSpeech = false;
      speechActive = false;
      silenceAfterSpeech = 0;
      lastVad = -1;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (resetRequested) {
      resetRequested = false;
      inSpeech = false;
      speechActive = false;
      silenceAfterSpeech = 0;
      lastVad = -1;
      if (gAfeHandle->reset_buffer) {
        gAfeHandle->reset_buffer(gAfeData);
      }
      gMultinet->clean(gModelData);
    }
    afe_fetch_result_t *res = gAfeHandle->fetch_with_delay
                                  ? gAfeHandle->fetch_with_delay(gAfeData, pdMS_TO_TICKS(50))
                                  : gAfeHandle->fetch(gAfeData);
    if (!res || res->ret_value == ESP_FAIL || !res->data) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    int curVad = static_cast<int>(res->vad_state);
    if (curVad != lastVad) {
      lastVad = curVad;
      Serial.printf("[VOICE] VAD=%d rb=%.2f\n", lastVad, res->ringbuff_free_pct);
    }

    // 仅在检测到人声（含 vad_cache 预卷）及少量尾音帧（最多 4 帧）期间驱动 MultiNet；
    // 结束未命中时立即清零模型与 AFE 缓冲，避免杂音导致下一句积压延迟
    esp_mn_state_t st = ESP_MN_STATE_DETECTING;
    if (res->vad_state == VAD_SPEECH) {
      silenceAfterSpeech = 0;
      if (!inSpeech) {
        inSpeech = true;
        speechActive = true;
        gMultinet->clean(gModelData);
        if (res->vad_cache && res->vad_cache_size >= res->data_size && res->data_size > 0) {
          int chunkSamples = res->data_size / static_cast<int>(sizeof(int16_t));
          int totalSamples = res->vad_cache_size / static_cast<int>(sizeof(int16_t));
          for (int off = 0; off + chunkSamples <= totalSamples; off += chunkSamples) {
            st = gMultinet->detect(gModelData, res->vad_cache + off);
            if (st == ESP_MN_STATE_DETECTED) break;
          }
        }
      }
      if (st == ESP_MN_STATE_DETECTING) {
        st = gMultinet->detect(gModelData, res->data);
      }
    } else if (inSpeech) {
      ++silenceAfterSpeech;
      st = gMultinet->detect(gModelData, res->data);
      if (st == ESP_MN_STATE_DETECTING && silenceAfterSpeech >= 4) {
        inSpeech = false;
        speechActive = false;
        silenceAfterSpeech = 0;
        gMultinet->clean(gModelData);
        if (gAfeHandle->reset_buffer) {
          gAfeHandle->reset_buffer(gAfeData);
        }
      }
    } else {
      vTaskDelay(1);
      continue;
    }

    if (st == ESP_MN_STATE_DETECTED) {
      esp_mn_results_t *mnRes = gMultinet->get_results(gModelData);
      if (mnRes && mnRes->num > 0) {
        int cmdId = mnRes->command_id[0];
        Serial.printf("[VOICE] Detected cmd=%d prob=%.2f rb=%.2f\n",
                      cmdId, mnRes->prob[0], res->ringbuff_free_pct);
        if (cmdId == VOICE_CMD_CONFIRM || cmdId == VOICE_CMD_SKIP) {
          pendingCmd = static_cast<uint8_t>(cmdId);
          listening = false;
        }
      }
      inSpeech = false;
      speechActive = false;
      silenceAfterSpeech = 0;
      gMultinet->clean(gModelData);
    } else if (st == ESP_MN_STATE_TIMEOUT) {
      Serial.println("[VOICE] MN_STATE_TIMEOUT -> clean");
      inSpeech = false;
      speechActive = false;
      silenceAfterSpeech = 0;
      gMultinet->clean(gModelData);
      if (gAfeHandle->reset_buffer) {
        gAfeHandle->reset_buffer(gAfeData);
      }
    }
    vTaskDelay(1);
  }
}
