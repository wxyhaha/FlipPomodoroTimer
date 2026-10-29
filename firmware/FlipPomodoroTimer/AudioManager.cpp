#include "AudioManager.h"
#include <math.h>

bool AudioManager::startWorker() {
  if (!expander) return false;
  for (int i = 0; i < 256; ++i) sine[i] = lroundf(sinf(i * 6.28318530718f / 256) * 26000);
  requests = xQueueCreate(1, sizeof(Request));
  if (!requests) return false;
  if (xTaskCreatePinnedToCore(taskEntry, "timer-audio", 4096, this, 1, &worker, 0) != pdPASS) {
    vQueueDelete(requests); requests = nullptr;
    return false;
  }
  isReady = true;
  Serial.println("[AUDIO] async ready: start=225ms, completion=2600ms");
  return true;
}

void AudioManager::enqueue(Cue cue) {
  if (!isReady || !requests) return;
  portENTER_CRITICAL(&stateMux);
  if (cue == Cue::Click && selected != Cue::None) {
    portEXIT_CRITICAL(&stateMux);
    return; // A page click must not cut off an important timer cue.
  }
  Request request{++generation, cue};
  selected = cue;
  portEXIT_CRITICAL(&stateMux);
  xQueueOverwrite(requests, &request);
}

void AudioManager::playStartTone() { enqueue(Cue::Start); }
void AudioManager::playCompletionChime() { enqueue(Cue::Completion); }
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

bool AudioManager::playCue(const Request &request) {
  static const Segment start[] = {{880, 80}, {0, 25}, {1320, 120}};
  static const Segment complete[] = {
      {784,180},{0,60},{1046,280},{0,180},
      {784,180},{0,60},{1046,280},{0,180},
      {784,180},{0,60},{1046,280},{0,180},{1319,500}};
  static const Segment click[] = {{1500,15}};
  if (request.cue == Cue::Click) return sequence(click, 1, request.id);
  if (request.cue == Cue::Start) return sequence(start, 3, request.id);
  if (request.cue == Cue::Completion) return sequence(complete, 13, request.id);
  if (request.cue == Cue::Test) {
    return sequence(start, 3, request.id) && tone(0, 600, request.id) &&
           sequence(complete, 13, request.id);
  }
  return false;
}

void AudioManager::taskEntry(void *self) { static_cast<AudioManager *>(self)->taskLoop(); }

void AudioManager::taskLoop() {
  Request request{};
  for (;;) {
    if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE || !current(request.id)) continue;
    const char *name = request.cue == Cue::Completion ? "completion" :
        request.cue == Cue::Start ? "start" : request.cue == Cue::Test ? "speaker-test" : "click";
    Serial.printf("[AUDIO] cue=%s begin\n", name);
    expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, HIGH);
    vTaskDelay(pdMS_TO_TICKS(5));
    bool played = playCue(request);
    // I2S write queues DMA data; don't mute the amp while the final note is pending.
    bool drained = played && tone(0, 160, request.id);
    expander->digitalWrite(BOARD_EXPANDER_AMP_CTRL, LOW);
    if (!drained) tone(0, 160, request.id, false); // Clear queued tail while muted.
    portENTER_CRITICAL(&stateMux);
    bool finished = generation == request.id;
    if (finished) selected = Cue::None;
    portEXIT_CRITICAL(&stateMux);
    Serial.printf("[AUDIO] cue=%s %s\n", name, played && drained && finished ? "finished" : "cancelled-or-error");
  }
}

bool AudioManager::init(Arduino_XCA9554SWSPI *exp) {
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
      return startWorker();
    }

    isReady = false;
    return false;
  }
