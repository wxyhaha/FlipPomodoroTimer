#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"
#include "RTCManager.h"

// 启用 U8g2 字体支持
#define U8G2_USE_LARGE_FONTS
#include "font/u8g2_font_unifont_t_chinese4.h"

// 基础颜色定义 (RGB565)
#define UI_COLOR_BG           0x0000  // 纯黑背景 (#000000)
#define UI_COLOR_CARD         0x10A2  // 拟物卡片底色 (#121418)
#define UI_COLOR_BORDER       0x2945  // 卡片边框描边
#define UI_COLOR_WHITE        0xFFFF  // 纯白高亮
#define UI_COLOR_MUTED        0x7BCF  // 弱化文字 (#7A808A)
#define UI_COLOR_ACCENT       0x067F  // 天蓝主题高亮 (#00CEFF)
#define UI_COLOR_ORANGE       0xFD20  // 暖橙高亮 (#FF9100)
#define UI_COLOR_GREEN        0x07E0  // 鲜绿 (#00E676)
#define UI_COLOR_PURPLE       0xD81F  // 霓虹紫 (#D500F9)
#define UI_COLOR_PAUSED       0xFDE0  // 暂停暖金 (#FFC107)

enum UIMode {
  UI_MODE_POMODORO = 0, // 番茄钟/倒计时模式
  UI_MODE_CLOCK    = 1  // 桌面大时钟模式
};

class DisplayUI {
public:
  bool isReady = false;
  UIMode currentMode = UI_MODE_POMODORO;

  bool init();
  void showBootScreen();
  void setMode(UIMode mode) { currentMode = mode; }
  UIMode getMode() const { return currentMode; }

  void fullRedraw(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power, const RTCManager &rtc);
  void update(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power, const RTCManager &rtc);

  void setRotation(uint8_t r);
  uint8_t getRotation() const { return currentRotation; }
  Arduino_RGB_Display* getDisplay() const { return gfx; }
  Arduino_XCA9554SWSPI* getExpander() const { return expander; }

  // 检查触摸坐标是否击中两个模式徽章（-1 表示未击中）
  int checkBadgeClick(int16_t x, int16_t y) const;

private:
  Arduino_XCA9554SWSPI *expander = nullptr;
  Arduino_ESP32RGBPanel *rgbpanel = nullptr;
  Arduino_RGB_Display *gfx = nullptr;

  uint8_t currentRotation = 0;

  // 缓存渲染状态
  TimerState lastState = STATE_IDLE;
  uint8_t lastActiveFace = 0;
  unsigned long lastSeconds = 999999;
  uint16_t lastCompleted = 999;
  int lastActiveSegments = -1;
  int lastBatteryPercent = -999;
  bool lastVbusIn = false;
  DeviceOrientation lastOrientation = ORIENTATION_UNKNOWN;
  char lastRtcTime[16] = "";
  UIMode lastRenderedMode = UI_MODE_POMODORO;
  bool lastHourglassState = false;

  void drawTopBadges(const PowerManager &power, bool force = false);
  void drawRainbowRing(float progress, bool force = false);
  void drawCenterTime(unsigned long remainingSec, const RTCManager &rtc, bool force = false);
  void drawRTCPill(const RTCManager &rtc, bool force = false);
  void drawStatusIcon(TimerState state, bool force = false);
  void drawSubtext(uint16_t completed, uint8_t activeFace, const RTCManager &rtc, bool force = false);
  void drawFooterHint(TimerState state, PauseReason reason, bool force = false);

  uint16_t getSegmentColor(uint8_t idx, bool active) const;
};
