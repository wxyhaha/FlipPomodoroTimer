#include "DisplayUI.h"

// 48 段彩虹色谱表 (从 12 点方向顺时针: 青蓝 -> 黄橙 -> 翠绿 -> 霓虹紫 -> 回到青蓝)
static const uint16_t RAINBOW_RGB565[48] = {
  // 顶部 (12 点方向): 青蓝高亮
  0x07FF, 0x077F, 0x06BF, 0x05DF, 0x053F, 0x24DF, 0x4C7F, 0x741F,
  0x9BCF, 0xC38F, 0xEB0F, 0xFDC0,
  // 右侧 (3 点方向): 黄橙与金红
  0xFE40, 0xFD80, 0xFCA0, 0xFBC0, 0xFA80, 0xF9C0, 0xF900, 0xF840,
  0xE800, 0xD800, 0xB040, 0x88E0,
  // 底部 (6 点方向): 清新鲜绿
  0x5FE0, 0x37E0, 0x1FE0, 0x07E0, 0x07E4, 0x07E9, 0x07EF, 0x07F6,
  0x07FE, 0x07DF, 0x075F, 0x16DF,
  // 左侧 (9 点方向): 霓虹粉紫
  0x3E1F, 0x6DBF, 0x9D5F, 0xCCDF, 0xF45F, 0xF81F, 0xE81F, 0xD01F,
  0xB81F, 0x801F, 0x481F, 0x101F
};

