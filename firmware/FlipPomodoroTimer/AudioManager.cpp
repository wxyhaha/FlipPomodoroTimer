#include "AudioManager.h"
#include "PianoAudio.h"
#include "I2CLock.h"
#include <math.h>

bool AudioManager::startWorker() {
  (void)i2cMutex(); // Initialize before either task can use the shared bus.
  if (!expander) return false;
  for (int i = 0; i < 256; ++i) sine[i] = lroundf(sinf(i * 6.28318530718f / 256) * 26000);
  requests = xQueueCreate(1, sizeof(Request));
  if (!requests) return false;
  if (xTaskCreatePinnedToCore(taskEntry, "timer-audio", 4096, this, 1, &worker, 0) != pdPASS) {
    vQueueDelete(requests); requests = nullptr;
    return false;
  }
  isReady = true;
  Serial.println("[AUDIO] soft piano ready: start=1030ms, completion=2100ms");
  return true;
}

void AudioManager::enqueue(Cue cue) {
  if (!isReady || !requests) return;
  if (currentVolume == 0 && cue != Cue::Test) return;
  portENTER_CRITICAL(&stateMux);
  if ((cue == Cue::Click || cue == Cue::Pet) && selected != Cue::None) {
    portEXIT_CRITICAL(&stateMux);
    return; // A page click or pet sound must not cut off an important timer cue.
  }
  Request request{++generation, cue};
  selected = cue;
  portEXIT_CRITICAL(&stateMux);
  xQueueOverwrite(requests, &request);
}

void AudioManager::setVolume(uint8_t volume) {
  currentVolume = constrain(volume, 0, 85);
  if (!isReady || !esHandle) return;
  I2CLock lock;
  es8311_voice_volume_set(esHandle, currentVolume == 0 ? 0 : currentVolume, NULL);
}

void AudioManager::playStartTone() { enqueue(Cue::Start); }
void AudioManager::playCompletionChime() { enqueue(Cue::Completion); }
void AudioManager::playHourlyChime() { enqueue(Cue::HourlyChime); }
void AudioManager::playPetTone() { enqueue(Cue::Pet); }
void AudioManager::playClickTone() { enqueue(Cue::Click); }
void AudioManager::playTest() { enqueue(Cue::Test); }

void AudioManager::stop() {
  if (!requests) return;
  portENTER_CRITICAL(&stateMux);
  ++generation; selected = Cue::None;
  portEXIT_CRITICAL(&stateMux);
  xQueueReset(requests);
}

void AudioManager::stopCompletion() {
  if (!requests) return;
  portENTER_CRITICAL(&stateMux);
  bool cancel = selected == Cue::Completion;
  if (cancel) { ++generation; selected = Cue::None; }
  portEXIT_CRITICAL(&stateMux);
  if (cancel) xQueueReset(requests);
}

bool AudioManager::current(uint32_t id) {
  portENTER_CRITICAL(&stateMux);
  bool matches = generation == id;
  portEXIT_CRITICAL(&stateMux);
  return matches;
}

bool AudioManager::tone(uint16_t frequency, uint16_t duration, uint32_t id, bool checkCancel) {
  constexpr uint32_t sampleRate = 16000;
  uint32_t total = duration * sampleRate / 1000, phase = 0;
  uint32_t increment = (static_cast<uint64_t>(frequency) << 32) / sampleRate;
  int16_t pcm[128 * 2];
  for (uint32_t position = 0; position < total;) {
    if (checkCancel && !current(id)) return false;
    size_t batch = min(static_cast<uint32_t>(128), total - position);
    for (size_t i = 0; i < batch; ++i) {
      int attack = (position + i) * 256U / 64U;
      int release = (total - position - i) * 256U / 128U;
      int envelope = min(256, min(attack, release));
      int16_t sample = frequency ? static_cast<int32_t>(sine[phase >> 24]) * envelope / 256 : 0;
      pcm[2 * i] = pcm[2 * i + 1] = sample;
      phase += increment;
    }
    size_t bytes = batch * 4;
    if (i2s.write(reinterpret_cast<uint8_t *>(pcm), bytes) != bytes) {
      Serial.println("[AUDIO] I2S write incomplete");
      return false;
    }
    position += batch;
  }
  return true;
}

