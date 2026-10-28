#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"
#include "RTCManager.h"
#include "FramePresenter.h"
#define U8G2_USE_LARGE_FONTS
#include "font/u8g2_font_unifont_t_chinese4.h"

enum UIMode { UI_MODE_POMODORO = 0, UI_MODE_CLOCK = 1 };

class DisplayUI {
public:
  bool isReady = false;
  bool init();
  void showBootScreen();
  void setMode(UIMode mode);
  UIMode getMode() const { return currentMode; }
  void fullRedraw(const PomodoroEngine &, const IMUSensor &, const PowerManager &, const RTCManager &);
  void update(const PomodoroEngine &, const IMUSensor &, const PowerManager &, const RTCManager &);
  void setRotation(uint8_t r);
  uint8_t getRotation() const { return currentRotation; }
  Arduino_RGB_Display* getDisplay() const { return display; }
  Arduino_XCA9554SWSPI* getExpander() const { return expander; }
  int checkBadgeClick(int16_t x, int16_t y) const;
  bool isAnimating() const { return rotating; }

private:
  Arduino_XCA9554SWSPI *expander = nullptr;
  Arduino_RGB_Display *display = nullptr;
  Arduino_Canvas *gfx = nullptr;
  UIMode currentMode = UI_MODE_POMODORO;
  uint8_t currentRotation = 0;
  bool needsRedraw = true;
  TimerState lastState = STATE_IDLE;
  unsigned long lastSeconds = ~0UL;
  uint32_t lastDate = 0;
  uint32_t lastClockSecond = ~0UL;
  int lastBattery = -999;
  bool lastVbus = false;
  bool lastBatteryConnected = false;
  uint16_t *ringPixels = nullptr;
  FramePresenter presenter;
  UiRect dirty;
  bool contentReady = false, rotating = false;
  float visualAngle = 0, rotationFrom = 0, rotationTo = 0;
  unsigned long rotationStart = 0, lastAnimationFrame = 0;
  uint16_t animationFrames = 0;
  char mainText[20] = "", smallText[16] = "", secondsText[4] = "";
  int lastLit = -1;
  bool lastPaused = false;
  uint16_t mainColor = 0;

  void render(const PomodoroEngine &, const PowerManager &, const RTCManager &);
  void drawHeader(const PowerManager &);
  void drawHourglass(int x, int y, int size, uint16_t color);
  void drawClockIcon(int x, int y, int size, uint16_t color);
  void drawStatus(TimerState state);
  void prepareRing();
  void drawRing(float remaining, bool paused);
  void drawDigits(const char *text, int x, int y, int width, int height, uint16_t color,
                  char *previous = nullptr, bool force = false);
  int digitTextWidth(const char *text, int width) const;
  void drawCenteredDigits(const char *text, int center, int y, int width, int height, uint16_t color,
                         char *previous = nullptr, bool force = false);
  void drawDate(const RTCManager &);
  void centeredText(const char *text, int baseline, int size, uint16_t color);
};
