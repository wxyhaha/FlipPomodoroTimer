#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"

// 颜色定义 (RGB565)
#define UI_COLOR_BG           0x0842  // 深蓝夜色背景 (#0A0E17)
#define UI_COLOR_CARD         0x18C5  // 卡片背景 (#131926)
#define UI_COLOR_BORDER       0x2969  // 边框描边
#define UI_COLOR_TRACK        0x1905  // 进度条底轨颜色
#define UI_COLOR_FOCUS        0xF9A4  // 番茄活力红 (#F0483E)
#define UI_COLOR_BREAK        0x0673  // 清新薄荷绿 (#00C853)
#define UI_COLOR_PAUSED       0xFDC0  // 暂停暖金黄 (#FFA000)
#define UI_COLOR_COMPLETED    0x9B1F  // 完成欢庆紫 (#AB47BC)
#define UI_COLOR_WHITE        0xFFFF  // 纯白
#define UI_COLOR_MUTED        0x9CD3  // 辅助弱化字 (#9AA0A6)
#define UI_COLOR_ACCENT       0x247F  // 天蓝点缀

class DisplayUI {
public:
  bool isReady = false;

  bool init();
  void showBootScreen();
  void fullRedraw(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power);
  void update(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power);
  Arduino_XCA9554SWSPI* getExpander() const { return expander; }

private:
  Arduino_XCA9554SWSPI *expander = nullptr;
  Arduino_ESP32RGBPanel *rgbpanel = nullptr;
  Arduino_RGB_Display *gfx = nullptr;

  // 缓存上一次渲染的状态，用于局部刷新防闪烁
  TimerState lastState = STATE_IDLE;
  unsigned long lastSeconds = 999999;
  int lastProgressDeg = -1;
  int lastBatteryPercent = -999;
  bool lastVbusIn = false;
  DeviceOrientation lastOrientation = ORIENTATION_UNKNOWN;
  bool lastAlerting = false;
  unsigned long lastAlertBlinkTime = 0;
  bool alertBlinkState = false;

  void drawStatusBar(const IMUSensor &imu, const PowerManager &power, bool force = false);
  void drawProgressRing(float progress, uint16_t color, bool force = false);
  void drawCenterClock(unsigned long remainingSec, TimerState state, uint16_t completed, bool force = false);
  void drawFooterHint(TimerState state, PauseReason reason, bool force = false);
};