bool AudioManager::sequence(const Segment *segments, size_t count, uint32_t id) {
  for (size_t i = 0; i < count; ++i)
    if (!tone(segments[i].frequency, segments[i].duration, id)) return false;
  return true;
}

bool AudioManager::playPcm(const int16_t *samples, size_t count, uint32_t id) {
  static_assert(PianoAudio::kSampleRate == 16000, "PCM and I2S rates must match");
  int16_t stereo[128 * 2];
  for (size_t position = 0; position < count;) {
    if (!current(id)) return false;
    size_t batch = min(static_cast<size_t>(128), count - position);
    for (size_t i = 0; i < batch; ++i)
      stereo[2 * i] = stereo[2 * i + 1] = samples[position + i];
    if (i2s.write(reinterpret_cast<uint8_t *>(stereo), batch * 4) != batch * 4) {
      Serial.println("[AUDIO] I2S write incomplete");
      return false;
    }
    position += batch;
  }
  return true;
}

bool AudioManager::playCue(const Request &request) {
  static const Segment click[] = {{1500,15}};
  if (request.cue == Cue::Click) return sequence(click, 1, request.id);
  if (request.cue == Cue::Pet || request.cue == Cue::HourlyChime) {
    // Soft bell/electric-piano harmonic voice with exponential decay
    auto playSoftNote = [&](uint16_t freq, uint16_t durationMs, float gain, float decayRate) -> bool {
      constexpr uint32_t sampleRate = 16000;
      uint32_t total = durationMs * sampleRate / 1000;
      uint32_t phase1 = 0, phase2 = 0;
      uint32_t inc1 = (static_cast<uint64_t>(freq) << 32) / sampleRate;
      uint32_t inc2 = (static_cast<uint64_t>(freq * 2U) << 32) / sampleRate;
      int16_t pcm[128 * 2];
      for (uint32_t pos = 0; pos < total;) {
        if (!current(request.id)) return false;
        size_t batch = min(static_cast<uint32_t>(128), total - pos);
        for (size_t i = 0; i < batch; ++i) {
          uint32_t n = pos + i;
          float t = static_cast<float>(n) / static_cast<float>(sampleRate);
          float att = n < 96 ? static_cast<float>(n) / 96.0f : 1.0f;
          float rel = (total - n) < 160 ? static_cast<float>(total - n) / 160.0f : 1.0f;
          float env = gain * att * rel * expf(-decayRate * t);
          float s = static_cast<float>(sine[phase1 >> 24]) * 0.80f +
                    static_cast<float>(sine[phase2 >> 24]) * 0.20f;
          int16_t sample = static_cast<int16_t>(constrain(lroundf(s * env), -30000L, 30000L));
          pcm[2 * i] = pcm[2 * i + 1] = sample;
          phase1 += inc1;
          phase2 += inc2;
        }
        size_t bytes = batch * 4;
        if (i2s.write(reinterpret_cast<uint8_t *>(pcm), bytes) != bytes) return false;
        pos += batch;
      }
      return true;
    };

    if (request.cue == Cue::Pet) {
      // Gentle playful two-note purr chime: G5 -> C6
      return playSoftNote(784, 85, 0.42f, 10.0f) &&
             playSoftNote(1047, 145, 0.46f, 8.5f);
    }
    // Hourly chime: soft warm C5 -> E5 -> G5 -> C6 arpeggio (~1.5s)
    return playSoftNote(523, 280, 0.45f, 4.2f) &&
           playSoftNote(659, 280, 0.46f, 4.2f) &&
           playSoftNote(784, 300, 0.48f, 4.0f) &&
           playSoftNote(1047, 640, 0.52f, 2.8f);
  }
  if (request.cue == Cue::Start)
    return playPcm(PianoAudio::kStart, sizeof(PianoAudio::kStart) / sizeof(int16_t), request.id);
  if (request.cue == Cue::Completion)
    return playPcm(PianoAudio::kCompletion, sizeof(PianoAudio::kCompletion) / sizeof(int16_t), request.id);
  if (request.cue == Cue::Test) {
    return playPcm(PianoAudio::kStart, sizeof(PianoAudio::kStart) / sizeof(int16_t), request.id) &&
           tone(0, 900, request.id) &&
           playPcm(PianoAudio::kCompletion, sizeof(PianoAudio::kCompletion) / sizeof(int16_t), request.id);
  }
  return false;
}

