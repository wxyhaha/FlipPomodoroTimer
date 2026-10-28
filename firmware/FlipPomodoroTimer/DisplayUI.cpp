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

  // 启用 U8g2 字体支持与 UTF-8 解码，确保全中文原生清晰排版
  gfx->setFont(u8g2_font_unifont_t_chinese4);
  gfx->setUTF8Print(true);

  isReady = true;
  return true;
}

void DisplayUI::showBootScreen() {
  if (!isReady) return;
  gfx->fillScreen(UI_COLOR_BG);

  // 居中欢迎标题
  gfx->setTextColor(UI_COLOR_FOCUS);
  gfx->setTextSize(3);
  gfx->setCursor(120, 180);
  gfx->print("翻转番茄钟");

  gfx->setTextColor(UI_COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(155, 240);
  gfx->print("硬件系统初始化中...");

  gfx->setTextColor(UI_COLOR_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(135, 280);
  gfx->print("ESP32-S3 · 4.0寸 480x480");
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

  // 4. 中央状态徽章
  drawCenterBadge(engine.state, engine.activeFace, true);

  // 5. 核心时间大数字
  drawCenterClock(engine.getRemainingSeconds(), true);

  // 6. 副标题与统计
  drawSubtitle(engine.completedPomodoros, engine.activeFace, true);

  // 7. 底部操作说明卡片
  drawFooterHint(engine.state, engine.pauseReason, true);

  // 记录最后状态
  lastState = engine.state;
  lastActiveFace = engine.activeFace;
  lastSeconds = engine.getRemainingSeconds();
  lastCompleted = engine.completedPomodoros;
  lastBatteryPercent = power.batteryPercent;
  lastVbusIn = power.vbusIn;
  lastOrientation = imu.currentOrientation;
  lastAlerting = engine.isAlerting;
}

void DisplayUI::update(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power) {
  if (!isReady) return;

  // 状态改变或活跃面改变时，进行微局部平滑重绘（不清除全屏背景，消除点击跳动）
  bool stateChanged = (engine.state != lastState);
  bool faceChanged = (engine.activeFace != lastActiveFace);

  if (stateChanged || faceChanged) {
    uint16_t accentColor = UI_COLOR_FOCUS;
    if (engine.state == STATE_BREAK_RUNNING || engine.state == STATE_BREAK_PAUSED) {
      accentColor = UI_COLOR_BREAK;
    } else if (engine.state == STATE_FOCUS_PAUSED) {
      accentColor = UI_COLOR_PAUSED;
    } else if (engine.state == STATE_COMPLETED) {
      accentColor = UI_COLOR_COMPLETED;
    }

    drawCenterBadge(engine.state, engine.activeFace, true);
    drawProgressRing(engine.getProgress(), accentColor, true);
    drawCenterClock(engine.getRemainingSeconds(), true);
    drawSubtitle(engine.completedPomodoros, engine.activeFace, true);
    drawFooterHint(engine.state, engine.pauseReason, true);

    lastState = engine.state;
    lastActiveFace = engine.activeFace;
    lastSeconds = engine.getRemainingSeconds();
    lastCompleted = engine.completedPomodoros;
  }

  // 顶部状态栏局部更新（当电量、供电方式或姿态发生改变）
  if (power.batteryPercent != lastBatteryPercent ||
      power.vbusIn != lastVbusIn ||
      imu.currentOrientation != lastOrientation) {
    drawStatusBar(imu, power, false);
    lastBatteryPercent = power.batteryPercent;
    lastVbusIn = power.vbusIn;
    lastOrientation = imu.currentOrientation;
  }

  // 倒计时每秒更新（双色直写覆盖，零闪烁）
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
    drawCenterClock(remSec, false);
    lastSeconds = remSec;
  }

  // 完成提醒状态时的温和呼吸反馈（仅闪烁徽章，不刷黑屏幕）
  if (engine.isAlerting) {
    unsigned long now = millis();
    if (now - lastAlertBlinkTime >= 500) {
      lastAlertBlinkTime = now;
      alertBlinkState = !alertBlinkState;
      uint16_t blinkColor = alertBlinkState ? UI_COLOR_COMPLETED : UI_COLOR_FOCUS;
      gfx->fillRoundRect(140, 110, 200, 36, 8, blinkColor);
      gfx->setTextColor(UI_COLOR_WHITE, blinkColor);
      gfx->setTextSize(1);
      gfx->setCursor(185, 134);
      gfx->print("倒计时已完成!");
    }
  }
}

void DisplayUI::drawStatusBar(const IMUSensor &imu, const PowerManager &power, bool force) {
  // 局域清除顶部状态栏背景 (Y: 10 - 46)
  gfx->fillRect(16, 10, 448, 36, UI_COLOR_BG);

  gfx->setTextSize(1);

  // 1. 左侧：电量与供电状态
  if (power.vbusIn) {
    gfx->setTextColor(UI_COLOR_BREAK, UI_COLOR_BG);
    gfx->setCursor(24, 32);
    if (power.charging) {
      gfx->printf("USB供电 · 充电%u%%", power.batteryPercent > 0 ? power.batteryPercent : 100);
    } else {
      gfx->print("USB 5V供电");
    }
  } else {
    uint16_t battColor = UI_COLOR_BREAK;
    if (power.batteryPercent < 20) battColor = UI_COLOR_FOCUS;
    else if (power.batteryPercent < 50) battColor = UI_COLOR_PAUSED;

    gfx->setTextColor(battColor, UI_COLOR_BG);
    gfx->setCursor(24, 32);
    if (power.batteryConnected) {
      gfx->printf("电池供电 %d%%", power.batteryPercent);
    } else {
      gfx->print("未接电池 (USB)");
    }
  }

  // 2. 中间：标题
  gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_BG);
  gfx->setCursor(208, 32);
  gfx->print("番茄时钟");

  // 3. 右侧：姿态指示胶囊 (X: 360 ~ 460)
  uint16_t oriColor = UI_COLOR_ACCENT;
  if (imu.currentOrientation == ORIENTATION_FACE_DOWN) {
    oriColor = UI_COLOR_PAUSED;
  } else if (imu.isTiltedFace()) {
    oriColor = UI_COLOR_BREAK;
  } else if (imu.currentOrientation == ORIENTATION_MOVING) {
    oriColor = UI_COLOR_MUTED;
  }

  gfx->fillRoundRect(360, 16, 96, 24, 6, UI_COLOR_CARD);
  gfx->drawRoundRect(360, 16, 96, 24, 6, UI_COLOR_BORDER);
  gfx->setTextColor(oriColor, UI_COLOR_CARD);
  gfx->setCursor(372, 33);
  gfx->print(imu.getOrientationName());
}

