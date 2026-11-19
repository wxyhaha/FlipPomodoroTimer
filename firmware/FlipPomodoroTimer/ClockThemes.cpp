#include "DisplayUI.h"
#include "DigitFont.h"
#include <math.h>

namespace {
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0x8C71;
constexpr uint16_t kCyan  = 0x07FF;
constexpr uint16_t kIce   = 0xD6BA;
constexpr uint16_t kGold  = 0xFE68;
constexpr float kPi       = 3.14159265359f;

inline uint16_t rgb565(int r, int g, int b) {
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha) {
  if (alpha == 0) return bg;
  if (alpha == 255) return fg;
  int r = (((bg >> 11) & 31) * (255 - alpha) + ((fg >> 11) & 31) * alpha) / 255;
  int g = (((bg >> 5) & 63) * (255 - alpha) + ((fg >> 5) & 63) * alpha) / 255;
  int b = ((bg & 31) * (255 - alpha) + (fg & 31) * alpha) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

struct RGB {
  uint8_t r, g, b;
};

inline RGB lerpRgb(const RGB &a, const RGB &b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return {
    static_cast<uint8_t>(lroundf(a.r + (b.r - a.r) * t)),
    static_cast<uint8_t>(lroundf(a.g + (b.g - a.g) * t)),
    static_cast<uint8_t>(lroundf(a.b + (b.b - a.b) * t))
  };
}

// 24 小时平滑昼夜天色关键帧（极简天象背景用，低饱和克制保证白色数字极清晰）
struct SkyStop {
  float hour;
  RGB top;
  RGB bot;
};

static const SkyStop kSkyStops[] = {
  { 0.0f, {  2,  4, 14}, { 18, 36, 82} }, // 深夜星空：深邃夜空 -> 地平线幽蓝
  { 5.0f, {  4,  8, 22}, { 28, 42, 88} }, // 拂晓前
  { 6.5f, { 14, 24, 64}, {148, 56, 48} }, // 清晨朝霞：深蓝 -> 暖红朝霞
  { 8.5f, {  8, 22, 62}, { 30, 96,140} }, // 晨光晴空
  {12.5f, {  6, 18, 52}, { 28, 96,142} }, // 午后晴空：深靛蓝 -> 晴朗青蓝
  {16.5f, { 10, 20, 56}, { 64, 78,116} }, // 下午微暖
  {18.0f, { 22, 14, 54}, {156, 68, 28} }, // 黄昏晚霞：暮色紫 -> 落日琥珀橘
  {19.5f, {  6, 10, 32}, { 40, 28, 68} }, // 暮色入夜
  {21.0f, {  2,  4, 14}, { 18, 36, 82} }, // 深夜星空
  {24.0f, {  2,  4, 14}, { 18, 36, 82} },
};

// 4x4 Bayer 有序抖动矩阵 (0..15)，彻底消除 RGB565 渐变色带分层
static const int8_t kBayer4x4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

void sampleSkyGradient(float hourF, RGB &topOut, RGB &botOut) {
  while (hourF < 0.0f) hourF += 24.0f;
  while (hourF >= 24.0f) hourF -= 24.0f;
  constexpr size_t n = sizeof(kSkyStops) / sizeof(kSkyStops[0]);
  for (size_t i = 0; i + 1 < n; ++i) {
    if (hourF >= kSkyStops[i].hour && hourF <= kSkyStops[i + 1].hour) {
      float span = kSkyStops[i + 1].hour - kSkyStops[i].hour;
      float t = span > 0.001f ? (hourF - kSkyStops[i].hour) / span : 0.0f;
      topOut = lerpRgb(kSkyStops[i].top, kSkyStops[i + 1].top, t);
      botOut = lerpRgb(kSkyStops[i].bot, kSkyStops[i + 1].bot, t);
      return;
    }
  }
  topOut = kSkyStops[0].top;
  botOut = kSkyStops[0].bot;
}

// 固定伪随机星空点（避开顶部栏和底部按钮）
struct StarPos { int16_t x, y; uint8_t phase; uint8_t bright; };
static const StarPos kStars[] = {
  { 36, 78, 12, 220}, { 92, 92, 55, 180}, {148, 72, 98, 240}, {210, 86,140, 170},
  {274, 74,185, 230}, {336, 94,220, 195}, {404, 80, 32, 245}, {452,110, 74, 180},
  { 58,136,118, 210}, {122,126,162, 165}, {378,132,205, 225}, {438,154,244, 190},
  { 28,196, 40, 175}, { 64,248, 88, 200}, {424,218,132, 215}, {456,268,176, 185},
  { 44,306,214, 190}, { 94,296, 18, 170}, {396,304, 62, 220}, {446,328,106, 180},
  { 76,368,150, 210}, {132,396,192, 175}, {354,388,236, 200}, {408,370, 26, 230},
  {172,114, 68, 185}, {308,118,112, 205}, { 22,158,156, 160}, {462,188,198, 175},
  { 84,424,240, 180}, {382,422, 46, 195}, {190,446, 90, 165}, {296,448,134, 175}
};

// 将抗锯齿 7 段数字直接 alpha-blend 到当前 framebuffer（无缝融入渐变天空，支持全局透明度与冒号呼吸）
void drawAlphaDigitsOnFb(uint16_t *fb, const char *text, int x, int y, int w, int h,
                         uint16_t color, uint8_t globalAlpha = 255, uint8_t colonAlpha = 255) {
  const uint8_t *atlas = nullptr;
  if (w == 64 && h == 104) atlas = &DIGITS_64_104[0][0];
  else if (w == 70 && h == 116) atlas = &DIGITS_70_116[0][0];
  else if (w == 24 && h == 40) atlas = &DIGITS_24_40[0][0];
  else if (w == 23 && h == 39) atlas = &DIGITS_23_39[0][0];

  for (size_t i = 0; text[i]; ++i) {
    char value = text[i];
    int cw = (value == ':') ? w / 3 : w;
    if (value == ':') {
      int r = max(2, w / 14);
      int cx = x + cw / 2;
      int ys[2] = { y + h / 3, y + h * 2 / 3 };
      uint16_t effAlphaBase = static_cast<uint16_t>(globalAlpha) * colonAlpha / 255U;
      for (int k = 0; k < 2; ++k) {
        int cy = ys[k];
        for (int dy = -r - 1; dy <= r + 1; ++dy) {
          int py = cy + dy;
          if (py < 0 || py >= 480) continue;
          for (int dx = -r - 1; dx <= r + 1; ++dx) {
            int px = cx + dx;
            if (px < 0 || px >= 480) continue;
            float d = sqrtf(static_cast<float>(dx * dx + dy * dy));
            if (d <= r) {
              fb[py * 480 + px] = blend565(fb[py * 480 + px], color, static_cast<uint8_t>(effAlphaBase));
            } else if (d < r + 1.0f) {
              uint8_t a = static_cast<uint8_t>((1.0f - (d - r)) * effAlphaBase);
              fb[py * 480 + px] = blend565(fb[py * 480 + px], color, a);
            }
          }
        }
      }
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
          if (alpha) {
            if (globalAlpha < 255) alpha = static_cast<uint8_t>((static_cast<uint16_t>(alpha) * globalAlpha) / 255U);
            dstRow[dstX] = blend565(dstRow[dstX], color, alpha);
          }
        }
      }
    }
    x += cw + w / 10;
  }
}

// 计算近似真实月相 (0.0=新月, 0.25=上弦, 0.5=满月, 0.75=下弦)
float calcMoonPhase(uint16_t y, uint8_t m, uint8_t d) {
  if (y < 2000) return 0.35f;
  int yy = y, mm = m;
  if (mm < 3) { yy--; mm += 12; }
  long jd = 365L * yy + yy / 4 - yy / 100 + yy / 400 + (153 * (mm + 1)) / 5 + d - 730122L;
  float daysSinceNew = static_cast<float>(jd) - 5.597661f;
  float cycles = daysSinceNew / 29.53058867f;
  float phase = cycles - floorf(cycles);
  if (phase < 0.0f) phase += 1.0f;
  return phase;
}

// 绘制太阳与多层柔和大气光晕 (warmth: 0=正午淡暖白, 1=晨昏暖橘红)
void drawSunGlowAndDisc(uint16_t *fb, int cx, int cy, float warmth) {
  RGB glowC = lerpRgb({255, 236, 198}, {255, 134, 64}, warmth);
  RGB discC = lerpRgb({255, 252, 240}, {255, 206, 145}, warmth);
  uint16_t glow565 = rgb565(glowC.r, glowC.g, glowC.b);
  uint16_t disc565 = rgb565(discC.r, discC.g, discC.b);

  constexpr int R_GLOW = 74;
  constexpr float R_DISC = 12.5f;
  int y0 = max(60, cy - R_GLOW), y1 = min(479, cy + R_GLOW);
  int x0 = max(0, cx - R_GLOW),  x1 = min(479, cx + R_GLOW);
  for (int y = y0; y <= y1; ++y) {
    int dy = y - cy;
    int dy2 = dy * dy;
    uint16_t *row = fb + y * 480;
    for (int x = x0; x <= x1; ++x) {
      int dx = x - cx;
      int d2 = dx * dx + dy2;
      if (d2 > R_GLOW * R_GLOW) continue;
      float d = sqrtf(static_cast<float>(d2));
      float g = 1.0f - d / R_GLOW;
      uint8_t ga = static_cast<uint8_t>(g * g * g * (82.0f + warmth * 38.0f));
      if (ga > 1) row[x] = blend565(row[x], glow565, ga);
      if (d <= R_DISC + 4.0f) {
        float edge = (d <= R_DISC * 0.55f) ? 1.0f : max(0.0f, 1.0f - (d - R_DISC * 0.55f) / (R_DISC * 0.45f + 4.0f));
        uint8_t da = static_cast<uint8_t>(edge * edge * 155.0f);
        if (da > 1) row[x] = blend565(row[x], disc565, da);
      }
    }
  }
}

