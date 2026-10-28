#include "DisplayUI.h"

bool DisplayUI::init() {
  expander = new Arduino_XCA9554SWSPI(
    BOARD_EXPANDER_LCD_RST,
    BOARD_EXPANDER_LCD_CS,
    BOARD_EXPANDER_LCD_SCL,
    BOARD_EXPANDER_LCD_SDA,
    &Wire,
    BOARD_EXPANDER_ADDRESS);

  rgbpanel = new Arduino_ESP32RGBPanel(
    BOARD_LCD_DE, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_PCLK,
    BOARD_LCD_R0, BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4,
    BOARD_LCD_G0, BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5,
    BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

  gfx = new Arduino_RGB_Display(
    BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

  if (!gfx->begin()) {
    isReady = false;
    return false;
  }

  isReady = true;
  return true;
}

void DisplayUI::showBootScreen() {
  if (!isReady) return;
  gfx->fillScreen(UI_COLOR_BG);

  // 绘制居中欢迎画面
  gfx->setTextColor(UI_COLOR_FOCUS);
  gfx->setTextSize(4);
  gfx->setCursor(70, 180);
  gfx->println("Flip Pomodoro");

  gfx->setTextColor(UI_COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(120, 240);
  gfx->println("Hardware Initializing...");

  gfx->setTextColor(UI_COLOR_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(140, 280);
  gfx->println("ESP32-S3 4.0\" 480x480");
}

void DisplayUI::fullRedraw(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power) {
  if (!isReady) return;

  gfx->fillScreen(UI_COLOR_BG);

  // 1. 顶部状态栏
  drawStatusBar(imu, power, true);

  // 2. 环形进度条底轨
  gfx->fillArc(240, 230, 160, 148, 0, 360, UI_COLOR_TRACK);

  // 3. 当前进度弧
  uint16_t accentColor = UI_COLOR_FOCUS;
  if (engine.state == STATE_BREAK_RUNNING || engine.state == STATE_BREAK_PAUSED) {
    accentColor = UI_COLOR_BREAK;
  } else if (engine.state == STATE_FOCUS_PAUSED) {
    accentColor = UI_COLOR_PAUSED;
  } else if (engine.state == STATE_COMPLETED) {
    accentColor = UI_COLOR_COMPLETED;
  }
  drawProgressRing(engine.getProgress(), accentColor, true);

  // 4. 中央时钟与状态
  drawCenterClock(engine.getRemainingSeconds(), engine.state, engine.completedPomodoros, true);

  // 5. 底部操作说明卡片
  drawFooterHint(engine.state, engine.pauseReason, true);

  // 记录最后状态
  lastState = engine.state;
  lastSeconds = engine.getRemainingSeconds();
  lastBatteryPercent = power.batteryPercent;
  lastVbusIn = power.vbusIn;
  lastOrientation = imu.currentOrientation;
  lastAlerting = engine.isAlerting;
}

void DisplayUI::update(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power) {
  if (!isReady) return;

  // 如果状态发生切换，或者结束报警闪烁状态改变，进行完整重绘
  if (engine.state != lastState) {
    fullRedraw(engine, imu, power);
    return;
  }

  // 状态栏更新（当电量、供电方式或姿态发生改变）
  if (power.batteryPercent != lastBatteryPercent ||
      power.vbusIn != lastVbusIn ||
      imu.currentOrientation != lastOrientation) {
    drawStatusBar(imu, power, false);
    lastBatteryPercent = power.batteryPercent;
    lastVbusIn = power.vbusIn;
    lastOrientation = imu.currentOrientation;
  }

  // 倒计时时间与进度环更新（当秒数改变时）
  unsigned long remSec = engine.getRemainingSeconds();
  if (remSec != lastSeconds) {
    uint16_t accentColor = UI_COLOR_FOCUS;
    if (engine.state == STATE_BREAK_RUNNING || engine.state == STATE_BREAK_PAUSED) {
      accentColor = UI_COLOR_BREAK;
    } else if (engine.state == STATE_FOCUS_PAUSED) {
      accentColor = UI_COLOR_PAUSED;
    } else if (engine.state == STATE_COMPLETED) {
      accentColor = UI_COLOR_COMPLETED;
    }

    drawProgressRing(engine.getProgress(), accentColor, false);
    drawCenterClock(remSec, engine.state, engine.completedPomodoros, false);
    lastSeconds = remSec;
  }

  // 完成提醒状态时的动态呼吸/闪烁反馈
  if (engine.isAlerting) {
    unsigned long now = millis();
    if (now - lastAlertBlinkTime >= 500) {
      lastAlertBlinkTime = now;
      alertBlinkState = !alertBlinkState;
      uint16_t blinkColor = alertBlinkState ? UI_COLOR_COMPLETED : UI_COLOR_FOCUS;
      gfx->fillRoundRect(140, 116, 200, 34, 8, blinkColor);
      gfx->setTextColor(UI_COLOR_WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(160, 125);
      gfx->print("TIME COMPLETE!");
    }
  }
}

void DisplayUI::drawStatusBar(const IMUSensor &imu, const PowerManager &power, bool force) {
  // 仅局部清除顶部状态栏区域 (Y: 12 - 46)
  gfx->fillRect(20, 12, 440, 36, UI_COLOR_BG);

  // 左侧：电池/电源图标与文字
  gfx->setTextSize(2);
  if (power.vbusIn) {
    // 插入 USB 供电
    gfx->setTextColor(UI_COLOR_BREAK);
    gfx->setCursor(24, 22);
    if (power.charging) {
      gfx->printf("[USB %u%% CHG]", power.batteryPercent > 0 ? power.batteryPercent : 100);
    } else {
      gfx->printf("[USB 5V DC]");
    }
  } else {
    // 纯电池供电
    uint16_t battColor = UI_COLOR_BREAK;
    if (power.batteryPercent < 20) battColor = UI_COLOR_FOCUS;
    else if (power.batteryPercent < 50) battColor = UI_COLOR_PAUSED;

    gfx->setTextColor(battColor);
    gfx->setCursor(24, 22);
    if (power.batteryConnected) {
      gfx->printf("[BATT %d%%]", power.batteryPercent);
    } else {
      gfx->print("[NO BATT]");
    }
  }

  // 中间：标题
  gfx->setTextColor(UI_COLOR_MUTED);
  gfx->setCursor(185, 22);
  gfx->print("FLIP TIMER");

  // 右侧：当前姿态胶囊
  const char *oriText = "平放";
  uint16_t oriColor = UI_COLOR_ACCENT;
  if (imu.currentOrientation == ORIENTATION_FACE_DOWN) {
    oriText = "已盖下";
    oriColor = UI_COLOR_PAUSED;
  } else if (imu.currentOrientation == ORIENTATION_UPRIGHT) {
    oriText = "立式";
    oriColor = UI_COLOR_BREAK;
  } else if (imu.currentOrientation == ORIENTATION_MOVING) {
    oriText = "移动中";
    oriColor = UI_COLOR_MUTED;
  }

  gfx->fillRoundRect(380, 16, 76, 26, 6, UI_COLOR_CARD);
  gfx->drawRoundRect(380, 16, 76, 26, 6, UI_COLOR_BORDER);
  gfx->setTextColor(oriColor);
  gfx->setCursor(390, 22);
  gfx->print(oriText);
}

void DisplayUI::drawProgressRing(float progress, uint16_t color, bool force) {
  int currentDeg = (int)(progress * 360.0f);
  if (currentDeg > 360) currentDeg = 360;
  if (currentDeg < 0) currentDeg = 0;

  if (!force && currentDeg == lastProgressDeg) return;

  // 如果进度减少或重置，需要先恢复底轨
  if (currentDeg < lastProgressDeg || force) {
    gfx->fillArc(240, 230, 160, 148, 0, 360, UI_COLOR_TRACK);
  }

  if (currentDeg > 0) {
    gfx->fillArc(240, 230, 160, 148, 270, 270 + currentDeg, color);
  }
  lastProgressDeg = currentDeg;
}

void DisplayUI::drawCenterClock(unsigned long remainingSec, TimerState state, uint16_t completed, bool force) {
  unsigned long minutes = remainingSec / 60;
  unsigned long seconds = remainingSec % 60;

  // 1. 状态徽章 (Pill Badge)
  uint16_t badgeBg = UI_COLOR_FOCUS;
  const char *badgeText = "FOCUS 专注";
  if (state == STATE_BREAK_RUNNING) {
    badgeBg = UI_COLOR_BREAK;
    badgeText = "BREAK 休息";
  } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
    badgeBg = UI_COLOR_PAUSED;
    badgeText = "PAUSED 暂停";
  } else if (state == STATE_COMPLETED) {
    badgeBg = UI_COLOR_COMPLETED;
    badgeText = "COMPLETE 完成";
  } else if (state == STATE_IDLE) {
    badgeBg = UI_COLOR_CARD;
    badgeText = "READY 就绪";
  }

  gfx->fillRoundRect(150, 116, 180, 34, 8, badgeBg);
  gfx->setTextColor(UI_COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(175, 125);
  gfx->print(badgeText);

  // 2. 核心大数字时钟 (MM:SS) - 仅清除时钟数字矩形区域以防闪烁
  gfx->fillRect(120, 180, 240, 60, UI_COLOR_BG);
  gfx->setTextColor(UI_COLOR_WHITE);
  gfx->setTextSize(6);
  gfx->setCursor(150, 185);
  gfx->printf("%02lu:%02lu", minutes, seconds);

  // 3. 统计副标题
  gfx->fillRect(130, 260, 220, 30, UI_COLOR_BG);
  gfx->setTextColor(UI_COLOR_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(150, 265);
  gfx->printf("番茄 #%u | 完成 %u", completed + 1, completed);
}

void DisplayUI::drawFooterHint(TimerState state, PauseReason reason, bool force) {
  gfx->fillRect(20, 400, 440, 70, UI_COLOR_BG);
  gfx->fillRoundRect(24, 405, 432, 60, 12, UI_COLOR_CARD);
  gfx->drawRoundRect(24, 405, 432, 60, 12, UI_COLOR_BORDER);

  gfx->setTextSize(2);
  if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
    gfx->setTextColor(UI_COLOR_WHITE);
    gfx->setCursor(44, 417);
    gfx->print("翻转设备朝下: 立即暂停");
    gfx->setTextColor(UI_COLOR_MUTED);
    gfx->setCursor(44, 440);
    gfx->print("轻触屏幕或短按按键: 暂停");
  } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
    gfx->setTextColor(UI_COLOR_PAUSED);
    gfx->setCursor(44, 417);
    if (reason == PAUSE_BY_FLIP) {
      gfx->print("已翻转暂停 -> 翻回正面继续");
    } else {
      gfx->print("已手动暂停 -> 点击屏幕继续");
    }
    gfx->setTextColor(UI_COLOR_MUTED);
    gfx->setCursor(44, 440);
    gfx->print("长按 BOOT 键 2 秒: 重置");
  } else if (state == STATE_COMPLETED) {
    gfx->setTextColor(UI_COLOR_COMPLETED);
    gfx->setCursor(44, 417);
    gfx->print("番茄已完成! 恭喜专注!");
    gfx->setTextColor(UI_COLOR_WHITE);
    gfx->setCursor(44, 440);
    gfx->print("翻转或轻触屏幕开始下一阶段");
  } else { // STATE_IDLE
    gfx->setTextColor(UI_COLOR_WHITE);
    gfx->setCursor(44, 417);
    gfx->print("翻转设备或轻触屏幕: 开始专注");
    gfx->setTextColor(UI_COLOR_MUTED);
    gfx->setCursor(44, 440);
    gfx->print("短按 BOOT 键即可开始");
  }
}