bool DisplayUI::init() {
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  expander = new Arduino_XCA9554SWSPI(
    BOARD_EXPANDER_LCD_CS, BOARD_EXPANDER_LCD_SCL, BOARD_EXPANDER_LCD_SDA,
    BOARD_EXPANDER_ADDRESS, &Wire);

  rgbpanel = new Arduino_ESP32RGBPanel(
    BOARD_LCD_DE, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_PCLK,
    BOARD_LCD_R0, BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4,
    BOARD_LCD_G0, BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5,
    BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4,
    1 /* hsync_polarity */, 40 /* hsync_front_porch */, 48 /* hsync_pulse_width */, 40 /* hsync_back_porch */,
    1 /* vsync_polarity */, 1 /* vsync_front_porch */, 31 /* vsync_pulse_width */, 13 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 14000000L /* prefer_speed */);

  gfx = new Arduino_RGB_Display(
    BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

  if (!gfx->begin()) {
    isReady = false;
    return false;
  }

  // 启用 U8g2 字体支持与 UTF-8 解码
  gfx->setFont(u8g2_font_unifont_t_chinese4);
  gfx->setUTF8Print(true);
  setRotation(0);

  isReady = true;
  return true;
}

void DisplayUI::setRotation(uint8_t r) {
  if (!isReady || !gfx) return;
  currentRotation = r & 3;
  gfx->setRotation(currentRotation);
}

int DisplayUI::checkBadgeClick(int16_t x, int16_t y) const {
  if (y >= 10 && y <= 65) {
    if (x >= 12 && x <= 135) return UI_MODE_POMODORO;
    if (x >= 345 && x <= 468) return UI_MODE_CLOCK;
  }
  return -1;
}

void DisplayUI::showBootScreen() {
  if (!isReady) return;
  gfx->fillScreen(UI_COLOR_BG);

  gfx->setTextColor(UI_COLOR_ACCENT);
  gfx->setTextSize(3);
  gfx->setCursor(120, 190);
  gfx->print("翻转时钟");

  gfx->setTextColor(UI_COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(140, 246);
  gfx->print("系统与网络授时启动中...");

  gfx->setTextColor(UI_COLOR_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(120, 286);
  gfx->print("ESP32-S3 · 4.0寸 480x480");
}

uint16_t DisplayUI::getSegmentColor(uint8_t idx, bool active) const {
  idx %= 48;
  uint16_t c = RAINBOW_RGB565[idx];
  if (active) return c;

  // 未点亮段保留 20% 暗色微光（模拟高端物理荧光段）
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5) & 0x3F;
  uint8_t b = c & 0x1F;
  r = (r >> 2);
  g = (g >> 2);
  b = (b >> 2);
  return (r << 11) | (g << 5) | b;
}

void DisplayUI::fullRedraw(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power, const RTCManager &rtc) {
  if (!isReady) return;

  // 1. 全屏纯黑打底
  gfx->fillScreen(UI_COLOR_BG);

  // 2. 顶部双交互徽章与电量
  drawTopBadges(power, true);

  // 3. 48 段彩虹分段外环
  float progress = 0.0f;
  if (currentMode == UI_MODE_POMODORO) {
    progress = engine.getProgress();
  } else {
    progress = (float)rtc.getSecond() / 60.0f;
  }
  lastActiveSegments = -1;
  drawRainbowRing(progress, true);

  // 4. 状态沙漏/时钟图标
  drawStatusIcon(engine.state, true);

  // 5. 中央超大时间数字
  drawCenterTime(engine.getRemainingSeconds(), rtc, true);

  // 6. 常驻 RTC 真实时间药丸框
  drawRTCPill(rtc, true);

  // 7. 辅助副标题
  drawSubtext(engine.completedPomodoros, engine.activeFace, rtc, true);

  // 8. 底部操作指引卡片
  drawFooterHint(engine.state, engine.pauseReason, true);

  // 缓存状态
  lastState = engine.state;
  lastActiveFace = engine.activeFace;
  lastSeconds = engine.getRemainingSeconds();
  lastCompleted = engine.completedPomodoros;
  lastBatteryPercent = power.batteryPercent;
  lastVbusIn = power.vbusIn;
  lastOrientation = imu.currentOrientation;
  lastRenderedMode = currentMode;
  strncpy(lastRtcTime, rtc.getTimeString(), sizeof(lastRtcTime));
}

void DisplayUI::update(const PomodoroEngine &engine, const IMUSensor &imu, const PowerManager &power, const RTCManager &rtc) {
  if (!isReady) return;

  // 模式切换时全屏重绘
  if (currentMode != lastRenderedMode) {
    fullRedraw(engine, imu, power, rtc);
    return;
  }

  // 1. 状态或活跃面改变
  bool stateChanged = (engine.state != lastState);
  bool faceChanged = (engine.activeFace != lastActiveFace);
  if (stateChanged || faceChanged) {
    drawStatusIcon(engine.state, true);
    drawCenterTime(engine.getRemainingSeconds(), rtc, true);
    drawSubtext(engine.completedPomodoros, engine.activeFace, rtc, true);
    drawFooterHint(engine.state, engine.pauseReason, true);
    lastState = engine.state;
    lastActiveFace = engine.activeFace;
  }

  // 2. 顶部状态栏更新
  if (power.batteryPercent != lastBatteryPercent || power.vbusIn != lastVbusIn) {
    drawTopBadges(power, false);
    lastBatteryPercent = power.batteryPercent;
    lastVbusIn = power.vbusIn;
  }

  // 3. 彩虹外环与时间更新
  if (currentMode == UI_MODE_POMODORO) {
    unsigned long curSec = engine.getRemainingSeconds();
    if (curSec != lastSeconds) {
      drawCenterTime(curSec, rtc, false);
      drawRainbowRing(engine.getProgress(), false);
      lastSeconds = curSec;
    }
  } else { // UI_MODE_CLOCK
    static uint8_t lastSec = 99;
    uint8_t curSec = rtc.getSecond();
    if (curSec != lastSec) {
      drawCenterTime(0, rtc, false);
      drawRainbowRing((float)curSec / 60.0f, false);
      lastSec = curSec;
    }
  }

  // 4. 真实时钟 RTC 药丸框更新 (每分钟或初始更新)
  if (strcmp(rtc.getTimeString(), lastRtcTime) != 0) {
    drawRTCPill(rtc, false);
    strncpy(lastRtcTime, rtc.getTimeString(), sizeof(lastRtcTime));
  }

  // 5. 倒计时进行中的沙漏微弱呼吸状态
  if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
    bool blink = ((millis() / 500) % 2 == 0);
    if (blink != lastHourglassState) {
      drawStatusIcon(engine.state, false);
      lastHourglassState = blink;
    }
  }
}

void DisplayUI::drawTopBadges(const PowerManager &power, bool force) {
  // 左上角：番茄钟模式徽章
  uint16_t pomoBg = (currentMode == UI_MODE_POMODORO) ? 0x0277 : UI_COLOR_CARD;
  uint16_t pomoBorder = (currentMode == UI_MODE_POMODORO) ? UI_COLOR_ACCENT : UI_COLOR_BORDER;
  uint16_t pomoText = (currentMode == UI_MODE_POMODORO) ? UI_COLOR_WHITE : UI_COLOR_MUTED;

  gfx->fillRoundRect(16, 14, 114, 34, 17, pomoBg);
  gfx->drawRoundRect(16, 14, 114, 34, 17, pomoBorder);
  gfx->setTextSize(1);
  gfx->setTextColor(pomoText, pomoBg);
  gfx->setCursor(28, 30);
  gfx->print("⏳ 番茄钟");

  // 右上角：时钟模式徽章
  uint16_t clockBg = (currentMode == UI_MODE_CLOCK) ? 0x4180 : UI_COLOR_CARD;
  uint16_t clockBorder = (currentMode == UI_MODE_CLOCK) ? UI_COLOR_ORANGE : UI_COLOR_BORDER;
  uint16_t clockText = (currentMode == UI_MODE_CLOCK) ? UI_COLOR_WHITE : UI_COLOR_MUTED;

  gfx->fillRoundRect(350, 14, 114, 34, 17, clockBg);
  gfx->drawRoundRect(350, 14, 114, 34, 17, clockBorder);
  gfx->setTextSize(1);
  gfx->setTextColor(clockText, clockBg);
  gfx->setCursor(372, 30);
  gfx->print("🕒 时钟");

  // 中间：电池指示器
  int battP = power.batteryPercent;
  uint16_t battColor = UI_COLOR_GREEN;
  if (battP <= 20) battColor = UI_COLOR_ORANGE;
  if (battP <= 10) battColor = 0xF800; // 红色

  // 电池外壳
  gfx->drawRoundRect(224, 22, 28, 16, 3, UI_COLOR_MUTED);
  gfx->fillRect(252, 26, 3, 8, UI_COLOR_MUTED);

  // 局部清理电量内部
  gfx->fillRect(226, 24, 24, 12, UI_COLOR_BG);
  int fillW = (battP * 24) / 100;
  if (fillW > 24) fillW = 24;
  if (fillW < 2 && battP > 0) fillW = 2;
  if (fillW > 0) {
    gfx->fillRect(226, 24, fillW, 12, battColor);
  }

  // 如果插了 USB 供电，右上角点缀一个小黄点
  if (power.vbusIn) {
    gfx->fillCircle(216, 30, 3, UI_COLOR_PAUSED);
  } else {
    gfx->fillCircle(216, 30, 3, UI_COLOR_BG);
  }
}

void DisplayUI::drawRainbowRing(float progress, bool force) {
  int activeCount = (int)(progress * 48.0f);
  if (activeCount > 48) activeCount = 48;
  if (activeCount < 0) activeCount = 0;

  if (!force && activeCount == lastActiveSegments) return;

  // 48 段圆环，外半径 218，内半径 196
  const int16_t cx = 240;
  const int16_t cy = 230;
  const int16_t r1 = 218;
  const int16_t r2 = 196;

  if (force || lastActiveSegments < 0) {
    for (uint8_t i = 0; i < 48; ++i) {
      float startA = 270.0f + (float)i * 7.5f;
      float endA = startA + 6.0f;
      uint16_t col = getSegmentColor(i, i < activeCount);
      gfx->fillArc(cx, cy, r1, r2, startA, endA, col);
    }
  } else {
    // 增量精准绘制改变的分段
    if (activeCount > lastActiveSegments) {
      for (int i = lastActiveSegments; i < activeCount; ++i) {
        float startA = 270.0f + (float)i * 7.5f;
        float endA = startA + 6.0f;
        gfx->fillArc(cx, cy, r1, r2, startA, endA, getSegmentColor(i, true));
      }
    } else if (activeCount < lastActiveSegments) {
      for (int i = activeCount; i < lastActiveSegments; ++i) {
        float startA = 270.0f + (float)i * 7.5f;
        float endA = startA + 6.0f;
        gfx->fillArc(cx, cy, r1, r2, startA, endA, getSegmentColor(i, false));
      }
    }
  }

  lastActiveSegments = activeCount;
}

void DisplayUI::drawStatusIcon(TimerState state, bool force) {
  gfx->setTextSize(1);
  gfx->setCursor(232, 138);

  if (currentMode == UI_MODE_CLOCK) {
    gfx->setTextColor(UI_COLOR_ORANGE, UI_COLOR_BG);
    gfx->print("🕒");
  } else {
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      gfx->setTextColor(UI_COLOR_ACCENT, UI_COLOR_BG);
      gfx->print("⏳");
    } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
      gfx->setTextColor(UI_COLOR_PAUSED, UI_COLOR_BG);
      gfx->print("⏸");
    } else if (state == STATE_COMPLETED) {
      gfx->setTextColor(UI_COLOR_GREEN, UI_COLOR_BG);
      gfx->print("★");
    } else {
      gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_BG);
      gfx->print("⌛");
    }
  }
}