void DisplayUI::drawProgressRing(float progress, uint16_t color, bool force) {
  int currentDeg = (int)(progress * 360.0f);
  if (currentDeg > 360) currentDeg = 360;
  if (currentDeg < 0) currentDeg = 0;

  if (!force && currentDeg == lastProgressDeg) return;

  // 如果进度减少或强制重绘，重绘完整底轨
  if (currentDeg < lastProgressDeg || force) {
    gfx->fillArc(240, 230, 160, 148, 0, 360, UI_COLOR_TRACK);
    if (currentDeg > 0) {
      gfx->fillArc(240, 230, 160, 148, 270, 270 + currentDeg, color);
    }
  } else if (currentDeg > lastProgressDeg) {
    // 增量绘制新进度弧段，绝不重复擦除已有弧段，完全无抖动
    gfx->fillArc(240, 230, 160, 148, 270 + lastProgressDeg, 270 + currentDeg, color);
  }

  lastProgressDeg = currentDeg;
}

void DisplayUI::drawCenterBadge(TimerState state, uint8_t activeFace, bool force) {
  uint16_t badgeBg = UI_COLOR_FOCUS;
  const char *badgeText = "专注计时中";

  if (state == STATE_BREAK_RUNNING) {
    badgeBg = UI_COLOR_BREAK;
    badgeText = "休息进行中";
  } else if (state == STATE_FOCUS_PAUSED) {
    badgeBg = UI_COLOR_PAUSED;
    badgeText = "专注已暂停";
  } else if (state == STATE_BREAK_PAUSED) {
    badgeBg = UI_COLOR_PAUSED;
    badgeText = "休息已暂停";
  } else if (state == STATE_COMPLETED) {
    badgeBg = UI_COLOR_COMPLETED;
    badgeText = "倒计时完成!";
  } else if (state == STATE_IDLE) {
    badgeBg = UI_COLOR_CARD;
    badgeText = "番茄钟待机";
  }

  // 局域重绘徽章胶囊
  gfx->fillRoundRect(140, 110, 200, 36, 8, badgeBg);
  gfx->setTextSize(1);
  gfx->setTextColor(UI_COLOR_WHITE, badgeBg);

  // 居中打印 5 个汉字 (5 * 16 = 80 像素宽)
  gfx->setCursor(200, 134);
  gfx->print(badgeText);
}

