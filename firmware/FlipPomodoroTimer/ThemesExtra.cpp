#include "DisplayUI.h"
#include "DigitFont.h"
#include <math.h>

namespace {
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kMuted = 0x8C71;
constexpr uint16_t kCyan = 0x07FF, kGold = 0xFE68, kIce = 0xD6BA, kMint = 0x3FE9;
constexpr float PI_F = 3.14159265359f;

inline uint16_t rgb565(int r, int g, int b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

inline uint16_t blendC(uint16_t bg, uint16_t fg, uint8_t alpha) {
  if (alpha == 0) return bg;
  if (alpha == 255) return fg;
  int r = (((bg >> 11) & 31) * (255 - alpha) + ((fg >> 11) & 31) * alpha) / 255;
  int g = (((bg >> 5) & 63) * (255 - alpha) + ((fg >> 5) & 63) * alpha) / 255;
  int b = ((bg & 31) * (255 - alpha) + (fg & 31) * alpha) / 255;
  return (r << 11) | (g << 5) | b;
}

// 沙漏 S 形玻璃轮廓半宽查表 (总高 284px: 0..141 上室, 142 细颈, 143..283 下室)
const int16_t *getHourglassContour() {
  static int16_t hw[285];
  static bool built = false;
  if (!built) {
    for (int i = 0; i <= 284; ++i) {
      float d = fabsf(static_cast<float>(i - 142)) / 142.0f; // 0(颈) .. 1(端)
      float w;
      if (d > 0.78f) {
        // 靠近底座处轻微圆润收口
        float k = (d - 0.78f) / 0.22f;
        w = 78.0f - 10.0f * k * k;
      } else {
        // 平滑 S 形曲率 (颈部 r=5, 肚部 r=78)
        float s = 0.5f - 0.5f * cosf((d / 0.78f) * PI_F);
        w = 5.0f + 73.0f * s;
      }
      hw[i] = static_cast<int16_t>(lroundf(w));
    }
    built = true;
  }
  return hw;
}
}

void DisplayUI::renderExtraTheme(uint8_t, const PomodoroEngine &engine, const IMUSensor &imu,
                                 const RTCManager &rtc, const ConfigManager &config, bool full) {
  const bool isPaused = (engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED);
  const bool isBrk = (engine.state == STATE_BREAK_RUNNING || engine.state == STATE_BREAK_PAUSED);
  const bool isRunning = (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING);
  const float progress = engine.getProgress();
  const float remaining = 1.0f - progress;
  const unsigned long sec = engine.getRemainingSeconds();
  const uint8_t pendingFace = imu.getPendingFace();

  const int cx = 148, topY = 98, neckY = 240, botY = 382;
  const int16_t *contour = getHourglassContour();

  // 1. 静态外框：黄铜/钛金质感底座与纤细立柱
  if (full) {
    gfx->fillRoundRect(50, 84, 196, 14, 5, rgb565(150, 118, 72));
    gfx->drawFastHLine(54, 86, 188, rgb565(220, 190, 130));
    gfx->fillRoundRect(50, 382, 196, 14, 5, rgb565(150, 118, 72));
    gfx->drawFastHLine(54, 384, 188, rgb565(220, 190, 130));
    gfx->fillRect(58, 98, 5, 284, rgb565(110, 88, 54));
    gfx->drawFastVLine(59, 98, 284, rgb565(190, 160, 110));
    gfx->fillRect(233, 98, 5, 284, rgb565(110, 88, 54));
    gfx->drawFastVLine(234, 98, 284, rgb565(190, 160, 110));
    dirty.include(50, 84, 196, 314);
  }

  // 2. 沙漏流沙与玻璃腔体动态渲染 (~16fps)
  static unsigned long lastSandMs = 0;
  unsigned long now = millis();
  bool sandTick = isRunning && (now - lastSandMs >= 65);
  if (full || sandTick || lastLit != static_cast<int>(sec)) {
    if (sandTick) lastSandMs = now;
    uint16_t *fb = gfx->getFramebuffer();
    const uint16_t kGlassBg = rgb565(10, 15, 24);
    const uint16_t kGlassRim = rgb565(115, 165, 200);
    const uint16_t kGlassHi = rgb565(205, 232, 250);

    // 沙粒主色（专注：暖流金；休息：薄荷青；暂停：暗琥珀）
    const int baseR = isBrk ? 56 : (isPaused ? 210 : 245);
    const int baseG = isBrk ? 218 : (isPaused ? 165 : 196);
    const int baseB = isBrk ? 148 : (isPaused ? 40 : 78);

    // 上室沙面高度：按面积/高度近似映射
    int topSandRows = static_cast<int>(powf(remaining, 0.72f) * 124.0f);
    int topSurfaceBaseY = neckY - topSandRows;

    // 下室圆锥沙堆高度
    int coneHeight = static_cast<int>(powf(progress, 0.72f) * 108.0f);
    int conePeakY = botY - coneHeight;

    for (int i = 0; i <= 284; ++i) {
      int y = topY + i;
      int w = contour[i];
      uint16_t *row = fb + y * 480;

      // 先填充玻璃内部暗底
      for (int dx = -w + 2; dx <= w - 2; ++dx) {
        row[cx + dx] = kGlassBg;
      }

      // 上室漏斗凹面流沙 (y < neckY)
      if (y < neckY && topSandRows > 1 && y >= topSurfaceBaseY - 8) {
        for (int dx = -w + 2; dx <= w - 2; ++dx) {
          float nx = static_cast<float>(dx) / max(1, w);
          // 中心向细颈凹陷的漏斗弧度
          int funnelDip = static_cast<int>((1.0f - nx * nx) * min(14, topSandRows / 2 + 4));
          int surfY = topSurfaceBaseY + funnelDip;
          if (y >= surfY) {
            int grain = ((cx + dx) * 17 + y * 31) & 15;
            float shadeK = (y <= surfY + 1) ? 1.12f : (0.82f + grain * 0.015f - nx * 0.14f);
            int r = constrain(static_cast<int>(baseR * shadeK), 0, 255);
            int g = constrain(static_cast<int>(baseG * shadeK), 0, 255);
            int b = constrain(static_cast<int>(baseB * shadeK), 0, 255);
            row[cx + dx] = rgb565(r, g, b);
          }
        }
      }

      // 下室圆锥形沙堆 (y > neckY)
      if (y > neckY && coneHeight > 1 && y >= conePeakY) {
        float tCone = static_cast<float>(y - conePeakY) / max(1, coneHeight);
        int coneHalfW = min(w - 2, static_cast<int>(tCone * 78.0f) + 2);
        for (int dx = -coneHalfW; dx <= coneHalfW; ++dx) {
          float nx = static_cast<float>(dx) / max(1, coneHalfW);
          int grain = ((cx + dx) * 19 + y * 29) & 15;
          bool isSlopeEdge = (abs(dx) >= coneHalfW - 1);
          float shadeK = isSlopeEdge ? 1.10f : (0.85f + grain * 0.014f - nx * 0.18f);
          int r = constrain(static_cast<int>(baseR * shadeK), 0, 255);
          int g = constrain(static_cast<int>(baseG * shadeK), 0, 255);
          int b = constrain(static_cast<int>(baseB * shadeK), 0, 255);
          row[cx + dx] = rgb565(r, g, b);
        }
      }

      // 细流沙柱与下落粒子 (细颈 -> 沙堆顶)
      if (isRunning && remaining > 0.001f && y >= neckY && y <= conePeakY + 1) {
        uint32_t animStep = (now / 45) + y;
        int streamBright = (animStep % 5 == 0) ? 255 : 210;
        uint16_t streamCol = rgb565(min(255, baseR * streamBright / 220),
                                    min(255, baseG * streamBright / 220),
                                    min(255, baseB * streamBright / 220));
        row[cx] = streamCol;
        if ((animStep % 7) < 3) row[cx - 1] = blendC(row[cx - 1], streamCol, 150);
        if ((animStep % 9) < 3) row[cx + 1] = blendC(row[cx + 1], streamCol, 150);
      }

      // 玻璃左侧曲面反光高光带 & 双层外轮廓
      int hiX = cx - static_cast<int>(w * 0.68f);
      if (w > 16 && i > 10 && i < 274) {
        row[hiX] = blendC(row[hiX], kGlassHi, 110);
        row[hiX + 1] = blendC(row[hiX + 1], kGlassHi, 70);
      }
      row[cx - w] = kGlassRim;
      row[cx - w + 1] = blendC(row[cx - w + 1], kGlassRim, 130);
      row[cx + w] = kGlassRim;
      row[cx + w - 1] = blendC(row[cx + w - 1], kGlassRim, 130);
    }

    // 沙堆顶点飞溅的 3 颗跳动微粒
    if (isRunning && remaining > 0.001f && conePeakY > neckY + 10) {
      int tick = (now / 75) % 4;
      uint16_t sparkCol = rgb565(min(255, baseR + 20), min(255, baseG + 30), min(255, baseB + 40));
      gfx->fillRect(cx - 5 - tick, conePeakY - 4 + tick, 2, 2, sparkCol);
      gfx->fillRect(cx + 4 + tick, conePeakY - 5 + ((tick + 1) % 3), 2, 2, sparkCol);
    }

    dirty.include(66, topY, 166, 286);
    lastLit = static_cast<int>(sec);
  }

  // 3. 右侧独立双行大数字倒计时 + 状态 + 当前时间
  const int rightCx = 352;
  if (full || lastState != engine.state || lastPendingFace != pendingFace) {
    gfx->fillRect(256, 86, 196, 36, kBlack);
    if (pendingFace > 0) {
      char pre[24];
      snprintf(pre, sizeof(pre), "预览 %u分", config.data.faceMinutes[pendingFace - 1]);
      centeredTextInBox(pre, rightCx, 112, 1, kCyan);
    } else if (isPaused) {
      centeredTextInBox("已暂停", rightCx, 112, 1, kGold);
    } else if (isBrk) {
      centeredTextInBox("休息中", rightCx, 112, 1, kMint);
    } else if (engine.state == STATE_COMPLETED) {
      centeredTextInBox(!engine.lastSessionWasBreak && engine.autoBreakEnabled ? "点击休息" : "已完成", rightCx, 112, 1, kCyan);
    } else {
      centeredTextInBox("HOURGLASS", rightCx, 112, 1, rgb565(170, 145, 95));
    }
    dirty.include(256, 86, 196, 36);
    lastPendingFace = pendingFace;
  }

  char minBuf[4], secBuf[4];
  snprintf(minBuf, sizeof(minBuf), "%02lu", sec / 60);
  snprintf(secBuf, sizeof(secBuf), "%02lu", sec % 60);
  uint16_t minCol = isPaused ? kGold : (isBrk ? kMint : kWhite);
  uint16_t secCol = isPaused ? kGold : (isBrk ? kMint : kGold);
  char prevMin[3] = {mainText[0], mainText[1], 0};
  char prevSec[3] = {mainText[2], mainText[3], 0};
  drawCenteredDigits(minBuf, rightCx, 134, 64, 104, minCol, prevMin, full || minCol != mainColor);
  drawCenteredDigits(secBuf, rightCx, 258, 64, 104, secCol, prevSec, full || secCol != mainColor);
  snprintf(mainText, sizeof(mainText), "%s%s", minBuf, secBuf);
  mainColor = minCol;

  if (rtc.timeValid) {
    drawCenteredDigits(rtc.getTimeString(), rightCx, 384, 24, 40, kIce, smallText, full);
  }
}
