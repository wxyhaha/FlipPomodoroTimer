#include "DisplayUI.h"
#include "DigitFont.h"
#include "NixieFont.h"
#include "FliqloFont.h"
#include "I2CLock.h"
#include <esp_heap_caps.h>
#include <math.h>

namespace {
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kMuted = 0x8C71;
constexpr uint16_t kCyan = 0x07FF, kGold = 0xFE68, kIce = 0xD6BA;
constexpr uint16_t kMint = 0x3FE9, kCardBg = 0x10A3, kCardBd = 0x2987;
constexpr uint16_t kFliqloBg = 0x2966, kFliqloBd = 0x5AEB, kFliqloHinge = 0x9CD3;
constexpr uint16_t kNixieBg = 0x1040, kNixieGlow = 0xFC82, kNixieHi = 0xFE68, kNixieBd = 0x61C0;
constexpr uint16_t kPixelRed = 0xF945, kPixelGreen = 0x3F07, kPixelWood = 0x8AC6;
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

inline uint16_t rgb(int r, int g, int b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

inline uint16_t shade(uint16_t c, int alpha) {
  return ((((c >> 11) & 31) * alpha / 255) << 11) |
         ((((c >> 5) & 63) * alpha / 255) << 5) |
         ((c & 31) * alpha / 255);
}

inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha) {
  if (alpha == 0) return bg;
  if (alpha == 255) return fg;
  int r = (((bg >> 11) & 31) * (255 - alpha) + ((fg >> 11) & 31) * alpha) / 255;
  int g = (((bg >> 5) & 63) * (255 - alpha) + ((fg >> 5) & 63) * alpha) / 255;
  int b = ((bg & 31) * (255 - alpha) + (fg & 31) * alpha) / 255;
  return (r << 11) | (g << 5) | b;
}

uint16_t ringColor(int index) {
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

bool isBreakState(TimerState state) {
  return state == STATE_BREAK_RUNNING || state == STATE_BREAK_PAUSED;
}
}

bool DisplayUI::init() {
  I2CLock lock;
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
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
  ringSector = static_cast<uint8_t *>(heap_caps_malloc(
      RING_SIZE * RING_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ringPixels || !ringSector) {
    Serial.println("[UI] Ring cache allocation failed");
    return false;
  }
  const size_t flipBytes = 88 * 172 * sizeof(uint16_t);
  for (int i = 0; i < 4; ++i) {
    flipCards[i].oldBuf = static_cast<uint16_t *>(heap_caps_malloc(flipBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    flipCards[i].newBuf = static_cast<uint16_t *>(heap_caps_malloc(flipBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  prepareRing();
  gfx->setFont(u8g2_font_unifont_t_chinese4);
  gfx->setUTF8Print(true);
  isReady = true;
  setRotation(0);
  Serial.println("[UI] V7.0: zero-trig ring, direct digit blit, PWM backlight, stats & portal UI");
  return true;
}

void DisplayUI::setMode(UIMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    needsRedraw = true;
  }
}

void DisplayUI::setManualScreenOff(bool off) {
  if (manualScreenOff != off) {
    manualScreenOff = off;
    needsRedraw = true;
  }
}

void DisplayUI::setPanelOutputOff(bool off, PowerManager &power) {
  if (!display || panelOutputOff == off) return;
  I2CLock lock;
  if (off) {
    display->displayOff();
    power.dimForFlipDown();
  } else {
    display->displayOn();
    power.restoreFromFlipUp();
  }
  panelOutputOff = off;
  if (!off) needsRedraw = true;
}

void DisplayUI::setRotation(uint8_t r) {
  r &= 3;
  if (!contentReady || manualScreenOff || panelOutputOff) {
    currentRotation = r;
    visualAngle = r * 90.0f;
    needsRedraw = true;
    return;
  }
  if (r == currentRotation) return;
  currentRotation = r;
  if (!rotating && !presenter.prepareAnimation(gfx->getFramebuffer())) {
    visualAngle = r * 90.0f;
    needsRedraw = true;
    Serial.println("[UI] animation cache unavailable; use stable native frame");
    return;
  }
  rotationFrom = visualAngle;
  float delta = remainderf(r * 90.0f - visualAngle, 360.0f);
  rotationTo = visualAngle + delta;
  rotationStart = millis();
  lastAnimationFrame = rotationStart - 16;
  animationFrames = 0;
  rotating = true;
}

int DisplayUI::checkBadgeClick(int16_t x, int16_t y) const {
  if (y >= 8 && y < 64) {
    if (x >= 16 && x < 72) return UI_MODE_POMODORO;
    if (x >= 82 && x < 138) return UI_MODE_STATS;
    if (x >= 408 && x < 464) return UI_MODE_CLOCK;
  }
  return -1;
}

bool DisplayUI::checkScreenOffClick(int16_t x, int16_t y) const {
  return y >= 416 && y < 472 && x >= 8 && x < 64;
}

bool DisplayUI::checkPortalButtonClick(int16_t x, int16_t y) const {
  if (currentMode != UI_MODE_STATS) return false;
  return x >= 90 && x < 390 && y >= 390 && y < 448;
}

bool DisplayUI::checkThemeButtonClick(int16_t x, int16_t y) const {
  if (currentMode != UI_MODE_STATS) return false;
  return x >= 290 && x < 448 && y >= 68 && y < 106;
}

void DisplayUI::centeredText(const char *text, int baseline, int size, uint16_t color) {
  centeredTextInBox(text, 240, baseline, size, color);
}

void DisplayUI::centeredTextInBox(const char *text, int cx, int baseline, int size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x, y;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, baseline, &x, &y, &w, &h);
  gfx->setCursor(cx - static_cast<int>(w) / 2 - x, baseline);
  gfx->print(text);
}

void DisplayUI::showBootScreen() {
  if (!isReady) return;
  gfx->fillScreen(kBlack);
  drawHourglass(240, 162, 38, kCyan);
  centeredText("翻转番茄钟", 245, 2, kWhite);
  centeredText("正在就绪…", 292, 1, kMuted);
  UiRect frame;
  frame.full();
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

void DisplayUI::drawStatsIcon(int x, int y, int size, uint16_t color) {
  int w = max(3, size / 5);
  int base = y + size / 2 - 2;
  gfx->fillRoundRect(x - size / 2 + 2, base - size / 3, w, size / 3, 1, color);
  gfx->fillRoundRect(x - w / 2, base - size * 4 / 5, w, size * 4 / 5, 1, color);
  gfx->fillRoundRect(x + size / 2 - 2 - w, base - size / 2, w, size / 2, 1, color);
}

void DisplayUI::drawScreenOffIcon(int x, int y, uint16_t color) {
  gfx->drawRoundRect(x - 12, y - 10, 24, 18, 3, color);
  gfx->drawFastHLine(x - 7, y + 11, 14, color);
  gfx->drawFastVLine(x, y + 8, 3, color);
}

void DisplayUI::drawHeader(const PowerManager &power, const RTCManager &rtc) {
  bool timer = currentMode == UI_MODE_POMODORO;
  bool statsMode = currentMode == UI_MODE_STATS;
  bool clockMode = currentMode == UI_MODE_CLOCK;

  gfx->fillCircle(44, 36, 20, timer ? 0x0125 : 0x1082);
  gfx->fillCircle(110, 36, 20, statsMode ? 0x0125 : 0x1082);
  gfx->fillCircle(436, 36, 20, clockMode ? 0x0125 : 0x1082);

  drawHourglass(44, 36, 24, timer ? kCyan : kMuted);
  drawStatsIcon(110, 36, 22, statsMode ? kCyan : kMuted);
  drawClockIcon(436, 36, 26, clockMode ? kCyan : kMuted);

  const bool known = power.batteryConnected && power.batteryPercent >= 0;
  uint16_t color = known && power.batteryPercent <= 20 ? kGold : kIce;
  gfx->drawRoundRect(196, 26, 40, 20, 3, color);
  gfx->fillRect(236, 32, 4, 8, color);
  if (known) {
    int bars = constrain((power.batteryPercent + 24) / 25, 0, 4);
    for (int i = 0; i < bars; ++i)
      gfx->fillRect(200 + i * 8, 30, 6, 12, shade(color, power.charging ? 80 : 255));
  } else {
    gfx->drawFastHLine(209, 36, 14, kMuted);
  }
  if (known && power.charging) {
    gfx->fillRect(211, 28, 10, 16, kBlack);
    gfx->fillTriangle(217, 28, 211, 37, 216, 37, kCyan);
    gfx->fillTriangle(216, 44, 221, 34, 216, 34, kCyan);
  }

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
  if (rtc.syncInProgress) gfx->fillCircle(304, 36, 3, kGold);
}

void DisplayUI::prepareRing() {
  for (int y = 0; y < RING_SIZE; ++y) {
    for (int x = 0; x < RING_SIZE; ++x) {
      float dx = x - 201.5f, dy = y - 201.5f;
      float radius = sqrtf(dx * dx + dy * dy);
      uint16_t color = kBlack;
      uint8_t sector = 255;
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
        if (color != kBlack) sector = static_cast<uint8_t>(index);
      }
      int pos = y * RING_SIZE + x;
      ringPixels[pos] = color;
      ringSector[pos] = sector;
    }
  }
}

void DisplayUI::drawRing(float remaining, bool isPaused, bool isBreak, uint8_t pulseScale) {
  int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
  uint16_t *pixels = gfx->getFramebuffer();
  const int activeBright = isPaused ? 175 : pulseScale;
  for (int y = 0; y < RING_SIZE; ++y) {
    uint16_t *dstRow = pixels + (y + RING_Y) * 480 + RING_X;
    const uint16_t *srcRow = ringPixels + y * RING_SIZE;
    const uint8_t *secRow = ringSector + y * RING_SIZE;
    for (int x = 0; x < RING_SIZE; ++x) {
      uint8_t sector = secRow[x];
      if (sector == 255) continue;
      uint16_t c = isBreak ? kMint : srcRow[x];
      int brightness = (pulseScale != 255) ? pulseScale : (sector < lit ? activeBright : 40);
      dstRow[x] = shade(c, brightness);
    }
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
    uint16_t color, char *previous, bool force, uint16_t bgColor) {
  drawDigits(text, center - digitTextWidth(text, w) / 2, y, w, h, color, previous, force, bgColor);
}

void DisplayUI::drawDigits(const char *text, int x, int y, int w, int h,
    uint16_t color, char *previous, bool force, uint16_t bgColor) {
  const uint8_t *atlas = nullptr;
  if (w == 64 && h == 104) atlas = &DIGITS_64_104[0][0];
  else if (w == 70 && h == 116) atlas = &DIGITS_70_116[0][0];
  else if (w == 24 && h == 40) atlas = &DIGITS_24_40[0][0];
  else if (w == 23 && h == 39) atlas = &DIGITS_23_39[0][0];

  uint16_t *fb = gfx->getFramebuffer();
  size_t oldLength = previous ? strlen(previous) : 0;
  for (size_t i = 0; text[i]; ++i) {
    char value = text[i];
    int cw = value == ':' ? w / 3 : w;
    bool changed = force || !previous || i >= oldLength || previous[i] != value;
    if (changed) {
      gfx->fillRect(x, y, cw, h, bgColor);
      dirty.include(x, y, cw, h);
      if (value == ':') {
        gfx->fillCircle(x + cw / 2, y + h / 3, max(2, w / 14), color);
        gfx->fillCircle(x + cw / 2, y + h * 2 / 3, max(2, w / 14), color);
      } else if (atlas && value >= '0' && value <= '9') {
        const uint8_t *mask = atlas + (value - '0') * w * h;
        for (int py = 0; py < h; ++py) {
          int dstY = y + py;
          if (dstY < 0 || dstY >= 480) continue;
          uint16_t *dstRow = fb + dstY * 480;
          const uint8_t *maskRow = mask + py * w;
          for (int px = 0; px < w; ++px) {
            int dstX = x + px;
            if (dstX < 0 || dstX >= 480) continue;
            uint8_t alpha = maskRow[px];
            if (alpha) dstRow[dstX] = blend565(bgColor, color, alpha);
          }
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
  } else if (isBreakState(state)) {
    centeredText("休息中", 162, 1, kMint);
  } else {
    drawHourglass(240, 152, 28, kIce);
  }
}

void DisplayUI::drawDate(const RTCManager &rtc, uint16_t color) {
  if (!rtc.timeValid) {
    centeredText("时钟未就绪", 350, 2, kMuted);
    return;
  }
  char date[48];
  snprintf(date, sizeof(date), "%04u年%02u月%02u日", rtc.currentDateTime.getYear(),
           rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay());
  centeredText(date, 350, 2, color);
  const char *weekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
  uint8_t week = rtc.currentDateTime.getWeek();
  centeredText(week < 7 ? weekdays[week] : "星期未知", 415, 3, color);
}

void DisplayUI::drawFliqloCard(int x, int y, int w, int h) {
  gfx->fillRoundRect(x, y, w, h, 16, kFliqloBg);
  gfx->drawRoundRect(x, y, w, h, 16, kFliqloBd);
  gfx->drawFastHLine(x + 16, y + 1, w - 32, 0x4A69);
  gfx->fillRect(x, y + h / 2 - 2, w, 4, kBlack);
  gfx->fillRoundRect(x - 2, y + h / 2 - 12, 6, 24, 2, kFliqloHinge);
  gfx->fillRoundRect(x + w - 4, y + h / 2 - 12, 6, 24, 2, kFliqloHinge);
}

void DisplayUI::renderFliqloCardBuf(uint16_t *buf, int w, int h, char ch, uint16_t fgColor) {
  if (!buf) return;
  const int r = 10;
  const int r2 = r * r;
  const int half = h / 2;
  const uint16_t kShellBg = rgb(30, 33, 38);
  for (int py = 0; py < h; ++py) {
    uint16_t *row = buf + py * w;
    // 上半叶越靠近中轴越亮 (#16181C -> #323740)；下半叶中轴下先微暗再渐变 (#2D323A -> #14161A)
    uint16_t fillCol;
    if (py < half) {
      int k = py * 255 / max(1, half - 1);
      fillCol = rgb(22 + k * 28 / 255, 24 + k * 31 / 255, 28 + k * 36 / 255);
    } else {
      int k = (py - half) * 255 / max(1, h - half - 1);
      fillCol = rgb(45 - k * 25 / 255, 50 - k * 28 / 255, 58 - k * 32 / 255);
    }
    for (int px = 0; px < w; ++px) {
      bool inside = true;
      bool border = false;
      int dx = 0, dy = 0;
      if (px < r && py < r) { dx = r - px; dy = r - py; }
      else if (px >= w - r && py < r) { dx = px - (w - 1 - r); dy = r - py; }
      else if (px < r && py >= h - r) { dx = r - px; dy = py - (h - 1 - r); }
      else if (px >= w - r && py >= h - r) { dx = px - (w - 1 - r); dy = py - (h - 1 - r); }
      if (dx > 0 || dy > 0) {
        int d2 = dx * dx + dy * dy;
        if (d2 > r2) inside = false;
        else if (d2 >= (r - 1) * (r - 1)) border = true;
      } else if (px == 0 || px == w - 1 || py == 0 || py == h - 1) {
        border = true;
      }
      if (!inside) row[px] = kShellBg;
      else if (border) row[px] = (py < 3) ? 0x6B6D : 0x39E7;
      else row[px] = fillCol;
    }
  }

  // 使用窄高粗体 Fliqlo 专属字库 (64x132) 居中渲染单数字
  if (ch >= '0' && ch <= '9') {
    const int curX = (w - FLIQLO_GLYPH_W) / 2;
    const int curY = (h - FLIQLO_GLYPH_H) / 2;
    const uint8_t *mask = &FLIQLO_DIGITS[ch - '0'][0];
    for (int py = 0; py < FLIQLO_GLYPH_H; ++py) {
      int dstY = curY + py;
      if (dstY < 2 || dstY >= h - 2) continue;
      uint16_t *dstRow = buf + dstY * w;
      const uint8_t *mRow = mask + py * FLIQLO_GLYPH_W;
      for (int px = 0; px < FLIQLO_GLYPH_W; ++px) {
        int dstX = curX + px;
        if (dstX < 2 || dstX >= w - 2) continue;
        uint8_t a = mRow[px];
        if (a) dstRow[dstX] = blend565(dstRow[dstX], fgColor, a);
      }
    }
  }

  // 半透明银灰金属转轴细杆 + 左右圆形金属轴套（复刻实体机械翻页轴）
  const uint16_t axisHi = rgb(140, 148, 160);
  const uint16_t axisDark = rgb(18, 20, 24);
  for (int px = 4; px < w - 4; ++px) {
    buf[(half - 1) * w + px] = blend565(buf[(half - 1) * w + px], axisDark, 150);
    buf[half * w + px] = blend565(buf[half * w + px], axisHi, 110);
  }
  for (int py = half - 5; py <= half + 5; ++py) {
    if (py >= 0 && py < h) {
      for (int px = 1; px <= 5; ++px) {
        buf[py * w + px] = (py == half - 2) ? 0xCE79 : 0x6B6D;
        buf[py * w + (w - 1 - px)] = (py == half - 2) ? 0xCE79 : 0x6B6D;
      }
    }
  }
}

void DisplayUI::blitFlipCard(int dstX, int dstY, int w, int h, const uint16_t *buf, uint16_t) {
  if (!buf) return;
  uint16_t *fb = gfx->getFramebuffer();
  for (int py = 0; py < h; ++py) {
    memcpy(fb + (dstY + py) * 480 + dstX, buf + py * w, w * sizeof(uint16_t));
  }
  dirty.include(dstX, dstY, w, h);
}

void DisplayUI::compositeFlipAnim(FlipCardAnim &anim, float t) {
  if (!anim.oldBuf || !anim.newBuf) return;
  uint16_t *fb = gfx->getFramebuffer();
  const int w = anim.w, h = anim.h, half = h / 2;
  const int dstX = anim.x, dstY = anim.y;
  const uint16_t kShellBg = rgb(30, 33, 38);

  float e = (t < 0.5f) ? (2.0f * t * t) : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
  float angle = e * PI_F;
  float cosA = cosf(angle);
  float sinA = sinf(angle);

  for (int py = 0; py < half; ++py) {
    uint16_t *dstRow = fb + (dstY + py) * 480 + dstX;
    const uint16_t *srcRow = anim.newBuf + py * w;
    if (e < 0.5f && py > half - 20) {
      int dist = half - py;
      int shadow = constrain(255 - static_cast<int>((20 - dist) * (1.0f - cosA) * 7.5f), 85, 255);
      for (int px = 0; px < w; ++px) dstRow[px] = shade(srcRow[px], shadow);
    } else {
      memcpy(dstRow, srcRow, w * sizeof(uint16_t));
    }
  }
  for (int py = half; py < h; ++py) {
    uint16_t *dstRow = fb + (dstY + py) * 480 + dstX;
    const uint16_t *srcRow = anim.oldBuf + py * w;
    if (e >= 0.5f && py < half + 24) {
      int dist = py - half;
      int shadow = constrain(255 - static_cast<int>((24 - dist) * (1.0f + cosA) * 6.5f), 80, 255);
      for (int px = 0; px < w; ++px) dstRow[px] = shade(srcRow[px], shadow);
    } else {
      memcpy(dstRow, srcRow, w * sizeof(uint16_t));
    }
  }

  const int maxExpand = 3;
  for (int py = 0; py < h; ++py) {
    uint16_t *row = fb + (dstY + py) * 480 + dstX;
    for (int px = -maxExpand; px < 0; ++px) row[px] = kShellBg;
    for (int px = w; px < w + maxExpand; ++px) row[px] = kShellBg;
  }

  if (e < 0.5f) {
    int flapH = max(1, static_cast<int>(lroundf(half * cosA)));
    int shadeFactor = max(90, static_cast<int>(255 * (0.45f + 0.55f * cosA)));
    for (int i = 0; i < flapH; ++i) {
      int srcY = i * half / flapH;
      int outY = dstY + half - flapH + i;
      float rowWeight = 1.0f - static_cast<float>(i) / flapH;
      int expand = static_cast<int>(lroundf(maxExpand * sinA * rowWeight));
      int outW = w + expand * 2;
      uint16_t *dstRow = fb + outY * 480 + (dstX - expand);
      const uint16_t *srcRow = anim.oldBuf + srcY * w;
      for (int ox = 0; ox < outW; ++ox) dstRow[ox] = shade(srcRow[ox * w / outW], shadeFactor);
    }
  } else {
    float openCos = -cosA;
    int flapH = max(1, static_cast<int>(lroundf(half * openCos)));
    int shadeFactor = max(95, static_cast<int>(255 * (0.48f + 0.52f * openCos)));
    for (int i = 0; i < flapH; ++i) {
      int srcY = min(h - 1, half + (i * half / flapH));
      int outY = dstY + half + i;
      float rowWeight = static_cast<float>(i) / flapH;
      int expand = static_cast<int>(lroundf(maxExpand * sinA * rowWeight));
      int outW = w + expand * 2;
      uint16_t *dstRow = fb + outY * 480 + (dstX - expand);
      const uint16_t *srcRow = anim.newBuf + srcY * w;
      for (int ox = 0; ox < outW; ++ox) dstRow[ox] = shade(srcRow[ox * w / outW], shadeFactor);
    }
  }

  dirty.include(dstX - maxExpand, dstY, w + maxExpand * 2, h);
}

void DisplayUI::drawNixieTube(int, int, int, int) {}

void DisplayUI::drawNixieDigit(int tubeX, int tubeY, int, int, char ch, uint16_t fgColor) {
  if (ch < '0' || ch > '9') return;
  const uint16_t *sprite = &NIXIE_TUBES[ch - '0'][0];
  uint16_t *fb = gfx->getFramebuffer();
  const bool nativeOrange = (fgColor == kNixieGlow);
  for (int py = 0; py < NIXIE_TUBE_H; ++py) {
    uint16_t *dstRow = fb + (tubeY + py) * 480 + tubeX;
    const uint16_t *srcRow = sprite + py * NIXIE_TUBE_W;
    if (nativeOrange) {
      memcpy(dstRow, srcRow, NIXIE_TUBE_W * sizeof(uint16_t));
    } else {
      for (int px = 0; px < NIXIE_TUBE_W; ++px) {
        dstRow[px] = shade(srcRow[px], 175);
      }
    }
  }
}

void DisplayUI::drawPixelBigTime(const char *text, int cx, int y, int pxSize, uint16_t fgColor) {
  static const uint8_t kFont5x7[11][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
    {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}  // :
  };
  int totalW = 0;
  for (const char *p = text; *p; ++p) {
    if (p != text) totalW += pxSize;
    totalW += (*p == ':' ? 3 : 5) * pxSize;
  }
  int curX = cx - totalW / 2;
  gfx->fillRect(cx - totalW / 2 - 4, y - 4, totalW + 10, 7 * pxSize + 10, kBlack);
  for (const char *p = text; *p; ++p) {
    int idx = (*p >= '0' && *p <= '9') ? (*p - '0') : (*p == ':' ? 10 : -1);
    int cols = (*p == ':') ? 3 : 5;
    if (idx >= 0) {
      for (int r = 0; r < 7; ++r) {
        uint8_t bits = kFont5x7[idx][r];
        for (int c = 0; c < cols; ++c) {
          int bitPos = (*p == ':') ? (3 - c) : (4 - c);
          if (bits & (1 << bitPos)) {
            int bx = curX + c * pxSize;
            int by = y + r * pxSize;
            gfx->fillRect(bx + 2, by + 2, pxSize - 1, pxSize - 1, 0x18E3); // 像素投影
            gfx->fillRect(bx, by, pxSize - 1, pxSize - 1, fgColor);
          }
        }
      }
    }
    curX += (cols + 1) * pxSize;
  }
  dirty.include(cx - totalW / 2 - 4, y - 4, totalW + 10, 7 * pxSize + 10);
}

void DisplayUI::drawPixelSprite(int x, int y, int scale, uint8_t stage) {
  static const char *const kSprites[5][16] = {
    // 0: 泥土嫩芽
    {
      "................","................","................","................",
      "................","................",".......GG.......","......GLLG......",
      "......GGGG......",".......GG.......",".......GG.......","...BBBBBBBBBB...",
      "..BBBBBBBBBBBB..","..BBBBBBBBBBBB..","................","................"
    },
    // 1: 舒展幼苗
    {
      "................","................","................","................",
      "....GGG..GGG....","...GLLG..GLLG...","...GGGGGGGGGG...",".....GGGGGG.....",
      ".......GG.......",".......GG.......",".......GG.......","...BBBBBBBBBB...",
      "..BBBBBBBBBBBB..","..BBBBBBBBBBBB..","................","................"
    },
    // 2: 开花挂果
    {
      "................",".......YY.......","......YWWY......",".......YY.......",
      "....GGGGGGGG....","...GLLGGGGGG....","...GGGG..GGGG...","......GGGGGG....",
      ".......GG..GG...",".......GG.......",".......GG.......","...BBBBBBBBBB...",
      "..BBBBBBBBBBBB..","..BBBBBBBBBBBB..","................","................"
    },
    // 3: 成熟红番茄
    {
      ".......GG.......","......GGGG......","....GGLLLLGG....","...RRRRGGRRRR...",
      "..RRWWRRRRRRRR..",".RRWWRRRRRRRRRR.",".RRRRRRRRRRRRRR.",".RRRRRRRRRRRRRR.",
      ".RRRRRRRRRRRRRR.",".RRRRRRRRRRRRDR.","..RRRRRRRRRRDR..","..RRRRRRRRRDDR..",
      "...RRRRRRRDDR...","....RRRRRRRR....","......RRRR......","................"
    },
    // 4: 休息热咖啡杯
    {
      ".....W....W.....","....W....W......",".....W....W.....","................",
      "..WWWWWWWWWWWW..","..WCCCCCCCCCCW..","..WCCCCCCCCCCWW.","..WCCCCCCCCCCW.W",
      "..WCCCCCCCCCCW.W","..WCCCCCCCCCCWW.","..WCCCCCCCCCCW..","...WWWWWWWWWW...",
      "....WWWWWWWW....",".WWWWWWWWWWWWWW.","................","................"
    }
  };
  const char *const *spr = kSprites[stage % 5];
  for (int r = 0; r < 16; ++r) {
    for (int c = 0; c < 16; ++c) {
      char ch = spr[r][c];
      if (ch == '.') continue;
      uint16_t col = kWhite;
      if (ch == 'B') col = kPixelWood;
      else if (ch == 'G') col = kPixelGreen;
      else if (ch == 'L') col = 0x7FE9;
      else if (ch == 'Y') col = kGold;
      else if (ch == 'R') col = kPixelRed;
      else if (ch == 'D') col = 0xB800;
      else if (ch == 'C') col = 0xB3E5;
      gfx->fillRect(x + c * scale, y + r * scale, scale, scale, col);
    }
  }
}

void DisplayUI::drawStatsPage(const RTCManager &rtc, const StatsManager &stats,
                              const ConfigManager &config, const WebPortalManager &portal) {
  gfx->setTextSize(2);
  gfx->setTextColor(kWhite);
  gfx->setCursor(34, 96);
  gfx->print("专注统计");

  // 右上角快捷主题切换胶囊按钮
  static const char *const kThemeNames[5] = {
    "主题: 科技环", "主题: 翻页", "主题: 辉光管", "主题: 像素", "主题: 沙漏"
  };
  uint8_t thm = config.data.theme < 5 ? config.data.theme : 0;
  gfx->fillRoundRect(290, 68, 158, 36, 12, kCardBg);
  gfx->drawRoundRect(290, 68, 158, 36, 12, kCyan);
  centeredTextInBox(kThemeNames[thm], 369, 92, 1, kCyan);

  // 今日卡片与累计卡片
  gfx->fillRoundRect(32, 114, 200, 96, 12, kCardBg);
  gfx->drawRoundRect(32, 114, 200, 96, 12, kCardBd);
  gfx->fillRoundRect(248, 114, 200, 96, 12, kCardBg);
  gfx->drawRoundRect(248, 114, 200, 96, 12, kCardBd);

  uint32_t today = rtc.getDateYyyyMmDd();
  char buf1[32], buf2[32];
  snprintf(buf1, sizeof(buf1), "%u 个", stats.getTodayCount(today));
  snprintf(buf2, sizeof(buf2), "%u 分钟", stats.getTodayMinutes(today));
  centeredTextInBox("今日完成", 132, 138, 1, kMuted);
  centeredTextInBox(buf1, 132, 174, 2, kCyan);
  centeredTextInBox(buf2, 132, 198, 1, kIce);

  snprintf(buf1, sizeof(buf1), "%lu 个", static_cast<unsigned long>(stats.totalPomodoros));
  snprintf(buf2, sizeof(buf2), "%lu 分钟", static_cast<unsigned long>(stats.totalFocusMinutes));
  centeredTextInBox("历史累计", 348, 138, 1, kMuted);
  centeredTextInBox(buf1, 348, 174, 2, kGold);
  centeredTextInBox(buf2, 348, 198, 1, kIce);

  // 近7天柱状图区域
  gfx->fillRoundRect(32, 224, 416, 152, 12, kCardBg);
  gfx->drawRoundRect(32, 224, 416, 152, 12, kCardBd);
  centeredText("近 7 日专注分钟", 248, 1, kMuted);

  uint16_t maxMin = 10;
  for (int i = 0; i < StatsManager::MAX_DAYS; ++i) {
    if (stats.days[i].minutes > maxMin) maxMin = stats.days[i].minutes;
  }

  for (int i = 0; i < StatsManager::MAX_DAYS; ++i) {
    int bx = 56 + i * 54;
    int maxBarH = 74;
    int barH = stats.days[i].minutes > 0 ? max(4, static_cast<int>(stats.days[i].minutes) * maxBarH / maxMin) : 2;
    uint16_t barColor = stats.days[i].minutes > 0 ? kCyan : kCardBd;
    gfx->fillRoundRect(bx, 336 - barH, 32, barH, 4, barColor);

    char lbl[8] = "--";
    if (stats.days[i].yyyymmdd > 0) {
      uint32_t mm = (stats.days[i].yyyymmdd / 100) % 100;
      uint32_t dd = stats.days[i].yyyymmdd % 100;
      snprintf(lbl, sizeof(lbl), "%02lu/%02lu", static_cast<unsigned long>(mm), static_cast<unsigned long>(dd));
    }
    centeredTextInBox(lbl, bx + 16, 360, 1, kMuted);
  }

  // 底部 Web 控制台 / 配网热点按钮
  PortalMode pMode = portal.getMode();
  if (pMode == PORTAL_CONNECTING_STA) {
    gfx->fillRoundRect(90, 390, 300, 56, 14, 0x2140);
    gfx->drawRoundRect(90, 390, 300, 56, 14, kGold);
    centeredText("正在连接已存 Wi-Fi…", 412, 1, kWhite);
    centeredText("点击取消 (超时自动转热点)", 434, 1, kGold);
  } else if (pMode == PORTAL_STA) {
    gfx->fillRoundRect(90, 390, 300, 56, 14, 0x0268);
    gfx->drawRoundRect(90, 390, 300, 56, 14, kMint);
    char info[48];
    snprintf(info, sizeof(info), "浏览器访问 http://%s", portal.getIpString());
    centeredText(info, 412, 1, kWhite);
    centeredText("局域网已就绪 · 点击关闭", 434, 1, kMint);
  } else if (pMode == PORTAL_AP) {
    gfx->fillRoundRect(90, 390, 300, 56, 14, 0x0268);
    gfx->drawRoundRect(90, 390, 300, 56, 14, kCyan);
    char info[48];
    snprintf(info, sizeof(info), "热点: %s", WebPortalManager::AP_SSID);
    centeredText(info, 412, 1, kWhite);
    snprintf(info, sizeof(info), "访问 %s (已连:%d) 点击关闭", portal.getIpString(), portal.getConnectedClients());
    centeredText(info, 434, 1, kCyan);
  } else {
    gfx->fillRoundRect(90, 390, 300, 56, 14, kCardBg);
    gfx->drawRoundRect(90, 390, 300, 56, 14, kCyan);
    centeredText("开启 Web 控制台 / 配网", 424, 1, kIce);
  }
}

void DisplayUI::render(const PomodoroEngine &engine, const IMUSensor &imu,
                       const PowerManager &power, const RTCManager &rtc,
                       const StatsManager &stats, const ConfigManager &config,
                       const WebPortalManager &portal) {
  bool full = needsRedraw || (lastTheme != config.data.theme) ||
              (currentMode == UI_MODE_CLOCK && lastClockTheme != config.data.clockTheme);
  dirty = UiRect();
  if (currentMode == UI_MODE_CLOCK) {
    renderClockPage(imu, power, rtc, stats, config, full);
    presenter.present(gfx->getFramebuffer(), currentRotation, dirty);
    lastTheme = config.data.theme < 5 ? config.data.theme : 0;
    contentReady = true;
    return;
  }
  if (full) {
    gfx->fillScreen(kBlack);
    dirty.full();
    mainText[0] = smallText[0] = secondsText[0] = 0;
    lastLit = -1;
    lastPixelStage = 255;
  }
  if (full || lastBattery != power.batteryPercent || lastVbus != power.vbusIn ||
      lastBatteryConnected != power.batteryConnected || lastCharging != power.charging ||
      lastNtpSyncing != rtc.syncInProgress) {
    gfx->fillRect(0, 8, 480, 48, kBlack);
    drawHeader(power, rtc);
    dirty.include(0, 8, 480, 48);
  }

  uint8_t theme = config.data.theme < 5 ? config.data.theme : 0;
  char time[20];
  if (currentMode == UI_MODE_POMODORO) {
    bool isPaused = paused(engine.state);
    bool isBrk = isBreakState(engine.state);
    float progress = engine.getProgress();
    float remaining = 1.0f - progress;
    unsigned long sec = engine.getRemainingSeconds();
    uint8_t pendingFace = imu.getPendingFace();

    if (theme == THEME_CYBER) {
      int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
      uint8_t pulse = 255;
      if (engine.state == STATE_COMPLETED) {
        float phase = (millis() % 1600) / 1600.0f;
        float s = 0.5f - 0.5f * cosf(phase * 2.0f * PI_F);
        pulse = static_cast<uint8_t>(90 + s * 165);
      }

      if (full || lit != lastLit || isPaused != lastPaused || isBrk != lastIsBreak ||
          (engine.state == STATE_COMPLETED && pulse != lastPulseLevel)) {
        drawRing(remaining, isPaused, isBrk, pulse);
        dirty.include(RING_X, RING_Y, RING_SIZE, RING_SIZE);
        lastLit = lit;
        lastPaused = isPaused;
        lastIsBreak = isBrk;
        lastPulseLevel = pulse;
      }

      if (full || lastState != engine.state || lastPendingFace != pendingFace) {
        gfx->fillRect(150, 128, 180, 44, kBlack);
        if (pendingFace > 0) {
          char pre[24];
          snprintf(pre, sizeof(pre), "预览 面%u · %u分", pendingFace, config.data.faceMinutes[pendingFace - 1]);
          centeredText(pre, 158, 1, kCyan);
        } else {
          drawStatus(engine.state);
        }
        dirty.include(150, 128, 180, 44);
        lastPendingFace = pendingFace;
      }

      snprintf(time, sizeof(time), "%02lu:%02lu", sec / 60, sec % 60);
      uint16_t color = isPaused ? 0xFFBC : (isBrk ? kMint : kWhite);
      drawCenteredDigits(time, 240, 208, 64, 104, color, mainText, full || color != mainColor);
      mainColor = color;

      if (engine.state == STATE_COMPLETED) {
        if (full || lastState != engine.state) {
          gfx->fillRect(140, 336, 200, 48, kBlack);
          if (!engine.lastSessionWasBreak && engine.autoBreakEnabled) {
            centeredText("点击开始休息", 368, 1, kMint);
          } else {
            centeredText("点击重新开始", 368, 1, kCyan);
          }
          dirty.include(140, 336, 200, 48);
          smallText[0] = 0;
        }
      } else {
        if (full || lastState == STATE_COMPLETED) {
          gfx->fillRect(140, 336, 200, 48, kBlack);
          dirty.include(140, 336, 200, 48);
        }
        if (rtc.timeValid) drawCenteredDigits(rtc.getTimeString(), 240, 342, 24, 40, kIce, smallText, full || lastState == STATE_COMPLETED);
        else if (full || lastState == STATE_COMPLETED) {
          centeredText("--:--", 380, 2, kMuted);
          dirty.include(180, 342, 120, 40);
        }
      }
    } else if (theme == THEME_FLIQLO) {
      static const int cardX[4] = {34, 128, 264, 358};
      if (full) {
        for (int i = 0; i < 4; ++i) {
          flipCards[i].active = false;
          flipCards[i].x = cardX[i];
          flipCards[i].y = 148;
          flipCards[i].w = 88;
          flipCards[i].h = 172;
        }
        // 深炭灰圆角机械表壳底座 + 顶部弧形反光 + 中间两颗小方冒号
        gfx->fillRoundRect(18, 132, 444, 204, 20, rgb(30, 33, 38));
        gfx->drawRoundRect(18, 132, 444, 204, 20, 0x4A69);
        gfx->drawFastHLine(40, 134, 400, 0x6B6D);
        gfx->fillRoundRect(236, 208, 8, 10, 2, kWhite);
        gfx->fillRoundRect(236, 248, 8, 10, 2, kWhite);
        dirty.include(18, 132, 444, 204);
      }
      if (full || lastState != engine.state || lastPendingFace != pendingFace) {
        gfx->fillRect(120, 86, 240, 40, kBlack);
        if (pendingFace > 0) {
          char pre[24];
          snprintf(pre, sizeof(pre), "预览 面%u · %u分", pendingFace, config.data.faceMinutes[pendingFace - 1]);
          centeredText(pre, 114, 1, kCyan);
        } else if (isPaused) {
          centeredText("已暂停", 114, 1, kGold);
        } else if (isBrk) {
          centeredText("休息中", 114, 1, kMint);
        } else if (engine.state == STATE_COMPLETED) {
          centeredText(!engine.lastSessionWasBreak && engine.autoBreakEnabled ? "点击开始休息" : "已完成 · 点击重开", 114, 1, kCyan);
        } else {
          centeredText("FLIQLO POMODORO", 114, 1, kMuted);
        }
        dirty.include(120, 86, 240, 40);
        lastPendingFace = pendingFace;
      }

      snprintf(time, sizeof(time), "%02lu%02lu", sec / 60, sec % 60);
      uint16_t color = isPaused ? kGold : (isBrk ? kMint : kWhite);
      bool colorChanged = (color != mainColor);
      mainColor = color;

      for (int i = 0; i < 4; ++i) {
        char ch = time[i];
        if (full || colorChanged) {
          flipCards[i].active = false;
          flipCards[i].currentChar = ch;
          renderFliqloCardBuf(flipCards[i].newBuf, 88, 172, ch, color);
          blitFlipCard(cardX[i], 148, 88, 172, flipCards[i].newBuf);
        } else if (flipCards[i].currentChar != ch) {
          if (flipCards[i].oldBuf && flipCards[i].newBuf) {
            memcpy(flipCards[i].oldBuf, flipCards[i].newBuf, 88 * 172 * 2);
          }
          flipCards[i].currentChar = ch;
          renderFliqloCardBuf(flipCards[i].newBuf, 88, 172, ch, color);
          flipCards[i].active = true;
          flipCards[i].startMs = millis();
        }
      }

      int lit = constrain(static_cast<int>(ceilf(remaining * 36)), 0, 36);
      if (full || lit != lastLit || isPaused != lastPaused || isBrk != lastIsBreak) {
        gfx->fillRoundRect(56, 356, 368, 12, 6, kFliqloBg);
        int fillW = static_cast<int>(remaining * 364.0f);
        if (fillW > 4) gfx->fillRoundRect(58, 358, fillW, 8, 4, isPaused ? kGold : (isBrk ? kMint : kWhite));
        dirty.include(56, 356, 368, 12);
        lastLit = lit; lastPaused = isPaused; lastIsBreak = isBrk;
      }
      if (rtc.timeValid) drawCenteredDigits(rtc.getTimeString(), 240, 392, 24, 40, kMuted, smallText, full);
    } else if (theme == THEME_NIXIE) {
      static const int tubeX[4] = {28, 128, 256, 356};
      if (full) {
        // 底部深胡桃木/铜座与中间两颗暖橙小氖灯冒号
        gfx->fillRoundRect(20, 326, 440, 22, 6, 0x3142);
        gfx->drawRoundRect(20, 326, 440, 22, 6, 0x6284);
        gfx->fillCircle(240, 204, 7, 0x6140);
        gfx->fillCircle(240, 204, 4, kNixieGlow);
        gfx->fillCircle(240, 248, 7, 0x6140);
        gfx->fillCircle(240, 248, 4, kNixieGlow);
        dirty.include(20, 326, 440, 22);
      }
      if (full || lastState != engine.state || lastPendingFace != pendingFace) {
        gfx->fillRect(110, 76, 260, 34, kBlack);
        if (pendingFace > 0) {
          char pre[24];
          snprintf(pre, sizeof(pre), "预览 面%u · %u分", pendingFace, config.data.faceMinutes[pendingFace - 1]);
          centeredText(pre, 100, 1, kNixieHi);
        } else if (isPaused) {
          centeredText("暂停 · 辉光待命", 100, 1, kGold);
        } else if (isBrk) {
          centeredText("休息中", 100, 1, kMint);
        } else if (engine.state == STATE_COMPLETED) {
          centeredText(!engine.lastSessionWasBreak && engine.autoBreakEnabled ? "点击开始休息" : "已完成 · 点击重开", 100, 1, kNixieHi);
        } else {
          centeredText("IN-18 NIXIE TIMER", 100, 1, kNixieBd);
        }
        dirty.include(110, 76, 260, 34);
        lastPendingFace = pendingFace;
      }

      snprintf(time, sizeof(time), "%02lu%02lu", sec / 60, sec % 60);
      uint16_t color = isPaused ? kGold : (isBrk ? kMint : kNixieGlow);
      bool colorChanged = (color != mainColor);
      for (int i = 0; i < 4; ++i) {
        if (full || colorChanged || mainText[i] != time[i]) {
          drawNixieDigit(tubeX[i], 112, NIXIE_TUBE_W, NIXIE_TUBE_H, time[i], color);
          dirty.include(tubeX[i], 112, NIXIE_TUBE_W, NIXIE_TUBE_H);
        }
      }
      strcpy(mainText, time);
      mainColor = color;

      int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
      if (full || lit != lastLit || isPaused != lastPaused || isBrk != lastIsBreak) {
        for (int i = 0; i < 20; ++i) {
          int lx = 66 + i * 18;
          uint16_t lc = (i < lit) ? (isBrk ? kMint : kNixieGlow) : 0x28E0;
          gfx->fillCircle(lx, 366, 5, lc);
        }
        dirty.include(58, 358, 364, 16);
        lastLit = lit; lastPaused = isPaused; lastIsBreak = isBrk;
      }
      if (rtc.timeValid) drawCenteredDigits(rtc.getTimeString(), 240, 396, 24, 40, kNixieHi, smallText, full);
    } else if (theme == THEME_PIXEL) {
      // THEME_PIXEL: RPG 像素风农场/小番茄
      uint8_t stage = 0;
      if (isBrk) stage = 4;
      else if (engine.state == STATE_COMPLETED) stage = 3;
      else if (progress >= 0.60f) stage = 2;
      else if (progress >= 0.25f) stage = 1;

      if (full) {
        // 像素双线框与点缀小星星
        gfx->drawRect(40, 82, 400, 324, kPixelRed);
        gfx->drawRect(44, 86, 392, 316, kCardBd);
        gfx->fillRect(72, 106, 4, 4, kGold);
        gfx->fillRect(120, 128, 3, 3, kIce);
        gfx->fillRect(360, 110, 4, 4, kGold);
        gfx->fillRect(400, 134, 3, 3, kIce);
        dirty.include(40, 82, 400, 324);
      }
      if (full || lastPixelStage != stage) {
        gfx->fillRect(198, 96, 84, 84, kBlack);
        drawPixelSprite(200, 98, 5, stage);
        dirty.include(198, 96, 84, 84);
        lastPixelStage = stage;
      }
      if (full || lastState != engine.state || lastPendingFace != pendingFace) {
        gfx->fillRect(100, 186, 280, 28, kBlack);
        if (pendingFace > 0) {
          char pre[24];
          snprintf(pre, sizeof(pre), "预览 面%u · %u分", pendingFace, config.data.faceMinutes[pendingFace - 1]);
          centeredText(pre, 206, 1, kCyan);
        } else if (isPaused) {
          centeredText("PAUSED · 已暂停", 206, 1, kGold);
        } else if (isBrk) {
          centeredText("BREAK · 喝杯咖啡", 206, 1, kMint);
        } else if (engine.state == STATE_COMPLETED) {
          centeredText(!engine.lastSessionWasBreak && engine.autoBreakEnabled ? "LV.MAX! 点击休息" : "LV.MAX! 点击重开", 206, 1, kPixelGreen);
        } else {
          static const char *const kLv[4] = {"LV.1 播种中", "LV.2 发芽啦", "LV.3 开花结果", "LV.MAX 成熟!"};
          centeredText(kLv[stage % 4], 206, 1, kIce);
        }
        dirty.include(100, 186, 280, 28);
        lastPendingFace = pendingFace;
      }

      snprintf(time, sizeof(time), "%02lu:%02lu", sec / 60, sec % 60);
      uint16_t color = isPaused ? kGold : (isBrk ? kMint : kWhite);
      if (full || strcmp(mainText, time) != 0 || color != mainColor) {
        drawPixelBigTime(time, 240, 232, 9, color);
        strcpy(mainText, time);
        mainColor = color;
      }

      // RPG 像素血条式进度条
      int lit = constrain(static_cast<int>(ceilf(remaining * 20)), 0, 20);
      if (full || lit != lastLit || isPaused != lastPaused || isBrk != lastIsBreak) {
        gfx->drawRect(78, 336, 324, 24, kWhite);
        gfx->fillRect(80, 338, 320, 20, kBlack);
        int fillW = static_cast<int>(remaining * 316.0f);
        if (fillW > 2) {
          gfx->fillRect(82, 340, fillW, 16, isPaused ? kGold : (isBrk ? kMint : kPixelGreen));
        }
        dirty.include(78, 336, 324, 24);
        lastLit = lit; lastPaused = isPaused; lastIsBreak = isBrk;
      }
      if (rtc.timeValid) drawCenteredDigits(rtc.getTimeString(), 240, 416, 24, 40, kIce, smallText, full);
    } else {
      renderExtraTheme(theme, engine, imu, rtc, config, full);
    }
  } else if (currentMode == UI_MODE_CLOCK) {
    for (int i = 0; i < 4; ++i) flipCards[i].active = false;
    snprintf(time, sizeof(time), "%02u:%02u", rtc.getHour(), rtc.getMinute());
    int width = digitTextWidth(time, 70), secondsWidth = digitTextWidth("00", 23);
    int left = (480 - width - 12 - secondsWidth) / 2;
    if (rtc.timeValid) {
      drawDigits(time, left, 165, 70, 116, kWhite, mainText, full);
      snprintf(time, sizeof(time), "%02u", rtc.getSecond());
      drawDigits(time, left + width + 12, 165, 23, 39, kIce, secondsText, full);
    } else if (full) {
      centeredText("--:--", 260, 4, kMuted);
      dirty.include(100, 180, 280, 100);
    }
    uint32_t date = rtc.getDateYyyyMmDd();
    if (full || lastDate != date) {
      gfx->fillRect(0, 310, 480, 125, kBlack);
      drawDate(rtc, kIce);
      dirty.include(0, 310, 480, 125);
    }
  } else {
    if (full || lastPortalMode != portal.getMode() ||
        lastPortalClients != portal.getConnectedClients() ||
        lastTotalPomodoros != stats.totalPomodoros) {
      gfx->fillRect(0, 60, 480, 410, kBlack);
      drawStatsPage(rtc, stats, config, portal);
      dirty.include(0, 60, 480, 410);
    }
  }

  if (full) {
    gfx->fillCircle(36, 444, 22, 0x0125);
    gfx->drawCircle(36, 444, 22, kCyan);
    drawScreenOffIcon(36, 444, kIce);
    dirty.include(8, 416, 56, 56);
  }
  presenter.present(gfx->getFramebuffer(), currentRotation, dirty);
  lastTheme = theme;
  contentReady = true;
}

void DisplayUI::fullRedraw(const PomodoroEngine &e, const IMUSensor &imu, const PowerManager &p,
                           const RTCManager &r, const StatsManager &s, const ConfigManager &c,
                           const WebPortalManager &w) {
  needsRedraw = true;
  update(e, imu, p, r, s, c, w);
}

void DisplayUI::update(const PomodoroEngine &e, const IMUSensor &imu, const PowerManager &p,
                       const RTCManager &r, const StatsManager &s, const ConfigManager &c,
                       const WebPortalManager &w) {
  if (!isReady) return;
  presenter.report();
  if (manualScreenOff || panelOutputOff) return;
  if (rotating) {
    unsigned long now = millis(), elapsed = now - rotationStart;
    if (elapsed >= 240) {
      visualAngle = currentRotation * 90.0f;
      UiRect frame;
      frame.full();
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
    return;
  }
  if (lastTimeValid != r.timeValid) needsRedraw = true;
  uint32_t second = r.getHour() * 3600UL + r.getMinute() * 60UL + r.getSecond();
  uint32_t date = r.getDateYyyyMmDd();
  unsigned long remaining = e.getRemainingSeconds();
  uint8_t pendingFace = imu.getPendingFace();

  bool pulseTick = false;
  if (currentMode == UI_MODE_POMODORO) {
    if (e.state == STATE_COMPLETED) {
      static unsigned long lastPulseFrame = 0;
      if (millis() - lastPulseFrame >= 80) {
        lastPulseFrame = millis();
        pulseTick = true;
      }
    } else if (c.data.theme == THEME_HOURGLASS) {
      static unsigned long lastExtraAnim = 0;
      if (millis() - lastExtraAnim >= 65) {
        lastExtraAnim = millis();
        pulseTick = true;
      }
    }
  }

  bool clockTick = clockNeedsAnimTick(imu, r, c);
  bool changed = needsRedraw || lastTheme != c.data.theme || lastClockTheme != c.data.clockTheme ||
      lastState != e.state || lastBattery != p.batteryPercent ||
      lastBatteryConnected != p.batteryConnected || lastCharging != p.charging || lastVbus != p.vbusIn ||
      lastDate != date || lastNtpSyncing != r.syncInProgress ||
      (currentMode == UI_MODE_POMODORO && (lastSeconds != remaining || lastClockSecond / 60 != second / 60 || lastPendingFace != pendingFace || pulseTick)) ||
      (currentMode == UI_MODE_CLOCK && (lastClockSecond != second || clockTick)) ||
      (currentMode == UI_MODE_STATS && (lastPortalMode != w.getMode() || lastPortalClients != w.getConnectedClients() || lastTotalPomodoros != s.totalPomodoros));

  auto anyFlipActive = [&]() {
    return flipCards[0].active || flipCards[1].active || flipCards[2].active || flipCards[3].active;
  };
  if (!changed) {
    if (anyFlipActive() && c.data.theme == THEME_FLIQLO && currentMode == UI_MODE_POMODORO) {
      unsigned long now = millis();
      if (now - lastFlipAnimFrame >= 16) {
        lastFlipAnimFrame = now;
        dirty = UiRect();
        for (int i = 0; i < 4; ++i) {
          FlipCardAnim &anim = flipCards[i];
          if (!anim.active) continue;
          unsigned long el = now - anim.startMs;
          if (el >= 220) {
            anim.active = false;
            blitFlipCard(anim.x, anim.y, anim.w, anim.h, anim.newBuf);
          } else {
            compositeFlipAnim(anim, static_cast<float>(el) / 220.0f);
          }
        }
        if (!dirty.empty()) presenter.present(gfx->getFramebuffer(), currentRotation, dirty);
      }
    }
    return;
  }
  render(e, imu, p, r, s, c, w);
  if (anyFlipActive() && c.data.theme == THEME_FLIQLO && currentMode == UI_MODE_POMODORO) {
    lastFlipAnimFrame = millis();
    dirty = UiRect();
    for (int i = 0; i < 4; ++i) {
      if (flipCards[i].active) compositeFlipAnim(flipCards[i], 0.05f);
    }
    if (!dirty.empty()) presenter.present(gfx->getFramebuffer(), currentRotation, dirty);
  }
  lastState = e.state;
  lastBattery = p.batteryPercent;
  lastVbus = p.vbusIn;
  lastBatteryConnected = p.batteryConnected;
  lastCharging = p.charging;
  lastDate = date;
  lastTimeValid = r.timeValid;
  lastNtpSyncing = r.syncInProgress;
  lastSeconds = remaining;
  lastClockSecond = second;
  lastPortalMode = w.getMode();
  lastPortalClients = w.getConnectedClients();
  lastTotalPomodoros = s.totalPomodoros;
  needsRedraw = false;
}