// 绘制真实月相与淡淡银蓝月晕
void drawMoonPhaseAndGlow(uint16_t *fb, int cx, int cy, float phase) {
  uint16_t glow565 = rgb565(135, 175, 235);
  uint16_t moon565 = rgb565(242, 246, 255);
  constexpr int R_GLOW = 46;
  constexpr float R_MOON = 12.5f;
  float cosPhi = cosf(phase * 2.0f * kPi);
  bool waxing = (phase <= 0.5f);

  int y0 = max(60, cy - R_GLOW), y1 = min(479, cy + R_GLOW);
  int x0 = max(0, cx - R_GLOW),  x1 = min(479, cx + R_GLOW);
  for (int y = y0; y <= y1; ++y) {
    int dy = y - cy;
    int dy2 = dy * dy;
    uint16_t *row = fb + y * 480;
    for (int x = x0; x <= x1; ++x) {
      int dx = x - cx;
      int d2 = dx * dx + dy2;
      if (d2 > R_GLOW * R_GLOW) continue;
      float d = sqrtf(static_cast<float>(d2));
      float g = 1.0f - d / R_GLOW;
      uint8_t ga = static_cast<uint8_t>(g * g * g * 58.0f);
      if (ga > 1) row[x] = blend565(row[x], glow565, ga);
      if (d <= R_MOON + 1.2f) {
        float edge = (d <= R_MOON) ? 1.0f : max(0.0f, 1.0f - (d - R_MOON) / 1.2f);
        float maxRx = sqrtf(max(0.0f, R_MOON * R_MOON - dy2));
        float termX = maxRx * cosPhi;
        bool lit = waxing ? (dx >= termX) : (dx <= -termX);
        float soft = 1.0f;
        float distToTerm = fabsf(static_cast<float>(dx) - (waxing ? termX : -termX));
        if (distToTerm < 1.8f) soft = lit ? (0.5f + 0.5f * distToTerm / 1.8f) : (0.5f - 0.5f * distToTerm / 1.8f);
        else if (!lit) soft = 0.08f; // 微弱灰光地球照
        uint8_t ma = static_cast<uint8_t>(edge * soft * 215.0f);
        if (ma > 1) row[x] = blend565(row[x], moon565, ma);
      }
    }
  }
}

// 底部三层分形雾化山脉（Ridged FBM 山峰、山脊天光高光与山岚薄雾，支持陀螺仪视差微移）
void drawMistMountains(uint16_t *fb, const RGB &botC, float warmth, int pxShift, bool isNight) {
  static float ridgeFar[540];
  static float ridgeMid[540];
  static float ridgeNear[540];
  static bool built = false;
  if (!built) {
    auto ridged = [](float x, float freq, float phase) {
      float v = 1.0f - fabsf(sinf(x * freq + phase));
      return v * v;
    };
    for (int i = 0; i < 540; ++i) {
      float u = static_cast<float>(i - 30) / 480.0f;
      // 极远黛山：连绵尖峰与缓谷
      float fFar = ridged(u, 3.8f, 0.4f) * 0.52f + ridged(u, 8.7f, 1.9f) * 0.28f +
                   ridged(u, 19.3f, 0.7f) * 0.13f + ridged(u, 41.0f, 2.2f) * 0.07f;
      ridgeFar[i] = 382.0f - fFar * 46.0f;

      // 中景山脊：立体峰峦 + 细腻岩脊微纹理
      float fMid = ridged(u, 3.1f, 2.1f) * 0.50f + ridged(u, 7.6f, 0.5f) * 0.30f +
                   ridged(u, 17.5f, 2.8f) * 0.14f + ridged(u, 37.0f, 1.1f) * 0.06f;
      ridgeMid[i] = 414.0f - fMid * 42.0f;

      // 近景山麓：稳重起伏 + 微细林冠锯齿
      float fNear = ridged(u, 2.6f, 1.0f) * 0.55f + ridged(u, 6.4f, 2.4f) * 0.28f +
                    ridged(u, 15.2f, 0.9f) * 0.11f + ridged(u, 48.0f, 0.2f) * 0.06f;
      ridgeNear[i] = 446.0f - fNear * 36.0f;
    }
    built = true;
  }

  RGB farBase  = lerpRgb({static_cast<uint8_t>(botC.r * 0.66f), static_cast<uint8_t>(botC.g * 0.68f), static_cast<uint8_t>(botC.b * 0.76f)},
                         {76, 44, 62}, warmth * 0.45f);
  RGB midBase  = lerpRgb({static_cast<uint8_t>(botC.r * 0.42f), static_cast<uint8_t>(botC.g * 0.45f), static_cast<uint8_t>(botC.b * 0.54f)},
                         {48, 28, 44}, warmth * 0.50f);
  RGB nearBase = lerpRgb({static_cast<uint8_t>(botC.r * 0.20f), static_cast<uint8_t>(botC.g * 0.23f), static_cast<uint8_t>(botC.b * 0.31f)},
                         {22, 14, 24}, warmth * 0.55f);

  // 山脊边缘高光色（晨昏暖金边 / 晴空青白边 / 深夜银蓝月边）
  RGB rimRgb = isNight ? RGB{95, 130, 185} : lerpRgb({110, 175, 225}, {255, 170, 105}, warmth);
  uint16_t rimCol = rgb565(rimRgb.r, rimRgb.g, rimRgb.b);

  int sFar  = constrain(pxShift / 3, -8, 8);
  int sMid  = constrain(pxShift * 2 / 3, -12, 12);
  int sNear = constrain(pxShift, -16, 16);

  for (int x = 0; x < 480; ++x) {
    float yf = ridgeFar[x + 30 + sFar];
    float ym = ridgeMid[x + 30 + sMid];
    float yn = ridgeNear[x + 30 + sNear];
    float slopeM = ridgeMid[x + 31 + sMid] - ridgeMid[x + 29 + sMid];
    int yStart = max(325, static_cast<int>(floorf(min(yf, min(ym, yn)))) - 1);
    const uint8_t bx = static_cast<uint8_t>(x & 3);

    for (int y = yStart; y < 480; ++y) {
      float fy = static_cast<float>(y);
      uint16_t &dst = fb[y * 480 + x];

      // 1. 极远黛山层
      if (fy >= yf - 0.8f) {
        float cov = constrain(fy - (yf - 0.8f), 0.0f, 1.0f);
        float depth = constrain((fy - yf) / 52.0f, 0.0f, 1.0f);
        // 山脚逐渐融入薄雾
        float mistFade = 1.0f - depth * 0.62f;
        uint8_t a = static_cast<uint8_t>(cov * mistFade * 175.0f);
        uint16_t c = rgb565(farBase.r, farBase.g, farBase.b);
        dst = blend565(dst, c, a);
      }

      // 2. 中景山脊层（带垂直雾化梯度 + 山脊向阳侧高光轮廓）
      if (fy >= ym - 0.8f) {
        float cov = constrain(fy - (ym - 0.8f), 0.0f, 1.0f);
        float depth = constrain((fy - ym) / 48.0f, 0.0f, 1.0f);
        float mistFade = 1.0f - depth * 0.45f;
        float dither = (kBayer4x4[y & 3][bx] - 7.5f) / 16.0f;
        RGB mc = lerpRgb(midBase, botC, depth * 0.35f);
        uint16_t m565 = rgb565(mc.r + static_cast<int>(dither * 4.0f),
                               mc.g + static_cast<int>(dither * 3.0f),
                               mc.b + static_cast<int>(dither * 4.0f));
        dst = blend565(dst, m565, static_cast<uint8_t>(cov * mistFade * 215.0f));
        // 1.5px 山脊高光轮廓
        if (fy - ym >= -0.4f && fy - ym <= 1.3f) {
          uint8_t rimA = static_cast<uint8_t>(constrain(42.0f - slopeM * 14.0f, 18.0f, 88.0f));
          dst = blend565(dst, rimCol, rimA);
        }
      }

      // 3. 近景山麓层
      if (fy >= yn - 0.8f) {
        float cov = constrain(fy - (yn - 0.8f), 0.0f, 1.0f);
        float depth = constrain((fy - yn) / 40.0f, 0.0f, 1.0f);
        RGB nc = lerpRgb(nearBase, {static_cast<uint8_t>(nearBase.r * 0.7f), static_cast<uint8_t>(nearBase.g * 0.7f), static_cast<uint8_t>(nearBase.b * 0.75f)}, depth);
        uint16_t n565 = rgb565(nc.r, nc.g, nc.b);
        dst = blend565(dst, n565, static_cast<uint8_t>(cov * 235.0f));
        if (fy - yn >= -0.3f && fy - yn <= 1.0f) {
          dst = blend565(dst, rimCol, 28);
        }
      }
    }
  }
}