void DisplayUI::drawCenterClock(unsigned long remainingSec, bool force) {
  unsigned long minutes = remainingSec / 60;
  unsigned long seconds = remainingSec % 60;

  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", minutes, seconds);

  // 双色直写模式：前景色纯白，背景色深蓝底色
  // 覆盖 160x64 区域，单次绘制完成，无先刷黑再画字的黑屏间隙
  gfx->setTextSize(4);
  gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_BG);
  gfx->setCursor(160, 246);
  gfx->print(buf);
}

void DisplayUI::drawSubtitle(uint16_t completed, uint8_t activeFace, bool force) {
  // 局部清理副标题文本行
  gfx->fillRect(100, 275, 280, 24, UI_COLOR_BG);

  gfx->setTextSize(1);
  gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_BG);

  char buf[48];
  if (activeFace >= 1 && activeFace <= 4) {
    const char *faceNames[] = {"", "面一(5分)", "面二(15分)", "面三(25分)", "面四(45分)"};
    snprintf(buf, sizeof(buf), "%s · 已完成 %u 轮", faceNames[activeFace], completed);
  } else {
    snprintf(buf, sizeof(buf), "已完成 %u 轮专注 · 保持节奏", completed);
  }

  gfx->setCursor(140, 292);
  gfx->print(buf);
}

void DisplayUI::drawFooterHint(TimerState state, PauseReason reason, bool force) {
  // 底部卡片背景
  gfx->fillRoundRect(20, 395, 440, 68, 12, UI_COLOR_CARD);
  gfx->drawRoundRect(20, 395, 440, 68, 12, UI_COLOR_BORDER);

  gfx->setTextSize(1);

  // 第一行：主要动作提示
  if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
    gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
    gfx->setCursor(36, 422);
    gfx->print("轻触屏幕: 暂停计时   翻转盖下: 息屏暂停");
  } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
    gfx->setTextColor(UI_COLOR_PAUSED, UI_COLOR_CARD);
    gfx->setCursor(36, 422);
    if (reason == PAUSE_BY_FLIP) {
      gfx->print("已翻转盖下暂停 -> 翻回正面自动继续");
    } else {
      gfx->print("已手动暂停 -> 轻触屏幕继续计时");
    }
  } else if (state == STATE_COMPLETED) {
    gfx->setTextColor(UI_COLOR_COMPLETED, UI_COLOR_CARD);
    gfx->setCursor(36, 422);
    gfx->print("倒计时已完成! 轻触屏幕开始下一阶段");
  } else { // STATE_IDLE
    gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
    gfx->setCursor(36, 422);
    gfx->print("轻触屏幕开始 · 立起侧面进入独立倒计时");
  }

  // 第二行：按键与操作指南
  gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_CARD);
  gfx->setCursor(36, 448);
  gfx->print("四面时长: 5/15/25/45分    长按BOOT: 复位");
}