void DisplayUI::drawCenterTime(unsigned long remainingSec, const RTCManager &rtc, bool force) {
  char buf[16];
  if (currentMode == UI_MODE_POMODORO) {
    snprintf(buf, sizeof(buf), "%02lu:%02lu", remainingSec / 60, remainingSec % 60);
  } else {
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", rtc.getHour(), rtc.getMinute(), rtc.getSecond());
  }

  gfx->setTextSize(3);
  gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_BG);

  if (currentMode == UI_MODE_POMODORO) {
    // 居中绘制 5 字符 MM:SS (X: 160 ~ 320)
    gfx->setCursor(158, 204);
  } else {
    // 居中绘制 8 字符 HH:MM:SS
    gfx->setCursor(124, 204);
  }
  gfx->print(buf);
}

void DisplayUI::drawRTCPill(const RTCManager &rtc, bool force) {
  // 正下方药丸框: X: 180~300, Y: 248~282, R: 17
  gfx->fillRoundRect(180, 248, 120, 34, 17, UI_COLOR_CARD);
  gfx->drawRoundRect(180, 248, 120, 34, 17, UI_COLOR_BORDER);

  gfx->setTextSize(1);
  gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);

  if (currentMode == UI_MODE_POMODORO) {
    gfx->setCursor(222, 264);
    gfx->print(rtc.getTimeString());
  } else {
    gfx->setCursor(208, 264);
    gfx->print("实时时钟");
  }
}