void drawAlphaRing(uint16_t *fb, int cx, int cy, float radius, float thickness, uint16_t color, uint8_t maxAlpha) {
  if (maxAlpha == 0 || radius <= 2.0f) return;
  int rOuter = static_cast<int>(ceilf(radius + thickness + 1.0f));
  int y0 = max(60, cy - rOuter);
  int y1 = min(479, cy + rOuter);
  int x0 = max(0, cx - rOuter);
  int x1 = min(479, cx + rOuter);
  for (int y = y0; y <= y1; ++y) {
    float dy = static_cast<float>(y - cy);
    uint16_t *row = fb + y * 480;
    for (int x = x0; x <= x1; ++x) {
      float dx = static_cast<float>(x - cx);
      float dist = fabsf(sqrtf(dx * dx + dy * dy) - radius);
      if (dist < thickness) {
        float f = 1.0f - dist / thickness;
        uint8_t a = static_cast<uint8_t>(f * f * maxAlpha);
        if (a > 2) row[x] = blend565(row[x], color, a);
      }
    }
  }
}

// ============================================================================
// 像素小猫全屏时间小世界绘制器 (120 x 120 逻辑像素 -> 480 x 480 全屏 4x 放大)
// ============================================================================
constexpr int kSceneW = 120;
constexpr int kSceneH = 120;
static uint16_t sSceneBuf[kSceneW * kSceneH];

inline void spx(int x, int y, uint16_t c) {
  if (x >= 0 && x < kSceneW && y >= 0 && y < kSceneH) sSceneBuf[y * kSceneW + x] = c;
}

inline void spxBlend(int x, int y, uint16_t c, uint8_t alpha) {
  if (x >= 0 && x < kSceneW && y >= 0 && y < kSceneH) {
    uint16_t &dst = sSceneBuf[y * kSceneW + x];
    dst = blend565(dst, c, alpha);
  }
}

void sRect(int x0, int y0, int x1, int y1, uint16_t c) {
  x0 = max(0, x0); y0 = max(0, y0);
  x1 = min(kSceneW, x1); y1 = min(kSceneH, y1);
  for (int y = y0; y < y1; ++y) {
    uint16_t *row = sSceneBuf + y * kSceneW;
    for (int x = x0; x < x1; ++x) row[x] = c;
  }
}

void sCircle(float cx, float cy, float r, uint16_t col, bool hasOutline = false, uint16_t outCol = 0) {
  int y0 = max(0, static_cast<int>(floorf(cy - r - 1.5f)));
  int y1 = min(kSceneH - 1, static_cast<int>(ceilf(cy + r + 1.5f)));
  int x0 = max(0, static_cast<int>(floorf(cx - r - 1.5f)));
  int x1 = min(kSceneW - 1, static_cast<int>(ceilf(cx + r + 1.5f)));
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      float d = hypotf(x - cx, y - cy);
      if (d <= r) spx(x, y, col);
      else if (hasOutline && d <= r + 1.1f) spx(x, y, outCol);
    }
  }
}

void sHeart(int cx, int cy, uint16_t c) {
  static const uint8_t rows[5] = {0b01010, 0b11111, 0b11111, 0b01110, 0b00100};
  for (int r = 0; r < 5; ++r) {
    for (int col = 0; col < 5; ++col) {
      if (rows[r] & (1 << (4 - col))) spx(cx + col - 2, cy + r - 2, c);
    }
  }
}

void sCloud(int cx, int cy, uint16_t c, int clipX0, int clipX1) {
  const float blobs[5][3] = {{-5, 1, 3.0f}, {-1, -1, 3.8f}, {4, 0, 3.3f}, {7, 2, 2.3f}, {-7, 2, 2.0f}};
  for (int i = 0; i < 5; ++i) {
    float bx = cx + blobs[i][0], by = cy + blobs[i][1], br = blobs[i][2];
    for (int y = static_cast<int>(by - br - 1); y <= static_cast<int>(by + br + 1); ++y) {
      for (int x = static_cast<int>(bx - br - 1); x <= static_cast<int>(bx + br + 1); ++x) {
        if (x >= clipX0 && x < clipX1 && hypotf(x - bx, y - by) <= br) spx(x, y, c);
      }
    }
  }
}

enum CatPeriod : uint8_t { CAT_MORNING = 0, CAT_WORK = 1, CAT_SUNSET = 2, CAT_SLEEP = 3 };

CatPeriod getCatPeriod(uint8_t hour, bool timeValid) {
  if (!timeValid) return CAT_WORK;
  if (hour >= 6 && hour < 9) return CAT_MORNING;
  if (hour >= 9 && hour < 17) return CAT_WORK;
  if (hour >= 17 && hour < 20) return CAT_SUNSET;
  return CAT_SLEEP;
}

