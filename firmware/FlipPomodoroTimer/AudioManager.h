#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "ESP_I2S.h"
#include "Arduino_GFX_Library.h"
#include "es8311.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class AudioManager {
public:
  bool isReady = false;
  bool init(Arduino_XCA9554SWSPI *exp);
  void playStartTone();
  void playCompletionChime();
  void playClickTone();
  void playTest();
  void stopCompletion();
  void stop();

private:
  enum class Cue : uint8_t { None, Click, Start, Completion, Test };
  struct Request { uint32_t id; Cue cue; };
  struct Segment { uint16_t frequency, duration; };
  I2SClass i2s;
  Arduino_XCA9554SWSPI *expander = nullptr;
  QueueHandle_t requests = nullptr;
  TaskHandle_t worker = nullptr;
  portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
  uint32_t generation = 0;
  Cue selected = Cue::None;
  int16_t sine[256] = {};

  bool startWorker();
  void enqueue(Cue cue);
  bool current(uint32_t id);
  bool tone(uint16_t frequency, uint16_t duration, uint32_t id, bool checkCancel = true);
  bool sequence(const Segment *segments, size_t count, uint32_t id);
  bool playCue(const Request &request);
  static void taskEntry(void *self);
  void taskLoop();
};