void DisplayUI::drawSubtext(uint16_t completed, uint8_t activeFace, const RTCManager &rtc, bool force) {
  // 清理副标题文本区域
  gfx->fillRect(70, 305, 340, 26, UI_COLOR_BG);

  gfx->setTextSize(1);
  gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_BG);

  char buf[64];
  if (currentMode == UI_MODE_POMODORO) {
    if (activeFace >= 1 && activeFace <= 4) {
      const char *faceNames[] = {"", "面一(5分)", "面二(10分)", "面三(30分)", "面四(60分)"};
      snprintf(buf, sizeof(buf), "%s · 已完成 %u 轮专注", faceNames[activeFace], completed);
    } else {
      snprintf(buf, sizeof(buf), "已完成 %u 轮专注 · 保持节奏", completed);
    }
  } else {
    snprintf(buf, sizeof(buf), "%04u年%02u月%02u日 星期%u",
             rtc.currentDateTime.getYear(), rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay(),
             rtc.currentDateTime.getWeek());
  }

  gfx->setCursor(130, 322);
  gfx->print(buf);
}

void DisplayUI::drawFooterHint(TimerState state, PauseReason reason, bool force) {
  // 底部拟物卡片 (X: 20 ~ 460, Y: 408 ~ 468)
  gfx->fillRoundRect(20, 408, 440, 60, 12, UI_COLOR_CARD);
  gfx->drawRoundRect(20, 408, 440, 60, 12, UI_COLOR_BORDER);

  gfx->setTextSize(1);

  if (currentMode == UI_MODE_POMODORO) {
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
      gfx->setCursor(36, 428);
      gfx->print("向后翻平: 立即暂停   轻触屏幕: 暂停倒计时");
      gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_CARD);
      gfx->setCursor(36, 452);
      gfx->print("四面时长: 5/10/30/60分   轻触右上角切时钟");
    } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
      gfx->setTextColor(UI_COLOR_PAUSED, UI_COLOR_CARD);
      gfx->setCursor(36, 428);
      if (reason == PAUSE_BY_FLIP) {
        gfx->print("已向后翻平暂停 -> 重新立起原面自动继续");
      } else {
        gfx->print("已手动暂停 -> 轻触屏幕继续倒计时");
      }
      gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_CARD);
      gfx->setCursor(36, 452);
      gfx->print("长按 BOOT 键 1.5 秒: 复位重置计时器");
    } else if (state == STATE_COMPLETED) {
      gfx->setTextColor(UI_COLOR_GREEN, UI_COLOR_CARD);
      gfx->setCursor(36, 428);
      gfx->print("倒计时已完成! 恭喜达成专注目标!");
      gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
      gfx->setCursor(36, 452);
      gfx->print("立起任意面开启新倒计时 或轻触确认");
    } else { // STATE_IDLE
      gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
      gfx->setCursor(36, 428);
      gfx->print("立起任意面开始倒计时 (5/10/30/60分)");
      gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_CARD);
      gfx->setCursor(36, 452);
      gfx->print("向后翻平: 暂停   轻触顶部: 切换模式");
    }
  } else { // UI_MODE_CLOCK
    gfx->setTextColor(UI_COLOR_WHITE, UI_COLOR_CARD);
    gfx->setCursor(36, 428);
    gfx->print("当前为桌面大时钟模式 · 硬件 RTC 持续走时");
    gfx->setTextColor(UI_COLOR_MUTED, UI_COLOR_CARD);
    gfx->setCursor(36, 452);
    gfx->print("轻触左上角徽章或立起面: 立即切回番茄钟");
  }
}