void drawPixelCatSprite(int cx, int cy, CatPeriod period, bool happy, bool chimeAnim,
                        bool blink, int tailSwing, int eyeShiftX, unsigned long now) {
  const uint16_t furMain  = rgb565(242, 162,  76);
  const uint16_t furLight = rgb565(255, 198, 122);
  const uint16_t furDark  = rgb565(205, 118,  42);
  const uint16_t furWhite = rgb565(252, 246, 236);
  const uint16_t outline  = rgb565( 92,  48,  22);
  const uint16_t earPink  = rgb565(245, 152, 162);
  const uint16_t eyeCol   = rgb565( 34,  22,  18);
  const uint16_t blush    = rgb565(255, 138, 148);

  const bool excited = happy || chimeAnim;

  if (period == CAT_SLEEP && !excited) {
    // 深夜蜷在柔软猫窝里睡觉，带轻微呼吸起伏
    int breath = ((now / 1100) % 2 == 0) ? 0 : -1;
    // 蓝色猫窝
    for (int y = cy + 1; y < cy + 10; ++y) {
      for (int x = cx - 13; x < cx + 14; ++x) {
        float d = powf((x - cx) / 13.0f, 2.0f) + powf((y - (cy + 6)) / 5.0f, 2.0f);
        if (d <= 1.0f) spx(x, y, y < cy + 6 ? rgb565(72, 108, 158) : rgb565(52, 82, 126));
      }
    }
    // 蜷成毛球身体
    for (int y = cy - 5 + breath; y < cy + 7; ++y) {
      for (int x = cx - 9; x < cx + 10; ++x) {
        float d = powf((x - cx) / 9.2f, 2.0f) + powf((y - (cy + 1 + breath * 0.5f)) / 5.8f, 2.0f);
        if (d <= 1.0f) spx(x, y, furMain);
        else if (d <= 1.25f) spx(x, y, outline);
      }
    }
    for (int dx : {2, 3, 5, 6}) {
      spx(cx + dx, cy - 3 + breath, furDark);
      spx(cx + dx, cy - 2 + breath, furDark);
    }
    // 尖耳朵
    const int8_t ears[][2] = {{-7,-5},{-6,-6},{-6,-5},{-5,-5},{-2,-5},{-1,-6},{-1,-5},{0,-5}};
    for (auto &e : ears) spx(cx + e[0], cy + e[1] + breath, furMain);
    spx(cx - 6, cy - 5 + breath, earPink);
    spx(cx - 1, cy - 5 + breath, earPink);
    // 脸颊 + 闭眼
    sRect(cx - 7, cy + 1, cx - 1, cy + 5, furWhite);
    for (int dx : {-7, -6, -3, -2}) spx(cx + dx, cy + 1, eyeCol);
    spx(cx - 4, cy + 2, earPink);
    spx(cx - 8, cy + 2, blush);
    spx(cx - 1, cy + 2, blush);
    for (int dx = 3; dx <= 7; ++dx) spx(cx + dx, cy + 4, dx >= 6 ? furWhite : furDark);

    // 缓慢飘动的 Zzz
    int zPhase = (now / 700) % 3;
    uint16_t zCol = rgb565(190, 215, 255);
    int zOff = -zPhase;
    const int8_t smallZ[][2] = {{2,-7},{3,-7},{4,-7},{3,-6},{2,-5},{3,-5},{4,-5}};
    for (auto &p : smallZ) spx(cx + p[0], cy + p[1] + zOff, zCol);
    if (zPhase >= 1) {
      const int8_t bigZ[][2] = {{7,-13},{8,-13},{9,-13},{10,-13},{9,-12},{8,-11},{7,-10},{8,-10},{9,-10},{10,-10}};
      for (auto &p : bigZ) spx(cx + p[0], cy + p[1] + zOff, zCol);
    }
    return;
  }

  int yOff = excited ? (((now / 180) % 2 == 0) ? -2 : -1) : 0;
  cy += yOff;

  // 投影
  sRect(cx - 8, cy + 8 - yOff, cx + 9, cy + 10 - yOff, rgb565(32, 22, 18));

  // 动态摇摆尾巴
  int txShift = excited ? (((now / 150) % 2 == 0) ? 1 : -1) : tailSwing;
  const int8_t tailBase[][2] = {{8,5},{9,5},{10,4},{11,3},{11,2},{11,1},{10,0},{10,-1}};
  for (int i = 0; i < 8; ++i) {
    int tx = cx + tailBase[i][0] + (i >= 4 ? txShift : 0);
    int ty = cy + tailBase[i][1];
    uint16_t col = (i >= 6) ? furWhite : ((i % 2 == 0) ? furDark : furMain);
    spx(tx, ty, col);
    spx(tx, ty + 1, col);
  }

  // 坐姿身体
  for (int y = cy + 1; y < cy + 9; ++y) {
    for (int x = cx - 8; x < cx + 9; ++x) {
      float d = powf((x - cx) / 7.8f, 2.0f) + powf((y - (cy + 5)) / 4.8f, 2.0f);
      if (d <= 1.0f) spx(x, y, (abs(x - cx) <= 3 && y >= cy + 3) ? furWhite : furMain);
      else if (d <= 1.25f) spx(x, y, outline);
    }
  }

  // 小白爪爪
  if (excited || period == CAT_MORNING) {
    int wave = excited ? (((now / 160) % 2 == 0) ? -1 : 0) : 0;
    for (int side : {-1, 1}) {
      int py = cy + wave * (side == 1 ? 1 : 0);
      spx(cx + side * 8, py - 1, furWhite);
      spx(cx + side * 9, py - 1, furWhite);
      spx(cx + side * 8, py, furWhite);
      spx(cx + side * 9, py, excited ? earPink : furWhite);
      spx(cx + side * 8, py + 1, furMain);
    }
  } else {
    sRect(cx - 5, cy + 6, cx - 2, cy + 9, furWhite);
    sRect(cx + 3, cy + 6, cx + 6, cy + 9, furWhite);
    spx(cx - 4, cy + 8, outline);
    spx(cx + 4, cy + 8, outline);
  }

  // 大圆头
  int hcy = cy - 3;
  for (int y = hcy - 7; y < hcy + 7; ++y) {
    for (int x = cx - 9; x < cx + 10; ++x) {
      float d = powf((x - cx) / 8.5f, 2.0f) + powf((y - hcy) / 6.3f, 2.0f);
      if (d <= 1.0f) {
        if (y >= hcy + 2 && abs(x - cx) <= 4) spx(x, y, furWhite);
        else if (y <= hcy - 3) spx(x, y, furLight);
        else spx(x, y, furMain);
      } else if (d <= 1.25f) {
        spx(x, y, outline);
      }
    }
  }

  // 头顶川字橘纹与颊纹
  for (int dx : {-2, 0, 2}) {
    spx(cx + dx, hcy - 5, furDark);
    spx(cx + dx, hcy - 4, furDark);
  }
  for (int side : {-1, 1}) {
    spx(cx + side * 7, hcy + 1, furDark);
    spx(cx + side * 8, hcy + 1, furDark);
    spx(cx + side * 7, hcy + 3, furDark);
  }

  // 尖耳朵
  for (int side : {-1, 1}) {
    int ex = cx + side * 5;
    const int8_t eo[][2] = {{-2,0},{-1,-1},{0,-2},{1,-1},{2,0},{-1,0},{0,-1},{1,0},{0,0}};
    for (auto &p : eo) spx(ex + p[0] * side, hcy - 6 + p[1], outline);
    const int8_t ei[][2] = {{-1,0},{0,-1},{1,0},{0,0}};
    for (auto &p : ei) spx(ex + p[0] * side, hcy - 5 + p[1], furMain);
    spx(ex, hcy - 5, earPink);
  }

  // 粉鼻子 & 嘴巴
  spx(cx, hcy + 2, earPink);
  if (excited) {
    sRect(cx - 2, hcy + 3, cx + 3, hcy + 5, rgb565(195, 75, 88));
    spx(cx, hcy + 4, rgb565(255, 165, 175));
  } else if (period == CAT_MORNING) {
    sRect(cx - 1, hcy + 3, cx + 2, hcy + 5, rgb565(185, 72, 82));
  } else {
    spx(cx - 2, hcy + 3, outline);
    spx(cx - 1, hcy + 4, outline);
    spx(cx,     hcy + 3, outline);
    spx(cx + 1, hcy + 4, outline);
    spx(cx + 2, hcy + 3, outline);
  }

  // 眼睛与饰品
  if (excited) {
    // 弯弯笑眼 ^ ^
    const int8_t sm[][2] = {{-5,0},{-4,-1},{-3,0},{3,0},{4,-1},{5,0}};
    for (auto &p : sm) spx(cx + p[0], hcy + p[1], eyeCol);
    int floatY = ((now / 220) % 2 == 0) ? -9 : -11;
    if (happy) {
      sHeart(cx - 10, hcy + floatY, rgb565(255, 95, 130));
      sHeart(cx + 10, hcy + floatY + 1, rgb565(255, 140, 170));
    }
    if (chimeAnim) {
      uint16_t nCol = rgb565(255, 220, 110);
      const int8_t note[][2] = {{11,-7},{12,-7},{12,-8},{12,-9},{12,-10},{13,-10},{14,-9}};
      for (auto &p : note) spx(cx + p[0], hcy + p[1] + (floatY + 9), nCol);
    }
  } else if (blink || period == CAT_MORNING) {
    sRect(cx - 5, hcy, cx - 3, hcy + 1, eyeCol);
    sRect(cx + 3, hcy, cx + 5, hcy + 1, eyeCol);
  } else {
    int shift = (period == CAT_SUNSET && eyeShiftX == 0) ? -1 : eyeShiftX;
    sRect(cx - 5 + shift, hcy - 1, cx - 3 + shift, hcy + 2, eyeCol);
    spx(cx - 5 + shift, hcy - 1, kWhite);
    sRect(cx + 3 + shift, hcy - 1, cx + 5 + shift, hcy + 2, eyeCol);
    spx(cx + 3 + shift, hcy - 1, kWhite);

    if (period == CAT_WORK) {
      // 深棕玳瑁黑框圆眼镜（与橘猫脸形成高对比度）
      uint16_t gCol = rgb565(68, 44, 34);
      spx(cx - 5, hcy - 2, gCol); spx(cx - 4, hcy - 2, gCol); spx(cx - 3, hcy - 2, gCol);
      spx(cx - 5, hcy + 2, gCol); spx(cx - 4, hcy + 2, gCol); spx(cx - 3, hcy + 2, gCol);
      spx(cx + 3, hcy - 2, gCol); spx(cx + 4, hcy - 2, gCol); spx(cx + 5, hcy - 2, gCol);
      spx(cx + 3, hcy + 2, gCol); spx(cx + 4, hcy + 2, gCol); spx(cx + 5, hcy + 2, gCol);
      for (int dy = -1; dy <= 1; ++dy) {
        spx(cx - 6, hcy + dy, gCol); spx(cx - 2, hcy + dy, gCol);
        spx(cx + 2, hcy + dy, gCol); spx(cx + 6, hcy + dy, gCol);
      }
      spx(cx - 1, hcy, gCol); spx(cx, hcy, gCol); spx(cx + 1, hcy, gCol);
    } else if (period == CAT_SUNSET) {
      uint16_t nCol = rgb565(255, 215, 110);
      int ny = ((now / 700) % 2 == 0) ? 0 : -1;
      const int8_t note[][2] = {{11,-7},{12,-7},{12,-8},{12,-9},{12,-10},{13,-10},{14,-9}};
      for (auto &p : note) spx(cx + p[0], hcy + p[1] + ny, nCol);
    }
  }

  // 腮红
  sRect(cx - 7, hcy + 1, cx - 5, hcy + 3, blush);
  sRect(cx + 6, hcy + 1, cx + 8, hcy + 3, blush);
}

// 圆润粗体 6x8 像素数字点阵 (0..9 + ':')，用于全景大落地窗内统一画风的像素时钟
static const uint8_t kPixFont6x8[11][8] = {
  {0b011110, 0b110011, 0b110011, 0b110011, 0b110011, 0b110011, 0b110011, 0b011110}, // 0
  {0b001100, 0b011100, 0b111100, 0b001100, 0b001100, 0b001100, 0b001100, 0b111111}, // 1
  {0b011110, 0b110011, 0b000011, 0b000110, 0b001100, 0b011000, 0b110000, 0b111111}, // 2
  {0b011110, 0b110011, 0b000011, 0b001110, 0b000011, 0b000011, 0b110011, 0b011110}, // 3
  {0b000110, 0b001110, 0b011110, 0b110110, 0b110110, 0b111111, 0b000110, 0b000110}, // 4
  {0b111111, 0b110000, 0b111110, 0b000011, 0b000011, 0b000011, 0b110011, 0b011110}, // 5
  {0b011110, 0b110000, 0b110000, 0b111110, 0b110011, 0b110011, 0b110011, 0b011110}, // 6
  {0b111111, 0b110011, 0b000011, 0b000110, 0b001100, 0b001100, 0b001100, 0b001100}, // 7
  {0b011110, 0b110011, 0b110011, 0b011110, 0b110011, 0b110011, 0b110011, 0b011110}, // 8
  {0b011110, 0b110011, 0b110011, 0b011111, 0b000011, 0b000011, 0b110011, 0b011110}, // 9
  {0b000000, 0b001100, 0b001100, 0b000000, 0b000000, 0b001100, 0b001100, 0b000000}, // :
};

void drawPixGlyph(int x, int y, char ch, int scale, uint16_t color, uint8_t alpha = 255) {
  int idx = (ch == ':') ? 10 : ((ch >= '0' && ch <= '9') ? (ch - '0') : -1);
  if (idx < 0) return;
  for (int r = 0; r < 8; ++r) {
    uint8_t row = kPixFont6x8[idx][r];
    for (int c = 0; c < 6; ++c) {
      if (row & (1 << (5 - c))) {
        for (int sy = 0; sy < scale; ++sy) {
          for (int sx = 0; sx < scale; ++sx) {
            if (alpha == 255) spx(x + c * scale + sx, y + r * scale + sy, color);
            else spxBlend(x + c * scale + sx, y + r * scale + sy, color, alpha);
          }
        }
      }
    }
  }
}

