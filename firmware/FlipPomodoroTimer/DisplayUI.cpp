#include "DisplayUI.h"
#include "DigitFont.h"
#include "ExpanderLock.h"
#include <esp_heap_caps.h>
#include <math.h>

namespace {
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kMuted = 0x8C71;
constexpr uint16_t kCyan = 0x07FF, kGold = 0xFE68, kIce = 0xD6BA;
constexpr int RING_SIZE = 404, RING_X = 38, RING_Y = 58;
constexpr float PI_F = 3.14159265359f;

// Arduino_RGB_Display leaves displayOn/displayOff empty; issue ST7701 DCS commands.
class FlipTimerRGBDisplay final : public Arduino_RGB_Display {
public:
  using Arduino_RGB_Display::Arduino_RGB_Display;
  void displayOn() override { if (_bus) _bus->sendCommand(0x29); }
  void displayOff() override { if (_bus) _bus->sendCommand(0x28); }
};

// Reserve the complete 2D composition in PSRAM, never clear the live screen.
class UiCanvas : public Arduino_Canvas {
public:
  UiCanvas() : Arduino_Canvas(480, 480, nullptr) {}
  bool begin(int32_t = GFX_NOT_DEFINED) override {
    if (!_framebuffer) {
      _framebuffer = static_cast<uint16_t *>(heap_caps_malloc(
          480 * 480 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return _framebuffer != nullptr;
  }
};

uint16_t rgb(int r, int g, int b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

uint16_t shade(uint16_t c, int alpha) {
  return ((((c >> 11) & 31) * alpha / 255) << 11) |
         ((((c >> 5) & 63) * alpha / 255) << 5) |
         ((c & 31) * alpha / 255);
}

uint16_t ringColor(int index) {
  // Clockwise from 12: red, gold, green, cyan; exactly 5 blocks per family.
  static const uint8_t endpoints[4][2][3] = {
    {{255, 90, 104}, {255, 57, 65}},
    {{255, 203, 15}, {255, 224, 40}},
    {{56, 235, 60}, {0, 222, 130}},
    {{0, 216, 222}, {0, 176, 248}}
  };
  int group = index / 5, t = index % 5;
  int c[3];
  for (int k = 0; k < 3; ++k)
    c[k] = (endpoints[group][0][k] * (4 - t) + endpoints[group][1][k] * t) / 4;
  return rgb(c[0], c[1], c[2]);
}

bool paused(TimerState state) {
  return state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED;
}
}

bool DisplayUI::init() {
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
  // Constructor order is rst, cs, sck, mosi, wire, address (not cs first).
  // A warm panel may retain its configuration and hide an incorrect bus setup.
  Wire.beginTransmission(BOARD_EXPANDER_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("[UI] TCA9554 at 0x20 not responding");
    return false;
  }
  expander = new Arduino_XCA9554SWSPI(
      BOARD_EXPANDER_LCD_RST, BOARD_EXPANDER_LCD_CS,
      BOARD_EXPANDER_LCD_SCL, BOARD_EXPANDER_LCD_SDA,
      &Wire, BOARD_EXPANDER_ADDRESS);
  auto *panel = new Arduino_ESP32RGBPanel(
      BOARD_LCD_DE, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_PCLK,
      BOARD_LCD_R0, BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4,
      BOARD_LCD_G0, BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5,
      BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4,
      1, 40, 48, 40, 1, 1, 31, 13, 1, 14000000L, false, 0, 0, 480 * 10);
  panel->enableDoubleBuffer();
  display = new FlipTimerRGBDisplay(
      BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, panel, 0, false,
      expander, GFX_NOT_DEFINED, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));
  if (!display->begin()) return false;
  if (!presenter.begin(panel)) return false;
  gfx = new UiCanvas();
  if (!gfx->begin()) {
    Serial.println("[UI] PSRAM canvas allocation failed");
    return false;
  }
  ringPixels = static_cast<uint16_t *>(heap_caps_malloc(
      RING_SIZE * RING_SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ringPixels) {
    Serial.println("[UI] Ring cache allocation failed");
    return false;
  }
  prepareRing();
  gfx->setFont(u8g2_font_unifont_t_chinese4);
  gfx->setUTF8Print(true);
  isReady = true;
  setRotation(0);
  Serial.println("[UI] V6.2: native glyphs, frame-release double buffer, 2D rotation, panel display control");
  return true;
}

void DisplayUI::setMode(UIMode mode) {
  if (currentMode != mode) { currentMode = mode; needsRedraw = true; }
}

void DisplayUI::setManualScreenOff(bool off) {
  if (manualScreenOff != off) {
    manualScreenOff = off;
    needsRedraw = true;
  }
}

void DisplayUI::setPanelOutputOff(bool off, PowerManager &power) {
  if (!display || panelOutputOff == off) return;
  ExpanderLock lock;
  if (off) {
    display->displayOff();
    power.dimForFlipDown();
  } else {
    display->displayOn();
    power.restoreFromFlipUp();
  }
  panelOutputOff = off;
  needsRedraw = true;
}

void DisplayUI::setRotation(uint8_t r) {
  r &= 3;
  if (!contentReady || manualScreenOff) {
    currentRotation = r; visualAngle = r * 90.0f; needsRedraw = true;
    return;
  }
  if (r == currentRotation) return;
  currentRotation = r;
  if (!rotating && !presenter.prepareAnimation(gfx->getFramebuffer())) {
    visualAngle = r * 90.0f; needsRedraw = true;
    Serial.println("[UI] animation cache unavailable; use stable native frame");
    return;
  }
  rotationFrom = visualAngle;
  float delta = remainderf(r * 90.0f - visualAngle, 360.0f);
  rotationTo = visualAngle + delta;
  rotationStart = millis(); lastAnimationFrame = rotationStart - 16;
  animationFrames = 0; rotating = true;
}

int DisplayUI::checkBadgeClick(int16_t x, int16_t y) const {
  // 56px hit areas are larger than the visible icons.
  if (y >= 8 && y < 64) {
    if (x >= 16 && x < 72) return UI_MODE_POMODORO;
    if (x >= 408 && x < 464) return UI_MODE_CLOCK;
  }
  return -1;
}

bool DisplayUI::checkScreenOffClick(int16_t x, int16_t y) const {
  return y >= 416 && y < 472 && x >= 8 && x < 64;
}

void DisplayUI::centeredText(const char *text, int baseline, int size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x, y; uint16_t w, h;
  gfx->getTextBounds(text, 0, baseline, &x, &y, &w, &h);
  gfx->setCursor((480 - static_cast<int>(w)) / 2 - x, baseline);
  gfx->print(text);
}

void DisplayUI::showBootScreen() {
  if (!isReady) return;
  gfx->fillScreen(kBlack);
  drawHourglass(240, 162, 38, kCyan);
  centeredText("翻转番茄钟", 245, 2, kWhite);
  centeredText("正在校时…", 292, 1, kMuted);
  UiRect frame; frame.full();
  presenter.present(gfx->getFramebuffer(), currentRotation, frame);
}

void DisplayUI::drawHourglass(int x, int y, int size, uint16_t color) {
  int w = size * 2 / 3, top = y - size / 2, bottom = y + size / 2;
  for (int d = 0; d < 2; ++d) {
    gfx->drawFastHLine(x - w / 2, top + d, w + 1, color);
    gfx->drawFastHLine(x - w / 2, bottom - d, w + 1, color);
    gfx->drawLine(x - w / 2 + d, top + 2, x + w / 2 - d, bottom - 2, color);
    gfx->drawLine(x + w / 2 - d, top + 2, x - w / 2 + d, bottom - 2, color);
  }
  gfx->fillTriangle(x, y + 4, x - w / 3, bottom - 3, x + w / 3, bottom - 3, color);
}

void DisplayUI::drawClockIcon(int x, int y, int size, uint16_t color) {
  gfx->drawCircle(x, y, size / 2, color);
  gfx->drawCircle(x, y, size / 2 - 1, color);
  gfx->fillRoundRect(x - 1, y - size / 3, 3, size / 3 + 2, 1, color);
  gfx->fillRoundRect(x, y - 1, size / 4 + 1, 3, 1, color);
}

void DisplayUI::drawScreenOffIcon(int x, int y, uint16_t color) {
  // A small display outline keeps this control distinct from the physical power key.
  gfx->drawRoundRect(x - 12, y - 10, 24, 18, 3, color);
  gfx->drawFastHLine(x - 7, y + 11, 14, color);
  gfx->drawFastVLine(x, y + 8, 3, color);
}

void DisplayUI::drawHeader(const PowerManager &power) {
  bool timer = currentMode == UI_MODE_POMODORO;
  gfx->fillCircle(44, 36, 20, timer ? 0x0125 : 0x1082);
  gfx->fillCircle(436, 36, 20, timer ? 0x1082 : 0x0125);
  drawHourglass(44, 36, 24, timer ? kCyan : kMuted);
  drawClockIcon(436, 36, 26, timer ? kMuted : kCyan);
  // Reserve a fixed-width percentage area so 9%, 98%, and 100% do not shift the icon.
  const bool known = power.batteryConnected && power.batteryPercent >= 0;
  uint16_t color = known && power.batteryPercent <= 20 ? kGold : kIce;
  gfx->drawRoundRect(196, 26, 40, 20, 3, color);
  gfx->fillRect(236, 32, 4, 8, color);
  if (known) {
    int bars = constrain((power.batteryPercent + 24) / 25, 0, 4);
    for (int i = 0; i < bars; ++i) gfx->fillRect(200 + i * 8, 30, 6, 12, shade(color, power.charging ? 80 : 255));
  } else {
    gfx->drawFastHLine(209, 36, 14, kMuted);
  }
  if (known && power.charging) {
    // Dark outline keeps the cyan bolt visible over a filled battery.
    gfx->fillRect(211, 28, 10, 16, kBlack);
    gfx->fillTriangle(217, 28, 211, 37, 216, 37, kCyan);
    gfx->fillTriangle(216, 44, 221, 34, 216, 34, kCyan);
  }
  // Compact seven-segment numerals match the main display's visual language.
  static const uint8_t segments[10] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
  char label[5];
  if (known) snprintf(label, sizeof(label), "%d%%", constrain(power.batteryPercent, 0, 100));
  else strcpy(label, "--%");
  int x = 248;
  for (const char *p = label; *p; ++p, x += 11) {
    if (*p >= '0' && *p <= '9') {
      uint8_t bits = segments[*p - '0'];
      if (bits & 0x01) gfx->fillRect(x + 1, 29, 6, 2, color);
      if (bits & 0x02) gfx->fillRect(x + 7, 30, 2, 6, color);
      if (bits & 0x04) gfx->fillRect(x + 7, 37, 2, 6, color);
      if (bits & 0x08) gfx->fillRect(x + 1, 42, 6, 2, color);
      if (bits & 0x10) gfx->fillRect(x, 37, 2, 6, color);
      if (bits & 0x20) gfx->fillRect(x, 30, 2, 6, color);
      if (bits & 0x40) gfx->fillRect(x + 1, 35, 6, 2, color);
    } else if (*p == '-') {
      gfx->fillRect(x + 1, 35, 6, 2, kMuted);
    } else {
      gfx->drawCircle(x + 2, 31, 1, known ? color : kMuted);
      gfx->drawLine(x, 43, x + 8, 29, known ? color : kMuted);
      gfx->drawCircle(x + 6, 42, 1, known ? color : kMuted);
    }
  }
  if (power.vbusIn) gfx->fillCircle(185, 36, 2, kCyan);
}

void DisplayUI::prepareRing() {
  // 2x2 subpixel coverage and rounded sector corners, cached once at boot.
  for (int y = 0; y < RING_SIZE; ++y) {
    for (int x = 0; x < RING_SIZE; ++x) {
      float dx = x - 201.5f, dy = y - 201.5f;
      float radius = sqrtf(dx * dx + dy * dy);
      uint16_t color = kBlack;
      if (radius >= 177 && radius <= 201) {
        float angle = atan2f(dy, dx) * 180 / PI_F + 90;
        if (angle < 0) angle += 360;
        int index = static_cast<int>(angle / 18) % 20;
        int coverage = 0;
        for (int sy = 0; sy < 2; ++sy) for (int sx = 0; sx < 2; ++sx) {
          float px = dx + (sx ? .25f : -.25f), py = dy + (sy ? .25f : -.25f);
          float r = sqrtf(px * px + py * py);
          float a = atan2f(py, px) * 180 / PI_F + 90;
          if (a < 0) a += 360;
          float local = fmodf(a, 18);
          float dr = fmaxf(178 - r, r - 200);
          float da = (1 - fminf(local, 18 - local)) * r * PI_F / 180;
          float u = dr + 1.3f, v = da + 1.3f;
          float distance = sqrtf(fmaxf(u, 0) * fmaxf(u, 0) + fmaxf(v, 0) * fmaxf(v, 0))
                           + fminf(fmaxf(u, v), 0) - 1.3f;
          if (distance <= 0) ++coverage;
        }
        color = shade(ringColor(index), coverage * 255 / 4);
      }
      ringPixels[y * RING_SIZE + x] = color;
    }
  }
}

void DisplayUI::drawRing(float remaining, bool isPaused) {
  int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
  uint16_t *pixels = gfx->getFramebuffer();
  for (int y = 0; y < RING_SIZE; ++y) for (int x = 0; x < RING_SIZE; ++x) {
    uint16_t c = ringPixels[y * RING_SIZE + x];
    if (!c) continue;
    float angle = atan2f(y - 201.5f, x - 201.5f) * 180 / PI_F + 90;
    if (angle < 0) angle += 360;
    int index = static_cast<int>(angle / 18) % 20;
    int brightness = index < lit ? (isPaused ? 175 : 255) : 40;
    pixels[(y + RING_Y) * 480 + x + RING_X] = shade(c, brightness);
  }
}

int DisplayUI::digitTextWidth(const char *text, int width) const {
  int total = 0;
  for (const char *p = text; *p; ++p) {
    if (p != text) total += width / 10;
    total += *p == ':' ? width / 3 : width;
  }
  return total;
}

void DisplayUI::drawCenteredDigits(const char *text, int center, int y, int w, int h,
    uint16_t color, char *previous, bool force) {
  drawDigits(text, center - digitTextWidth(text, w) / 2, y, w, h, color, previous, force);
}

void DisplayUI::drawDigits(const char *text, int x, int y, int w, int h,
    uint16_t color, char *previous, bool force) {
  const uint8_t *atlas = nullptr;
  if (w == 64 && h == 104) atlas = &DIGITS_64_104[0][0];
  else if (w == 70 && h == 116) atlas = &DIGITS_70_116[0][0];
  else if (w == 24 && h == 40) atlas = &DIGITS_24_40[0][0];
  else if (w == 23 && h == 39) atlas = &DIGITS_23_39[0][0];
  size_t oldLength = previous ? strlen(previous) : 0;
  for (size_t i = 0; text[i]; ++i) {
    char value = text[i];
    int cw = value == ':' ? w / 3 : w;
    bool changed = force || !previous || i >= oldLength || previous[i] != value;
    if (changed) {
      gfx->fillRect(x, y, cw, h, kBlack);
      dirty.include(x, y, cw, h);
      if (value == ':') {
        gfx->fillCircle(x + cw / 2, y + h / 3, max(2, w / 14), color);
        gfx->fillCircle(x + cw / 2, y + h * 2 / 3, max(2, w / 14), color);
      } else if (atlas && value >= '0' && value <= '9') {
        const uint8_t *mask = atlas + (value - '0') * w * h;
        for (int py = 0; py < h; ++py) for (int px = 0; px < w; ++px) {
          int alpha = mask[py * w + px];
          if (alpha) gfx->drawPixel(x + px, y + py, shade(color, alpha));
        }
      }
    }
    x += cw + w / 10;
  }
  if (previous) strcpy(previous, text);
}

void DisplayUI::drawStatus(TimerState state) {
  if (paused(state)) {
    gfx->fillRoundRect(230, 138, 7, 28, 2, kGold);
    gfx->fillRoundRect(243, 138, 7, 28, 2, kGold);
  } else if (state == STATE_COMPLETED) {
    gfx->drawLine(227, 152, 236, 162, kCyan);
    gfx->drawLine(236, 162, 253, 140, kCyan);
  } else drawHourglass(240, 152, 28, kIce);
}

void DisplayUI::drawDate(const RTCManager &rtc) {
  if (!rtc.timeValid) {
    centeredText("时钟未就绪", 350, 2, kMuted);
    return;
  }
  char date[48];
  snprintf(date, sizeof(date), "%04u年%02u月%02u日", rtc.currentDateTime.getYear(),
           rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay());
  centeredText(date, 350, 2, kIce);
  const char *weekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
  uint8_t week = rtc.currentDateTime.getWeek();
  centeredText(week < 7 ? weekdays[week] : "星期未知", 415, 3, kIce);
}

void DisplayUI::render(const PomodoroEngine &engine, const PowerManager &power, const RTCManager &rtc) {
  bool full = needsRedraw;
  dirty = UiRect();
  if (full) {
    gfx->fillScreen(kBlack); dirty.full();
    mainText[0] = smallText[0] = secondsText[0] = 0;
    lastLit = -1;
  }
  if (full || lastBattery != power.batteryPercent || lastVbus != power.vbusIn ||
      lastBatteryConnected != power.batteryConnected || lastCharging != power.charging) {
    gfx->fillRect(0, 8, 480, 48, kBlack);
    drawHeader(power); dirty.include(0, 8, 480, 48);
  }
  char time[20];
  if (currentMode == UI_MODE_POMODORO) {
    bool isPaused = paused(engine.state);
    float remaining = 1.0f - engine.getProgress();
    int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
    if (full || lit != lastLit || isPaused != lastPaused) {
      drawRing(remaining, isPaused); dirty.include(RING_X, RING_Y, RING_SIZE, RING_SIZE);
      lastLit = lit; lastPaused = isPaused;
    }
    if (full || lastState != engine.state) {
      gfx->fillRect(217, 132, 46, 40, kBlack);
      drawStatus(engine.state); dirty.include(217, 132, 46, 40);
    }
    unsigned long sec = engine.getRemainingSeconds();
    snprintf(time, sizeof(time), "%02lu:%02lu", sec / 60, sec % 60);
    uint16_t color = isPaused ? 0xFFBC : kWhite;
    drawCenteredDigits(time, 240, 208, 64, 104, color, mainText, full || color != mainColor);
    mainColor = color;
    if (rtc.timeValid) drawCenteredDigits(rtc.getTimeString(), 240, 342, 24, 40, kIce, smallText, full);
    else if (full) { centeredText("--:--", 380, 2, kMuted); dirty.include(180,342,120,40); }
  } else {
    snprintf(time, sizeof(time), "%02u:%02u", rtc.getHour(), rtc.getMinute());
    int width = digitTextWidth(time, 70), secondsWidth = digitTextWidth("00", 23);
    int left = (480 - width - 12 - secondsWidth) / 2;
    if (rtc.timeValid) {
      drawDigits(time, left, 165, 70, 116, kWhite, mainText, full);
      snprintf(time, sizeof(time), "%02u", rtc.getSecond());
      drawDigits(time, left + width + 12, 165, 23, 39, kIce, secondsText, full);
    } else if (full) { centeredText("--:--", 260, 4, kMuted); dirty.include(100,180,280,100); }
    uint32_t date = rtc.currentDateTime.getYear() * 10000UL +
        rtc.currentDateTime.getMonth() * 100UL + rtc.currentDateTime.getDay();
    if (full || lastDate != date) {
      gfx->fillRect(0, 310, 480, 125, kBlack);
      drawDate(rtc); dirty.include(0, 310, 480, 125);
    }
  }
  if (full) {
    gfx->fillCircle(36, 444, 22, 0x0125);
    gfx->drawCircle(36, 444, 22, kCyan);
    drawScreenOffIcon(36, 444, kIce);
    dirty.include(8, 416, 56, 56);
  }
  presenter.present(gfx->getFramebuffer(), currentRotation, dirty);
  contentReady = true;
}

void DisplayUI::fullRedraw(const PomodoroEngine &e, const IMUSensor &imu, const PowerManager &p, const RTCManager &r) {
  needsRedraw = true;
  update(e, imu, p, r);
}

void DisplayUI::update(const PomodoroEngine &e, const IMUSensor &, const PowerManager &p, const RTCManager &r) {
  if (!isReady) return;
  presenter.report();
  if (manualScreenOff) return;
  if (rotating) {
    unsigned long now = millis(), elapsed = now - rotationStart;
    if (elapsed >= 240) {
      visualAngle = currentRotation * 90.0f;
      UiRect frame; frame.full();
      presenter.present(gfx->getFramebuffer(), currentRotation, frame);
      presenter.endAnimation();
      rotating = false;
      Serial.printf("[UI] rotate complete %u deg; frames=%u; duration=%lu ms\n",
          currentRotation * 90, animationFrames + 1, millis() - rotationStart);
    } else if (now - lastAnimationFrame >= 16) {
      lastAnimationFrame = now;
      float t = elapsed / 240.0f, ease = t * t * (3 - 2 * t);
      visualAngle = rotationFrom + (rotationTo - rotationFrom) * ease;
      presenter.presentRotated(gfx->getFramebuffer(), visualAngle);
      ++animationFrames;
    }
    return; // The timer engine still ticks; the animation never delays it by design.
  }
  if (lastTimeValid != r.timeValid) needsRedraw = true;
  uint32_t second = r.getHour() * 3600UL + r.getMinute() * 60UL + r.getSecond();
  uint32_t date = r.currentDateTime.getYear() * 10000UL + r.currentDateTime.getMonth() * 100UL + r.currentDateTime.getDay();
  unsigned long remaining = e.getRemainingSeconds();
  bool changed = needsRedraw || lastState != e.state || lastBattery != p.batteryPercent ||
      lastBatteryConnected != p.batteryConnected || lastCharging != p.charging || lastVbus != p.vbusIn || lastDate != date ||
      (currentMode == UI_MODE_CLOCK ? lastClockSecond != second :
       (lastSeconds != remaining || lastClockSecond / 60 != second / 60));
  if (!changed) return;
  render(e, p, r);
  lastState = e.state; lastBattery = p.batteryPercent; lastVbus = p.vbusIn;
  lastBatteryConnected = p.batteryConnected; lastCharging = p.charging; lastDate = date; lastTimeValid = r.timeValid;
  lastSeconds = remaining; lastClockSecond = second; needsRedraw = false;
}
