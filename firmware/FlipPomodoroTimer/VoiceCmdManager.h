#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "AudioManager.h"

enum VoiceCommand : uint8_t {
  VOICE_CMD_NONE = 0,
  VOICE_CMD_CONFIRM = 1,
  VOICE_CMD_SKIP = 2
};

class VoiceCmdManager {
public:
  bool isReady = false;

  bool begin(AudioManager &audioMgr);
  void setListening(bool enable);
  bool isListening() const { return listening; }
  bool isRecognizing() const { return speechActive; }
  VoiceCommand pollCommand();

private:
  AudioManager *audio = nullptr;
  volatile bool listening = false;
  volatile bool speechActive = false;
  volatile bool resetRequested = false;
  volatile uint8_t pendingCmd = VOICE_CMD_NONE;
  uint8_t es7210Addr = 0;

  bool initES7210();
  bool writeES7210Reg(uint8_t reg, uint8_t val);

  static void feedTaskEntry(void *arg);
  static void detectTaskEntry(void *arg);
  void feedLoop();
  void detectLoop();
};