void drawPixText(int x, int y, const char *str, int scale, uint16_t color, uint16_t shadowCol,
                 uint8_t shadowAlpha, uint8_t colonAlpha = 255, bool outline = false) {
  int curX = x;
  for (size_t i = 0; str[i]; ++i) {
    char ch = str[i];
    int cw = (ch == ':') ? (4 * scale) : (6 * scale);
    int gx = (ch == ':') ? (curX - scale) : curX;
    uint8_t fgAlpha = (ch == ':') ? colonAlpha : 255;
    if (outline) {
      uint8_t oa = static_cast<uint8_t>((static_cast<uint16_t>(shadowAlpha) * fgAlpha) / 255U);
      drawPixGlyph(gx - 1, y,     ch, scale, shadowCol, oa);
      drawPixGlyph(gx + 1, y,     ch, scale, shadowCol, oa);
      drawPixGlyph(gx,     y - 1, ch, scale, shadowCol, oa);
      drawPixGlyph(gx,     y + 1, ch, scale, shadowCol, oa);
      drawPixGlyph(gx + 1, y + 1, ch, scale, shadowCol, oa);
    } else if (shadowAlpha > 0) {
      drawPixGlyph(gx + 1, y + 1, ch, scale, shadowCol,
                   static_cast<uint8_t>((static_cast<uint16_t>(shadowAlpha) * fgAlpha) / 255U));
    }
    drawPixGlyph(gx, y, ch, scale, color, fgAlpha);
    curX += cw + scale;
  }
}

// 绘制小巧可爱的 5x5 像素小番茄图标
void drawMiniTomatoIcon(int x, int y) {
  spx(x + 2, y, rgb565(65, 200, 90));
  spx(x + 1, y + 1, rgb565(65, 200, 90));
  spx(x + 3, y + 1, rgb565(65, 200, 90));
  sRect(x + 1, y + 2, x + 4, y + 5, rgb565(242, 68, 58));
  spx(x, y + 3, rgb565(242, 68, 58));
  spx(x + 4, y + 3, rgb565(242, 68, 58));
  spx(x + 1, y + 2, rgb565(255, 160, 150));
}

void blitPixelScene4x(uint16_t *fb) {
  for (int sy = 0; sy < kSceneH; ++sy) {
    const uint16_t *src = sSceneBuf + sy * kSceneW;
    int baseY = sy * 4;
    for (int repY = 0; repY < 4; ++repY) {
      uint16_t *dst = fb + (baseY + repY) * 480;
      for (int sx = 0; sx < kSceneW; ++sx) {
        uint16_t c = src[sx];
        int dx = sx * 4;
        dst[dx]     = c;
        dst[dx + 1] = c;
        dst[dx + 2] = c;
        dst[dx + 3] = c;
      }
    }
  }
}

} // namespace

bool DisplayUI::checkClockStyleButtonClick(int16_t x, int16_t y) const {
  if (currentMode != UI_MODE_CLOCK) return false;
  return y >= 416 && y < 472 && x >= 416 && x < 472;
}

bool DisplayUI::checkClockPetClick(int16_t x, int16_t y, uint8_t clockTheme) const {
  if (currentMode != UI_MODE_CLOCK || clockTheme != CLOCK_STYLE_PIXEL_CAT) return false;
  // 下半区房间互动区域（避开底部左右两个按钮）
  return x >= 24 && x < 456 && y >= 256 && y < 428;
}

bool DisplayUI::checkClockDemoClick(int16_t x, int16_t y) const {
  if (currentMode != UI_MODE_CLOCK) return false;
  // 点击上半区时间数字/落地窗区域切换 24 小时实机演示模式
  return x >= 32 && x < 448 && y >= 68 && y < 244;
}

void DisplayUI::toggleClockDemo() {
  clockDemoMode = !clockDemoMode;
  clockDemoStartMs = millis();
  lastClockAnimMs = 0;
  meteorStartMs = 0;
  nextMeteorMs = clockDemoMode ? (clockDemoStartMs + 9000UL) : 0;
  needsRedraw = true;
}

void DisplayUI::triggerClockPetHappy() {
  petHappyUntilMs = millis() + 3000UL;
  lastClockAnimMs = 0;
}

void DisplayUI::triggerHourlyChimeAnim() {
  hourlyChimeStartMs = millis();
  hourlyChimeActive = true;
  lastClockAnimMs = 0;
}

void DisplayUI::drawClockStyleButton(uint8_t clockTheme) {
  const int cx = 444, cy = 444;
  gfx->fillCircle(cx, cy, 22, 0x0125);
  gfx->drawCircle(cx, cy, 22, kCyan);
  if (clockTheme == CLOCK_STYLE_SKY) {
    // 当前是极简天象 -> 按钮内画极简小猫头轮廓（提示可切到像素小猫）
    gfx->fillTriangle(cx - 9, cy - 3, cx - 7, cy - 11, cx - 2, cy - 5, kIce);
    gfx->fillTriangle(cx + 9, cy - 3, cx + 7, cy - 11, cx + 2, cy - 5, kIce);
    gfx->fillCircle(cx, cy + 2, 8, kIce);
    gfx->fillCircle(cx - 3, cy + 1, 1, 0x0125);
    gfx->fillCircle(cx + 3, cy + 1, 1, 0x0125);
    gfx->drawPixel(cx, cy + 4, 0x0125);
  } else {
    // 当前是像素小猫 -> 按钮内画弯月+星点（提示可切到极简天象）
    gfx->fillCircle(cx - 2, cy, 8, kIce);
    gfx->fillCircle(cx + 3, cy - 2, 7, 0x0125);
    gfx->fillCircle(cx + 7, cy - 6, 2, kGold);
    gfx->drawPixel(cx + 9, cy + 4, kIce);
  }
  dirty.include(416, 416, 56, 56);
}

bool DisplayUI::clockNeedsAnimTick(const IMUSensor &imu, const RTCManager &rtc, const ConfigManager &config) {
  if (currentMode != UI_MODE_CLOCK) {
    clockDemoMode = false;
    return false;
  }
  unsigned long now = millis();

  if (hourlyChimeActive && (now - hourlyChimeStartMs >= 3200UL)) {
    hourlyChimeActive = false;
    needsRedraw = true;
  }

  uint8_t cTheme = config.data.clockTheme < 2 ? config.data.clockTheme : 0;
  uint8_t effHour = rtc.timeValid ? rtc.getHour() : 22;
  if (clockDemoMode) {
    uint32_t demoTotalMin = (330UL + ((now - clockDemoStartMs) * 45UL) / 1000UL) % 1440UL;
    effHour = static_cast<uint8_t>(demoTotalMin / 60UL);
  }
  bool nightSky = (effHour >= 20 || effHour < 6);

  // 深夜偶发流星调度（演示模式每 5..7 秒一次，正常模式每 16..24 秒一次，持续 1300ms）
  if (nightSky) {
    if (nextMeteorMs == 0) nextMeteorMs = now + (clockDemoMode ? 800UL : 5000UL);
    if (meteorStartMs == 0 && now >= nextMeteorMs) {
      meteorStartMs = now;
      meteorStartX = 280 + static_cast<int16_t>((now * 37UL) % 150UL);
      meteorStartY = 72 + static_cast<int16_t>((now * 19UL) % 80UL);
      meteorDir = -1;
    } else if (meteorStartMs != 0 && now - meteorStartMs >= 1300UL) {
      meteorStartMs = 0;
      nextMeteorMs = now + (clockDemoMode ? 3500UL : (16000UL + (now % 8000UL)));
    }
  } else {
    meteorStartMs = 0;
    if (clockDemoMode) nextMeteorMs = 0;
  }

  bool meteorActive = (meteorStartMs != 0);
  bool happyActive = (now < petHappyUntilMs);

  // 极简天象：检测设备倾斜以提供丝滑的陀螺仪视差反馈
  bool skyTilting = false;
  if (cTheme == CLOCK_STYLE_SKY && imu.isReady) {
    float tx = 0.0f;
    switch (currentRotation) {
      case 0: tx =  imu.ax; break;
      case 1: tx =  imu.ay; break;
      case 2: tx = -imu.ax; break;
      case 3: tx = -imu.ay; break;
    }
    if (fabsf(tx) > 0.14f) skyTilting = true;
  }

  // 自适应节能帧率：活跃动效/演示模式/倾斜视差 ~18fps (55ms)，静止小猫 ~2fps (500ms)，静止极简天象 1fps (走秒触发)
  bool highFps = clockDemoMode || hourlyChimeActive || happyActive || skyTilting ||
                 (cTheme == CLOCK_STYLE_SKY && meteorActive);
  unsigned long interval = highFps ? 55UL : (cTheme == CLOCK_STYLE_PIXEL_CAT ? 500UL : 1000UL);
  if (now - lastClockAnimMs >= interval) {
    lastClockAnimMs = now;
    return true;
  }
  return false;
}

