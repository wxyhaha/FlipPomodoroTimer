#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"
#include "RTCManager.h"
#include "StatsManager.h"
#include "ConfigManager.h"
#include "WebPortalManager.h"
#include "FramePresenter.h"
#define U8G2_USE_LARGE_FONTS
#include "font/u8g2_font_unifont_t_chinese4.h"

enum UIMode { UI_MODE_POMODORO = 0, UI_MODE_CLOCK = 1, UI_MODE_STATS = 2 };
enum ClockTheme {
  THEME_CYBER = 0,
  THEME_FLIQLO = 1,
  THEME_NIXIE = 2,
  THEME_PIXEL = 3,
  THEME_HOURGLASS = 4
};

class DisplayUI {
public:
  bool isReady = false;
  bool init();
  void showBootScreen();
  void setMode(UIMode mode);
  void setManualScreenOff(bool off);
  void setPanelOutputOff(bool off, PowerManager &power);
  bool isManualScreenOff() const { return manualScreenOff; }
  bool isPanelOutputOff() const { return panelOutputOff; }
  UIMode getMode() const { return currentMode; }
  void fullRedraw(const PomodoroEngine &, const IMUSensor &, const PowerManager &, const RTCManager &,
                  const StatsManager &, const ConfigManager &, const WebPortalManager &);
  void update(const PomodoroEngine &, const IMUSensor &, const PowerManager &, const RTCManager &,
              const StatsManager &, const ConfigManager &, const WebPortalManager &);
  void setRotation(uint8_t r);
  uint8_t getRotation() const { return currentRotation; }
  Arduino_RGB_Display* getDisplay() const { return display; }
  Arduino_XCA9554SWSPI* getExpander() const { return expander; }
  int checkBadgeClick(int16_t x, int16_t y) const;
  bool checkScreenOffClick(int16_t x, int16_t y) const;
  bool checkPortalButtonClick(int16_t x, int16_t y) const;
  bool checkThemeButtonClick(int16_t x, int16_t y) const;
  bool isAnimating() const { return rotating; }
  void forceRedraw() { needsRedraw = true; }

private:
  Arduino_XCA9554SWSPI *expander = nullptr;
  Arduino_RGB_Display *display = nullptr;
  Arduino_Canvas *gfx = nullptr;
  UIMode currentMode = UI_MODE_POMODORO;
  bool manualScreenOff = false;
  bool panelOutputOff = false;
  uint8_t currentRotation = 0;
  bool needsRedraw = true;
  TimerState lastState = STATE_IDLE;
  unsigned long lastSeconds = ~0UL;
  uint32_t lastDate = 0;
  uint32_t lastClockSecond = ~0UL;
  int lastBattery = -999;
  bool lastVbus = false;
  bool lastTimeValid = false;
  bool lastBatteryConnected = false;
  bool lastCharging = false;
  bool lastNtpSyncing = false;
  uint8_t lastPendingFace = 0;
  PortalMode lastPortalMode = PORTAL_OFF;
  int lastPortalClients = -1;
  uint32_t lastTotalPomodoros = 0;
  uint8_t lastPulseLevel = 255;
  uint8_t lastTheme = 0;
  uint8_t lastPixelStage = 255;

  struct FlipCardAnim {
    bool active = false;
    unsigned long startMs = 0;
    int x = 0, y = 148, w = 88, h = 172;
    char currentChar = '\0';
    uint16_t *oldBuf = nullptr;
    uint16_t *newBuf = nullptr;
  };
  FlipCardAnim flipCards[4];
  unsigned long lastFlipAnimFrame = 0;

  uint16_t *ringPixels = nullptr;
  uint8_t *ringSector = nullptr;
  FramePresenter presenter;
  UiRect dirty;
  bool contentReady = false, rotating = false;
  float visualAngle = 0, rotationFrom = 0, rotationTo = 0;
  unsigned long rotationStart = 0, lastAnimationFrame = 0;
  uint16_t animationFrames = 0;
  char mainText[20] = "", smallText[16] = "", secondsText[4] = "";
  int lastLit = -1;
  bool lastPaused = false;
  bool lastIsBreak = false;
  uint16_t mainColor = 0;

  void render(const PomodoroEngine &, const IMUSensor &, const PowerManager &, const RTCManager &,
              const StatsManager &, const ConfigManager &, const WebPortalManager &);
  void drawHeader(const PowerManager &, const RTCManager &);
  void drawHourglass(int x, int y, int size, uint16_t color);
  void drawClockIcon(int x, int y, int size, uint16_t color);
  void drawStatsIcon(int x, int y, int size, uint16_t color);
  void drawScreenOffIcon(int x, int y, uint16_t color);
  void drawStatus(TimerState state);
  void prepareRing();
  void drawRing(float remaining, bool paused, bool isBreak, uint8_t pulseScale = 255);
  void drawDigits(const char *text, int x, int y, int width, int height, uint16_t color,
                  char *previous = nullptr, bool force = false, uint16_t bgColor = 0x0000);
  int digitTextWidth(const char *text, int width) const;
  void drawCenteredDigits(const char *text, int center, int y, int width, int height, uint16_t color,
                         char *previous = nullptr, bool force = false, uint16_t bgColor = 0x0000);
  void drawDate(const RTCManager &, uint16_t color = 0xD6BA);
  void drawStatsPage(const RTCManager &, const StatsManager &, const ConfigManager &, const WebPortalManager &);
  void drawFliqloCard(int x, int y, int w, int h);
  void renderFliqloCardBuf(uint16_t *buf, int w, int h, char ch, uint16_t fgColor);
  void blitFlipCard(int dstX, int dstY, int w, int h, const uint16_t *buf, uint16_t bgAround = 0x2124);
  void compositeFlipAnim(FlipCardAnim &anim, float t);
  void drawNixieTube(int x, int y, int w, int h);
  void drawNixieDigit(int tubeX, int tubeY, int tubeW, int tubeH, char ch, uint16_t fgColor);
  void drawPixelSprite(int x, int y, int scale, uint8_t stage);
  void drawPixelBigTime(const char *text, int cx, int y, int pxSize, uint16_t fgColor);
  void renderExtraTheme(uint8_t theme, const PomodoroEngine &, const IMUSensor &, const RTCManager &, const ConfigManager &, bool full);
  void centeredText(const char *text, int baseline, int size, uint16_t color);
  void centeredTextInBox(const char *text, int cx, int baseline, int size, uint16_t color);
};