void AudioManager::taskEntry(void *self) { static_cast<AudioManager *>(self)->taskLoop(); }

void AudioManager::taskLoop() {
  Request request{};
  for (;;) {
    if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE || !current(request.id)) continue;
    const char *name = request.cue == Cue::Completion ? "completion" :
        request.cue == Cue::HourlyChime ? "hourly-chime" :
        request.cue == Cue::Pet ? "pet" :
        request.cue == Cue::Start ? "start" : request.cue == Cue::Test ? "speaker-test" : "click";
    Serial.printf("[AUDIO] cue=%s begin\n", name);
    uint32_t playbackStarted = millis();
    { I2CLock lock; expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, HIGH); }
    vTaskDelay(pdMS_TO_TICKS(5));
    bool played = playCue(request);
    // I2S write queues DMA data; don't mute the amp while the final note is pending.
    bool drained = played && tone(0, 160, request.id);
    { I2CLock lock; expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, LOW); }
    if (!drained) tone(0, 160, request.id, false); // Clear queued tail while muted.
    portENTER_CRITICAL(&stateMux);
    bool finished = generation == request.id;
    if (finished) selected = Cue::None;
    portEXIT_CRITICAL(&stateMux);
    Serial.printf("[AUDIO] cue=%s %s\n", name, played && drained && finished ? "finished" : "cancelled-or-error");
    Serial.printf("[AUDIO] playback elapsed=%lu ms (includes drain)\n",
                  static_cast<unsigned long>(millis() - playbackStarted));
  }
}

bool AudioManager::init(Arduino_XCA9554SWSPI *exp, uint8_t initialVolume) {
  expander = exp;
  currentVolume = constrain(initialVolume, 0, 85);
  if (expander) {
    I2CLock lock;
    expander->pinMode(BOARD_EXPANDER_AMP_CTRL, OUTPUT);
    expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, LOW); // 默认关闭功放省电
  }

  i2s.setPins(PIN_ES7210_BCLK, PIN_ES7210_LRCK, PIN_ES8311_DOUT, PIN_ES7210_DIN, PIN_ES7210_MCLK);
  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    isReady = false;
    return false;
  }

  I2CLock lock;
  esHandle = es8311_create(0, ES8311_ADDRRES_0);
  if (!esHandle) {
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

  if (es8311_init(esHandle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK &&
      es8311_sample_frequency_config(esHandle, es_clk.mclk_frequency, es_clk.sample_frequency) == ESP_OK &&
      es8311_voice_volume_set(esHandle, currentVolume, NULL) == ESP_OK) {
    if (currentVolume == 75) {
      Wire.beginTransmission(ES8311_ADDRRES_0);
      Wire.write(0x32);
      if (Wire.endTransmission(false) != 0 ||
          Wire.requestFrom(static_cast<uint8_t>(ES8311_ADDRRES_0), static_cast<uint8_t>(1)) != 1 ||
          Wire.read() != 0xBF) {
        Serial.println("[AUDIO] DAC 0dB volume readback failed");
        return false;
      }
    }
    Serial.printf("[AUDIO] DAC vol=%u, I2S configured=%lu Hz/%u bits\n",
                  currentVolume, static_cast<unsigned long>(i2s.txSampleRate()), static_cast<unsigned>(i2s.txDataWidth()));
    return startWorker();
  }

  isReady = false;
  return false;
}