void DisplayUI::renderClockPage(const IMUSensor &imu, const PowerManager &power,
                                const RTCManager &rtc, const StatsManager &stats,
                                const ConfigManager &config, bool full) {
  (void)imu;
  (void)full;
  for (int i = 0; i < 4; ++i) flipCards[i].active = false;

  const uint8_t cTheme = config.data.clockTheme < 2 ? config.data.clockTheme : 0;
  const unsigned long now = millis();
  uint16_t *fb = gfx->getFramebuffer();

  uint8_t hour = rtc.timeValid ? rtc.getHour() : 22;
  uint8_t minute = rtc.timeValid ? rtc.getMinute() : 0;
  uint8_t second = rtc.timeValid ? rtc.getSecond() : 0;
  bool effectiveTimeValid = rtc.timeValid || clockDemoMode;
  if (clockDemoMode) {
    // 从 05:30 开始每 1 秒推进 45 分钟，约 32 秒完整走完 24 小时
    uint32_t demoTotalMin = (330UL + ((now - clockDemoStartMs) * 45UL) / 1000UL) % 1440UL;
    hour = static_cast<uint8_t>(demoTotalMin / 60UL);
    minute = static_cast<uint8_t>(demoTotalMin % 60UL);
    second = static_cast<uint8_t>((now / 1000UL) % 60UL);
  }
  const float hourF = hour + minute / 60.0f;
  const bool chimeActive = hourlyChimeActive && (now - hourlyChimeStartMs < 3200UL);

  if (cTheme == CLOCK_STYLE_SKY) {
    // ========================================================================
    // 主题 0：极简天象时钟（24h 昼夜渐变 + 日月轨迹/真实月相 + 雾化远山 + 星空流星 + 视差）
    // ========================================================================
    RGB topC, botC;
    sampleSkyGradient(hourF, topC, botC);

    // 陀螺仪视差偏移量（平放时为 0，倾斜时提供柔和景深错位）
    static float smoothPx = 0.0f;
    float targetPx = 0.0f;
    if (imu.isReady) {
      float tx = 0.0f;
      switch (currentRotation) {
        case 0: tx =  imu.ax; break;
        case 1: tx =  imu.ay; break;
        case 2: tx = -imu.ax; break;
        case 3: tx = -imu.ay; break;
      }
      if (fabsf(tx) > 0.10f) targetPx = constrain(tx * 22.0f, -15.0f, 15.0f);
    }
    smoothPx += (targetPx - smoothPx) * 0.25f;
    int pxShift = static_cast<int>(lroundf(smoothPx));

    // 1. 绘制 480x480 垂直平滑渐变天幕（4x4 Bayer 有序抖动消除 RGB565 色带）
    for (int y = 0; y < 480; ++y) {
      float t = static_cast<float>(y) / 479.0f;
      float rF = topC.r + (botC.r - topC.r) * t;
      float gF = topC.g + (botC.g - topC.g) * t;
      float bF = topC.b + (botC.b - topC.b) * t;
      const int8_t *bRow = kBayer4x4[y & 3];
      uint16_t *row = fb + y * 480;
      uint16_t dithered[4];
      for (int k = 0; k < 4; ++k) {
        float d = (bRow[k] - 7.5f) / 16.0f;
        int r = constrain(static_cast<int>(rF + d * 8.0f), 0, 255);
        int g = constrain(static_cast<int>(gF + d * 4.0f), 0, 255);
        int b = constrain(static_cast<int>(bF + d * 8.0f), 0, 255);
        dithered[k] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
      for (int x = 0; x < 480; x += 4) {
        row[x]     = dithered[0];
        row[x + 1] = dithered[1];
        row[x + 2] = dithered[2];
        row[x + 3] = dithered[3];
      }
    }

    // 计算晨昏暖度 (05:30-08:00 与 16:30-19:00 为暖色朝暮)
    float warmth = 0.0f;
    if (hourF >= 5.2f && hourF <= 8.2f) {
      warmth = 1.0f - fabsf(hourF - 6.6f) / 1.6f;
    } else if (hourF >= 16.3f && hourF <= 19.3f) {
      warmth = 1.0f - fabsf(hourF - 17.9f) / 1.6f;
    }
    warmth = constrain(warmth, 0.0f, 1.0f);

    // 2. 深夜星空与偶发流星
    bool isNight = (!effectiveTimeValid || hour >= 20 || hour < 6);
    if (isNight) {
      float tSec = now / 1000.0f;
      int starShift = pxShift / 3;
      for (const auto &st : kStars) {
        int sx = constrain(st.x + starShift, 2, 477);
        int sy = st.y;
        float tw = 0.55f + 0.45f * sinf(tSec * 1.6f + st.phase * 0.08f);
        uint8_t a = static_cast<uint8_t>(st.bright * tw);
        uint16_t starCol = rgb565(225, 238, 255);
        int idx = sy * 480 + sx;
        fb[idx] = blend565(fb[idx], starCol, a);
        if (st.bright > 215 && sx > 0 && sx < 479 && sy > 0 && sy < 479) {
          uint8_t halo = a / 3;
          fb[idx - 1] = blend565(fb[idx - 1], starCol, halo);
          fb[idx + 1] = blend565(fb[idx + 1], starCol, halo);
          fb[idx - 480] = blend565(fb[idx - 480], starCol, halo);
          fb[idx + 480] = blend565(fb[idx + 480], starCol, halo);
        }
      }

      if (meteorStartMs != 0 && now >= meteorStartMs) {
        float p = static_cast<float>(now - meteorStartMs) / 1300.0f;
        if (p >= 0.0f && p <= 1.0f) {
          float headX = meteorStartX + meteorDir * p * 190.0f;
          float headY = meteorStartY + p * 95.0f;
          float fade = (p < 0.2f) ? (p / 0.2f) : (p > 0.7f ? (1.0f - p) / 0.3f : 1.0f);
          for (int k = 0; k < 28; ++k) {
            int mx = static_cast<int>(lroundf(headX - meteorDir * k * 2.1f));
            int my = static_cast<int>(lroundf(headY - k * 1.05f));
            if (mx >= 1 && mx < 479 && my >= 60 && my < 479) {
              uint8_t a = static_cast<uint8_t>(fade * (1.0f - k / 28.0f) * 235.0f);
              uint16_t mc = (k < 4) ? kWhite : rgb565(170, 215, 255);
              fb[my * 480 + mx] = blend565(fb[my * 480 + mx], mc, a);
              fb[(my + 1) * 480 + mx] = blend565(fb[(my + 1) * 480 + mx], mc, a / 2);
            }
          }
        }
      }
    }

    // 3. 真实日月轨迹与月相（躲在雾化远山与时间数字后的远景天体，正午抬高至顶部上方避免遮挡数字）
    if (effectiveTimeValid && hourF >= 5.75f && hourF <= 18.25f) {
      float p = (hourF - 5.75f) / 12.5f;
      int sunX = static_cast<int>(lroundf(58.0f + p * 364.0f)) + pxShift / 2;
      int sunY = static_cast<int>(lroundf(375.0f - sinf(p * kPi) * 283.0f));
      drawSunGlowAndDisc(fb, sunX, sunY, warmth);
    } else {
      float nightF = (hourF >= 18.0f) ? (hourF - 18.0f) : (hourF + 6.0f);
      float p = constrain(nightF / 12.0f, 0.0f, 1.0f);
      int moonX = static_cast<int>(lroundf(64.0f + p * 352.0f)) + pxShift / 2;
      int moonY = static_cast<int>(lroundf(365.0f - sinf(p * kPi) * 270.0f));
      float mPhase = rtc.timeValid
          ? calcMoonPhase(rtc.currentDateTime.getYear(), rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay())
          : 0.35f;
      drawMoonPhaseAndGlow(fb, moonX, moonY, mPhase);
    }

    // 4. 底部三层分形雾化远山剪影（形成天际纵深感）
    drawMistMountains(fb, botC, warmth, pxShift, isNight);

    // 5. 整点星光涟漪动效
    if (chimeActive) {
      float p = static_cast<float>(now - hourlyChimeStartMs) / 3200.0f;
      for (int ring = 0; ring < 2; ++ring) {
        float rp = p - ring * 0.22f;
        if (rp > 0.0f && rp < 1.0f) {
          float radius = 36.0f + rp * 215.0f;
          uint8_t alpha = static_cast<uint8_t>((1.0f - rp) * (1.0f - rp) * 190.0f);
          drawAlphaRing(fb, 240, 224, radius, 4.5f, ring == 0 ? kCyan : kGold, alpha);
        }
      }
    }

    // 6. 透明叠加大号时间（带极淡悬浮投影与冒号温柔呼吸）、秒数与日期星期
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", hour, minute);
    int width = digitTextWidth(timeBuf, 70);
    int secondsWidth = digitTextWidth("00", 23);
    int left = (480 - width - 12 - secondsWidth) / 2;
    if (effectiveTimeValid) {
      drawAlphaDigitsOnFb(fb, timeBuf, left + 3, 168, 70, 116, kBlack, 65, 255);
      drawAlphaDigitsOnFb(fb, timeBuf, left, 165, 70, 116, kWhite, 255, 255);
      snprintf(timeBuf, sizeof(timeBuf), "%02u", second);
      drawAlphaDigitsOnFb(fb, timeBuf, left + width + 14, 167, 23, 39, kBlack, 60);
      drawAlphaDigitsOnFb(fb, timeBuf, left + width + 12, 165, 23, 39, kIce, 255);
    } else {
      centeredText("--:--", 260, 4, kMuted);
    }
    drawDate(rtc, kIce);
  } else {
    // ========================================================================
    // 主题 1：像素小猫 · 全屏时间小世界（方案三：全景大落地窗 + 阳光光束 + 编织地毯）
    // ========================================================================
    CatPeriod period = getCatPeriod(hour, effectiveTimeValid);
    bool happy = (now < petHappyUntilMs);

    RGB skyTop, skyBot, wallC, wallHi, beamC, floorC, floorSeam, deskC, deskDark, curtainC, curtainSh;
    uint16_t timeCol, secCol, accentCol;
    bool lampOn = false;

    if (period == CAT_MORNING) {
      skyTop = {68, 118, 192}; skyBot = {228, 152, 102};
      wallC = {56, 48, 60}; wallHi = {68, 58, 72}; beamC = {74, 52, 42};
      floorC = {92, 64, 48}; floorSeam = {72, 48, 36};
      deskC = {144, 98, 66}; deskDark = {102, 68, 44};
      curtainC = {188, 128, 108}; curtainSh = {142, 92, 78};
      timeCol = rgb565(255, 250, 240); secCol = rgb565(255, 218, 152); accentCol = rgb565(255, 194, 108);
    } else if (period == CAT_WORK) {
      skyTop = {32, 96, 178}; skyBot = {76, 152, 214};
      wallC = {48, 56, 72}; wallHi = {58, 68, 86}; beamC = {68, 52, 40};
      floorC = {86, 62, 46}; floorSeam = {68, 48, 36};
      deskC = {146, 104, 68}; deskDark = {104, 70, 46};
      curtainC = {112, 152, 188}; curtainSh = {82, 116, 148};
      timeCol = rgb565(255, 255, 255); secCol = rgb565(214, 244, 255); accentCol = rgb565(125, 238, 175);
    } else if (period == CAT_SUNSET) {
      skyTop = {56, 36, 98}; skyBot = {216, 96, 56};
      wallC = {52, 36, 48}; wallHi = {64, 44, 58}; beamC = {58, 38, 36};
      floorC = {72, 48, 42}; floorSeam = {54, 34, 30};
      deskC = {124, 80, 56}; deskDark = {86, 54, 38};
      curtainC = {148, 92, 108}; curtainSh = {108, 64, 78};
      timeCol = rgb565(255, 240, 224); secCol = rgb565(255, 188, 136); accentCol = rgb565(255, 168, 112);
      lampOn = true;
    } else {
      skyTop = {10, 16, 38}; skyBot = {24, 36, 74};
      wallC = {24, 28, 44}; wallHi = {30, 36, 54}; beamC = {34, 28, 34};
      floorC = {38, 30, 34}; floorSeam = {26, 20, 24};
      deskC = {66, 50, 44}; deskDark = {44, 34, 30};
      curtainC = {52, 62, 92}; curtainSh = {36, 44, 68};
      timeCol = rgb565(218, 232, 255); secCol = rgb565(152, 188, 244); accentCol = rgb565(142, 178, 236);
      lampOn = true;
    }
    if (happy) accentCol = rgb565(255, 130, 165);

    uint16_t wall565 = rgb565(wallC.r, wallC.g, wallC.b);
    uint16_t wallHi565 = rgb565(wallHi.r, wallHi.g, wallHi.b);
    uint16_t beam565 = rgb565(beamC.r, beamC.g, beamC.b);
    uint16_t floor565 = rgb565(floorC.r, floorC.g, floorC.b);
    uint16_t floorSeam565 = rgb565(floorSeam.r, floorSeam.g, floorSeam.b);
    uint16_t desk565 = rgb565(deskC.r, deskC.g, deskC.b);
    uint16_t deskDark565 = rgb565(deskDark.r, deskDark.g, deskDark.b);

    // 1. 全屏墙壁与木地板 (0..119)
    for (int y = 0; y < 98; ++y) {
      for (int x = 0; x < kSceneW; ++x) spx(x, y, (x % 6 == 0) ? wallHi565 : wall565);
    }
    // 天花板顶部木质横梁 (托住顶部状态栏)
    sRect(0, 0, kSceneW, 14, rgb565(max(0, wallC.r - 12), max(0, wallC.g - 12), max(0, wallC.b - 12)));
    sRect(0, 14, kSceneW, 16, beam565);
    // 墙脚踢脚线与全屏木地板 (y: 98..119)
    sRect(0, 96, kSceneW, 98, deskDark565);
    sRect(0, 98, kSceneW, kSceneH, floor565);
    for (int fy : {103, 108, 113, 118}) sRect(0, fy, kSceneW, fy + 1, floorSeam565);

    // 2. 全景大落地窗 (x: 8..112, y: 17..67)
    const int wx0 = 8, wy0 = 17, wx1 = 112, wy1 = 67;
    for (int y = wy0; y < wy1; ++y) {
      float t = static_cast<float>(y - wy0) / static_cast<float>(wy1 - wy0 - 1);
      RGB sc = lerpRgb(skyTop, skyBot, t);
      uint16_t c565 = rgb565(sc.r, sc.g, sc.b);
      for (int x = wx0; x < wx1; ++x) spx(x, y, c565);
    }

    int cloudDrift = static_cast<int>((now / 2600UL) % 16UL) - 6;
    if (period == CAT_MORNING) {
      sCircle(98, 57, 7.5f, rgb565(255, 230, 125));
      sCloud(24 + cloudDrift / 2, 54, rgb565(255, 228, 212), wx0, wx1);
    } else if (period == CAT_WORK) {
      // 太阳与白云避开中央时间数字区域，让文字极清晰
      sCircle(100, 23, 5.2f, rgb565(255, 244, 175));
      sCloud(28 + cloudDrift / 2, 54, rgb565(235, 246, 255), wx0, wx1);
      sCloud(92 - cloudDrift / 3, 50, rgb565(225, 240, 255), wx0, wx1);
    } else if (period == CAT_SUNSET) {
      sCircle(92, 57, 8.0f, rgb565(255, 202, 92));
      for (int x = wx0; x < wx1; ++x) {
        int mh = static_cast<int>(5.0f + 3.8f * sinf((x - wx0) * 0.11f) + 1.8f * cosf((x - wx0) * 0.25f));
        for (int y = wy1 - mh; y < wy1; ++y) spx(x, y, rgb565(56, 32, 58));
      }
      int birdX = 20 + ((now / 1200) % 2);
      spx(birdX - 1, 22, rgb565(45, 25, 40)); spx(birdX, 23, rgb565(45, 25, 40)); spx(birdX + 1, 22, rgb565(45, 25, 40));
    } else {
      // 深夜弯月 + 呼吸繁星 + 窗外流星
      sCircle(22, 24, 4.8f, rgb565(250, 236, 168));
      RGB midSky = lerpRgb(skyTop, skyBot, 0.20f);
      sCircle(24.5f, 22.5f, 4.2f, rgb565(midSky.r, midSky.g, midSky.b));
      const uint8_t sPts[][2] = {
        {38,21},{54,19},{72,22},{88,20},{102,24},{20,48},{102,48},{64,53}
      };
      for (int i = 0; i < 8; ++i) {
        bool dim = ((now / 900 + i) % 3 == 0);
        spx(sPts[i][0], sPts[i][1], dim ? rgb565(120, 148, 205) : rgb565(255, 250, 222));
      }
      if (meteorStartMs != 0 && now >= meteorStartMs) {
        float p = static_cast<float>(now - meteorStartMs) / 1300.0f;
        if (p >= 0.0f && p <= 1.0f) {
          int mx = 98 - static_cast<int>(p * 52.0f);
          int my = 20 + static_cast<int>(p * 16.0f);
          for (int k = 0; k < 7; ++k) {
            int px = mx + k, py = my - k / 2;
            if (px >= wx0 && px < wx1 && py >= wy0 && py < wy1) {
              spxBlend(px, py, kWhite, static_cast<uint8_t>((7 - k) * 35));
            }
          }
        }
      }
    }

    // 整点窗景柔和金色微光
    if (chimeActive) {
      float p = static_cast<float>(now - hourlyChimeStartMs) / 3200.0f;
      uint8_t glowA = static_cast<uint8_t>(sinf(p * kPi) * 85.0f);
      for (int y = wy0; y < wy1; ++y)
        for (int x = wx0; x < wx1; ++x)
          spxBlend(x, y, kGold, glowA);
    }

    // 3. 通透无遮挡全景落地窗 + 两侧弧形束起窗帘（带小绑带）+ 宽大木质窗台
    uint16_t frameC = (period == CAT_SLEEP) ? rgb565(64, 58, 76) : rgb565(156, 118, 84);
    uint16_t frameHi = (period == CAT_SLEEP) ? rgb565(82, 74, 96) : rgb565(186, 146, 106);
    uint16_t cur565 = rgb565(curtainC.r, curtainC.g, curtainC.b);
    uint16_t curSh565 = rgb565(curtainSh.r, curtainSh.g, curtainSh.b);
    uint16_t tieCol = (period == CAT_SLEEP) ? rgb565(145, 130, 105) : rgb565(242, 208, 128);
    for (int y = wy0; y < wy1; ++y) {
      int rel = y - wy0;
      // 束腰弧度：顶部宽 7px -> 中段 26px 处束紧为 3px -> 下摆微散至 5px
      int cw = 5;
      if (rel < 26) cw = 7 - (rel * 4) / 26;
      else cw = 3 + ((rel - 26) * 2) / 23;
      for (int dx = 0; dx < cw; ++dx) {
        spx(wx0 + dx, y, (dx % 2 == 0) ? cur565 : curSh565);
        spx(wx1 - 1 - dx, y, (dx % 2 == 0) ? cur565 : curSh565);
      }
      if (rel == 26) {
        sRect(wx0, y, wx0 + cw, y + 1, tieCol);
        sRect(wx1 - cw, y, wx1, y + 1, tieCol);
      }
    }
    uint16_t borderC = chimeActive ? kGold : frameC;
    sRect(wx0 - 1, wy0 - 1, wx1 + 1, wy0 + 1, borderC);
    sRect(wx0 - 1, wy0, wx0 + 1, wy1, borderC);
    sRect(wx1 - 1, wy0, wx1 + 1, wy1, borderC);
    sRect(wx0 - 3, wy1, wx1 + 3, wy1 + 2, frameHi);
    sRect(wx0 - 3, wy1 + 2, wx1 + 3, wy1 + 3, frameC);

    // 4. 在全景窗景中央绘制带深色全描边的圆润粗体像素时间（任何天色下都无比清晰）
    char hmStr[8];
    if (effectiveTimeValid) snprintf(hmStr, sizeof(hmStr), "%02u:%02u", hour, minute);
    else strcpy(hmStr, "--:--");
    uint16_t shadow565 = rgb565(12, 20, 38);
    if (effectiveTimeValid) {
      drawPixText(23, 27, hmStr, 2, timeCol, shadow565, 220, 255, true);
      char secStr[4];
      snprintf(secStr, sizeof(secStr), "%02u", second);
      drawPixText(92, 29, secStr, 1, secCol, shadow565, 200, 255, true);
    } else {
      sRect(36, 36, 48, 38, timeCol); sRect(52, 36, 64, 38, timeCol);
    }

    // 5. 窗台下方暖色“阳光斜射光束”
    if (period == CAT_MORNING || period == CAT_WORK) {
      uint16_t sunBeamCol = (period == CAT_MORNING) ? rgb565(255, 218, 165) : rgb565(255, 244, 210);
      for (int y = wy1 + 3; y < 114; ++y) {
        float shift = (y - (wy1 + 3)) * 0.55f;
        int bx0 = static_cast<int>(16 + shift);
        int bx1 = static_cast<int>(54 + shift);
        uint8_t a = static_cast<uint8_t>(26.0f * (1.0f - (y - 70) / 55.0f));
        for (int x = bx0; x < bx1 && x < kSceneW; ++x) spxBlend(x, y, sunBeamCol, a);
      }
    }

    // 6. 窗台专注成长小盆栽（左窗台 x=15..24, y=59..67）
    sRect(16, 63, 23, 67, period == CAT_SLEEP ? rgb565(85, 48, 42) : rgb565(185, 92, 62));
    sRect(15, 62, 24, 63, period == CAT_SLEEP ? rgb565(98, 56, 48) : rgb565(205, 108, 74));
    uint16_t leafC = (period == CAT_SLEEP) ? rgb565(36, 95, 58) : rgb565(68, 192, 88);
    uint16_t todayCnt = stats.getTodayCount(rtc.getDateYyyyMmDd());
    spx(19, 61, leafC); spx(19, 60, leafC); spx(18, 60, leafC);
    if (todayCnt >= 1) {
      spx(20, 60, leafC); spx(17, 59, leafC); spx(21, 59, leafC); spx(19, 59, leafC); spx(19, 58, leafC);
    }
    if (todayCnt >= 3) {
      spx(17, 58, rgb565(255, 225, 90));
      spx(21, 58, rgb565(255, 225, 90));
    }
    if (todayCnt >= 5) {
      spx(17, 59, rgb565(255, 75, 75));
      spx(21, 59, rgb565(255, 75, 75));
      spx(19, 57, rgb565(255, 95, 85));
    }

    // 7. 地板复古菱形暗纹编织小地毯 (x: 17..51, y: 103..113) + 旁侧小猫陶瓷水碗
    uint16_t rugBase = (period == CAT_SLEEP) ? rgb565(64, 58, 70) : rgb565(192, 170, 138);
    uint16_t rugInner = (period == CAT_SLEEP) ? rgb565(78, 70, 86) : rgb565(214, 190, 154);
    uint16_t rugPattern = (period == CAT_SLEEP) ? rgb565(94, 82, 98) : rgb565(172, 126, 104);
    uint16_t rugTrim = (period == CAT_SLEEP) ? rgb565(104, 86, 86) : rgb565(212, 118, 108);
    sRect(18, 104, 50, 113, rugBase);
    sRect(20, 105, 48, 112, rugInner);
    for (int y = 104; y < 113; y += 2) { spx(17, y, rugTrim); spx(50, y, rugTrim); }
    // 编织几何小菱形花纹
    for (int cx : {26, 34, 42}) {
      spx(cx, 106, rugPattern); spx(cx, 110, rugPattern);
      spx(cx - 1, 107, rugPattern); spx(cx + 1, 107, rugPattern);
      spx(cx - 2, 108, rugPattern); spx(cx + 2, 108, rugPattern);
      spx(cx - 1, 109, rugPattern); spx(cx + 1, 109, rugPattern);
    }

    // 地面小猫水碗 (x: 54..61, y: 102..106)
    uint16_t bowlBody = (period == CAT_SLEEP) ? rgb565(118, 124, 138) : rgb565(232, 232, 226);
    uint16_t bowlRim = (period == CAT_SLEEP) ? rgb565(90, 96, 112) : rgb565(196, 196, 190);
    uint16_t waterCol = (period == CAT_SLEEP) ? rgb565(56, 88, 128) : rgb565(108, 192, 240);
    sRect(54, 103, 62, 106, bowlBody);
    sRect(55, 106, 61, 107, bowlRim);
    sRect(55, 102, 61, 104, bowlRim);
    sRect(56, 102, 60, 103, waterCol);

    // 8. 书桌 / 小夜灯猫窝 & 橘白小猫主角
    bool blink = ((now % 4800UL) < 170UL);
    int tailSwing = ((now / 900UL) % 2 == 0) ? 0 : 1;
    int eyeShift = 0;

    if (period == CAT_SLEEP && !happy && !chimeActive) {
      sRect(50, 96, 58, 108, rgb565(68, 48, 38));
      for (int y = 82; y < 100; ++y) {
        for (int x = 42; x < 67; ++x) {
          float dist = hypotf(x - 54.0f, y - 90.0f);
          if (dist < 11.0f) {
            uint8_t a = static_cast<uint8_t>((1.0f - dist / 11.0f) * 105.0f);
            spxBlend(x, y, rgb565(255, 215, 115), a);
          }
        }
      }
      sRect(53, 92, 55, 96, rgb565(180, 150, 95));
      sRect(51, 88, 57, 92, rgb565(255, 225, 130));
      drawPixelCatSprite(84, 101, CAT_SLEEP, false, false, false, 0, 0, now);
    } else {
      sRect(58, 89, 110, 92, desk565);
      sRect(58, 92, 110, 94, deskDark565);
      sRect(62, 94, 65, 106, deskDark565);
      sRect(103, 94, 106, 106, deskDark565);

      sRect(62, 84, 67, 89, rgb565(240, 238, 232));
      spx(67, 85, rgb565(240, 238, 232));
      spx(67, 87, rgb565(240, 238, 232));
      spx(68, 86, rgb565(240, 238, 232));
      if (period == CAT_MORNING || period == CAT_WORK) {
        int stOff = ((now / 700UL) % 2 == 0) ? 0 : -1;
        spx(63, 82 + stOff, rgb565(210, 225, 240));
        spx(64, 80 + stOff, rgb565(210, 225, 240));
        spx(65, 82 - stOff, rgb565(210, 225, 240));
        spx(66, 79 - stOff, rgb565(210, 225, 240));
      }

      if (period == CAT_WORK) {
        sRect(69, 87, 77, 89, rgb565(245, 242, 230));
        sRect(73, 87, 74, 89, rgb565(180, 175, 165));
      }

      if (lampOn || chimeActive) {
        for (int y = 71; y < 91; ++y) {
          for (int x = 90; x < 110; ++x) {
            float dist = hypotf(x - 101.0f, y - 80.0f);
            if (dist < 11.0f) {
              uint8_t a = static_cast<uint8_t>((1.0f - dist / 11.0f) * 112.0f);
              spxBlend(x, y, rgb565(255, 210, 110), a);
            }
          }
        }
      }
      sRect(99, 88, 104, 89, rgb565(120, 125, 138));
      sRect(101, 82, 102, 88, rgb565(150, 155, 168));
      sRect(98, 79, 104, 82, (lampOn || chimeActive) ? rgb565(255, 225, 130) : rgb565(165, 175, 190));

      drawPixelCatSprite(84, 81, period, happy, chimeActive, blink, tailSwing, eyeShift, now);
    }

    blitPixelScene4x(fb);

    // 清晰居中中文日期叠加在窗景下沿（带微投影，去掉了番茄计数更清爽）
    char dateChip[32];
    if (rtc.timeValid) {
      const char *weekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
      uint8_t wk = rtc.currentDateTime.getWeek();
      snprintf(dateChip, sizeof(dateChip), "%02u月%02u日 %s",
               rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay(),
               wk < 7 ? weekdays[wk] : "");
    } else {
      strcpy(dateChip, "等待校时");
    }
    gfx->setTextSize(1);
    int16_t tx, ty; uint16_t tw, th;
    gfx->getTextBounds(dateChip, 0, 246, &tx, &ty, &tw, &th);
    int cx = 240 - static_cast<int>(tw) / 2 - tx;
    gfx->setTextColor(shadow565);
    gfx->setCursor(cx - 1, 246); gfx->print(dateChip);
    gfx->setCursor(cx + 1, 246); gfx->print(dateChip);
    gfx->setCursor(cx, 245); gfx->print(dateChip);
    gfx->setCursor(cx, 247); gfx->print(dateChip);
    gfx->setTextColor(timeCol);
    gfx->setCursor(cx, 246);
    gfx->print(dateChip);
  }

  // 顶部导航栏、左下角模式切换按钮与右下角时钟主题切换按钮（均叠加在天幕/背景之上）
  drawHeader(power, rtc);
  if (clockDemoMode) {
    centeredText("24H 昼夜演示中 · 点击时间退出",
                 cTheme == CLOCK_STYLE_SKY ? 132 : 60, 1, kGold);
  }
  bool sch = config.data.scheduleMode;
  gfx->fillCircle(36, 444, 22, sch ? 0x0247 : 0x0125);
  gfx->drawCircle(36, 444, 22, sch ? kGold : kCyan);
  drawModeSwitchIcon(36, 444, sch, sch ? kGold : kIce);
  drawClockStyleButton(cTheme);
  if (sch && schedState.popupVisible) {
    drawSchedulePopup(rtc);
  }

  dirty.full();
  lastClockTheme = cTheme;
}
