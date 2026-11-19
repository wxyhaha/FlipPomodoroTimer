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

// 将抗锯齿 7 段数字直接 alpha-blend 到当前 framebuffer（无缝融入渐变天空）
void drawAlphaDigitsOnFb(uint16_t *fb, const char *text, int x, int y, int w, int h, uint16_t color) {
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
      for (int k = 0; k < 2; ++k) {
        int cy = ys[k];
        for (int dy = -r - 1; dy <= r + 1; ++dy) {
          int py = cy + dy;
          if (py < 0 || py >= 480) continue;
          for (int dx = -r - 1; dx <= r + 1; ++dx) {
            int px = cx + dx;
            if (px < 0 || px >= 480) continue;
            float d = sqrtf(static_cast<float>(dx * dx + dy * dy));
            if (d <= r) fb[py * 480 + px] = color;
            else if (d < r + 1.0f) {
              uint8_t a = static_cast<uint8_t>((1.0f - (d - r)) * 255.0f);
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
          if (alpha) dstRow[dstX] = blend565(dstRow[dstX], color, alpha);
        }
      }
    }
    x += cw + w / 10;
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
// 像素小猫小屋逻辑像素绘制器 (120 x 56 逻辑像素 -> 480 x 224 屏幕区域 y=212..435)
// ============================================================================
constexpr int kSceneW = 120;
constexpr int kSceneH = 56;
constexpr int kSceneScreenY = 212;
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
      uint16_t gCol = rgb565(240, 205, 95);
      for (int dx = -6; dx <= -2; ++dx) { spx(cx + dx, hcy - 2, gCol); spx(cx + dx, hcy + 2, gCol); }
      for (int dx = 2; dx <= 6; ++dx) { spx(cx + dx, hcy - 2, gCol); spx(cx + dx, hcy + 2, gCol); }
      for (int dy = -2; dy <= 2; ++dy) {
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

void blitPixelScene4x(uint16_t *fb) {
  for (int sy = 0; sy < kSceneH; ++sy) {
    const uint16_t *src = sSceneBuf + sy * kSceneW;
    int baseY = kSceneScreenY + sy * 4;
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
  // 小屋区域（避开底部左右两个按钮）
  return x >= 40 && x < 440 && y >= 212 && y < 416;
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
  if (currentMode != UI_MODE_CLOCK) return false;
  unsigned long now = millis();

  if (hourlyChimeActive && (now - hourlyChimeStartMs >= 3200UL)) {
    hourlyChimeActive = false;
    needsRedraw = true;
  }

  uint8_t cTheme = config.data.clockTheme < 2 ? config.data.clockTheme : 0;
  bool nightSky = !rtc.timeValid || rtc.getHour() >= 20 || rtc.getHour() < 6;

  // 深夜偶发流星调度（每 16..24 秒一次，持续 1300ms）
  if (nightSky) {
    if (nextMeteorMs == 0) nextMeteorMs = now + 5000UL;
    if (meteorStartMs == 0 && now >= nextMeteorMs) {
      meteorStartMs = now;
      meteorStartX = 280 + static_cast<int16_t>((now * 37UL) % 150UL);
      meteorStartY = 72 + static_cast<int16_t>((now * 19UL) % 80UL);
      meteorDir = -1;
    } else if (meteorStartMs != 0 && now - meteorStartMs >= 1300UL) {
      meteorStartMs = 0;
      nextMeteorMs = now + 16000UL + (now % 8000UL);
    }
  } else {
    meteorStartMs = 0;
  }

  bool meteorActive = (meteorStartMs != 0);
  bool happyActive = (now < petHappyUntilMs);

  // 像素小猫毛线球物理更新（倾斜超过死区时驱动，带惯性阻尼）
  bool yarnMoving = false;
  if (cTheme == CLOCK_STYLE_PIXEL_CAT) {
    float tiltX = 0.0f;
    if (imu.isReady) {
      switch (currentRotation) {
        case 0: tiltX =  imu.ax; break;
        case 1: tiltX =  imu.ay; break;
        case 2: tiltX = -imu.ax; break;
        case 3: tiltX = -imu.ay; break;
      }
    }
    if (fabsf(tiltX) < 0.14f) tiltX = 0.0f;
    if (lastYarnUpdateMs == 0) lastYarnUpdateMs = now;
    unsigned long dtMs = now - lastYarnUpdateMs;
    if (dtMs >= 45) {
      float dt = min(0.12f, dtMs / 1000.0f);
      lastYarnUpdateMs = now;
      yarnVx += tiltX * 48.0f * dt;
      yarnVx *= 0.86f;
      if (fabsf(yarnVx) < 0.08f && tiltX == 0.0f) yarnVx = 0.0f;
      yarnX += yarnVx * dt * 12.0f;
      if (yarnX < 17.0f) { yarnX = 17.0f; yarnVx = -yarnVx * 0.45f; }
      if (yarnX > 48.0f) { yarnX = 48.0f; yarnVx = -yarnVx * 0.45f; }
    }
    yarnMoving = (fabsf(yarnVx) > 0.12f || fabsf(tiltX) > 0.14f);
  }

  // 自适应节能帧率：活跃动效 ~18fps (55ms)，静止小猫 ~2fps (500ms)，静止极简天象 1fps (走秒触发)
  bool highFps = hourlyChimeActive || happyActive || yarnMoving || (cTheme == CLOCK_STYLE_SKY && meteorActive);
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

  const uint8_t hour = rtc.timeValid ? rtc.getHour() : 22;
  const uint8_t minute = rtc.timeValid ? rtc.getMinute() : 0;
  const uint8_t second = rtc.timeValid ? rtc.getSecond() : 0;
  const float hourF = hour + minute / 60.0f;
  const bool chimeActive = hourlyChimeActive && (now - hourlyChimeStartMs < 3200UL);

  if (cTheme == CLOCK_STYLE_SKY) {
    // ========================================================================
    // 主题 0：极简天象时钟（24 小时昼夜渐变 + 星空微闪 + 流星 + 整点星光涟漪）
    // ========================================================================
    RGB topC, botC;
    sampleSkyGradient(hourF, topC, botC);

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

    // 2. 深夜星空与偶发流星
    bool isNight = (!rtc.timeValid || hour >= 20 || hour < 6);
    if (isNight) {
      float tSec = now / 1000.0f;
      for (const auto &st : kStars) {
        float tw = 0.55f + 0.45f * sinf(tSec * 1.6f + st.phase * 0.08f);
        uint8_t a = static_cast<uint8_t>(st.bright * tw);
        uint16_t starCol = rgb565(225, 238, 255);
        int idx = st.y * 480 + st.x;
        fb[idx] = blend565(fb[idx], starCol, a);
        if (st.bright > 215 && st.x > 0 && st.x < 479 && st.y > 0 && st.y < 479) {
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

    // 3. 整点星光涟漪动效
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

    // 4. 透明叠加大号时间、秒数与日期星期
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", hour, minute);
    int width = digitTextWidth(timeBuf, 70);
    int secondsWidth = digitTextWidth("00", 23);
    int left = (480 - width - 12 - secondsWidth) / 2;
    if (rtc.timeValid) {
      drawAlphaDigitsOnFb(fb, timeBuf, left, 165, 70, 116, kWhite);
      snprintf(timeBuf, sizeof(timeBuf), "%02u", second);
      drawAlphaDigitsOnFb(fb, timeBuf, left + width + 12, 165, 23, 39, kIce);
    } else {
      centeredText("--:--", 260, 4, kMuted);
    }
    drawDate(rtc, kIce);
  } else {
    // ========================================================================
    // 主题 1：像素小猫 · 时间小世界（底部去文字留白 + 统计联动盆栽 + 交互彩蛋）
    // ========================================================================
    CatPeriod period = getCatPeriod(hour, rtc.timeValid);
    bool happy = (now < petHappyUntilMs);

    RGB bgC, skyTop, skyBot, wallC, wallHi, floorC, deskC, deskDark;
    uint16_t timeCol, accentCol;
    bool lampOn = false;

    if (period == CAT_MORNING) {
      bgC = {18, 22, 34}; skyTop = {110, 155, 225}; skyBot = {255, 188, 130};
      wallC = {52, 46, 56}; wallHi = {66, 58, 70}; floorC = {78, 56, 44};
      deskC = {132, 92, 62}; deskDark = {92, 62, 42};
      timeCol = rgb565(255, 246, 232); accentCol = rgb565(255, 178, 92);
    } else if (period == CAT_WORK) {
      bgC = {14, 20, 30}; skyTop = {62, 152, 235}; skyBot = {158, 218, 252};
      wallC = {46, 54, 68}; wallHi = {58, 68, 86}; floorC = {72, 54, 42};
      deskC = {138, 98, 66}; deskDark = {95, 65, 44};
      timeCol = rgb565(245, 250, 255); accentCol = rgb565(72, 215, 135);
    } else if (period == CAT_SUNSET) {
      bgC = {20, 14, 24}; skyTop = {75, 52, 118}; skyBot = {250, 122, 72};
      wallC = {52, 36, 46}; wallHi = {68, 46, 56}; floorC = {64, 42, 38};
      deskC = {118, 76, 54}; deskDark = {82, 50, 36};
      timeCol = rgb565(255, 232, 212); accentCol = rgb565(255, 132, 82);
      lampOn = true;
    } else {
      bgC = {8, 10, 18}; skyTop = {12, 18, 42}; skyBot = {26, 38, 76};
      wallC = {24, 28, 44}; wallHi = {32, 38, 56}; floorC = {34, 28, 32};
      deskC = {62, 48, 42}; deskDark = {42, 32, 28};
      timeCol = rgb565(175, 195, 228); accentCol = rgb565(115, 155, 230);
      lampOn = true;
    }
    if (happy) accentCol = rgb565(255, 105, 140);

    uint16_t bg565 = rgb565(bgC.r, bgC.g, bgC.b);
    gfx->fillScreen(bg565);

    // 1. 上半区清爽时间 + 日期/今日专注数
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", hour, minute);
    int tw = digitTextWidth(timeBuf, 64);
    int sw = digitTextWidth("00", 23);
    int left = (480 - tw - 10 - sw) / 2;
    if (rtc.timeValid) {
      drawAlphaDigitsOnFb(fb, timeBuf, left, 68, 64, 104, timeCol);
      snprintf(timeBuf, sizeof(timeBuf), "%02u", second);
      drawAlphaDigitsOnFb(fb, timeBuf, left + tw + 10, 74, 23, 39, accentCol);
    } else {
      centeredText("--:--", 150, 3, kMuted);
    }

    uint16_t todayCnt = stats.getTodayCount(rtc.getDateYyyyMmDd());
    char subLine[48];
    if (rtc.timeValid) {
      const char *weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
      uint8_t wk = rtc.currentDateTime.getWeek();
      snprintf(subLine, sizeof(subLine), "%02u-%02u %s  ·  今日专注 %u",
               rtc.currentDateTime.getMonth(), rtc.currentDateTime.getDay(),
               wk < 7 ? weekdays[wk] : "", todayCnt);
    } else {
      snprintf(subLine, sizeof(subLine), "等待校时  ·  今日专注 %u", todayCnt);
    }
    centeredText(subLine, 198, 1, accentCol);

    // 2. 绘制 120x56 像素小屋到局部缓冲
    sRect(0, 0, kSceneW, kSceneH, bg565);
    const int rx0 = 10, ry0 = 1, rx1 = 110, ry1 = 55;
    uint16_t borderCol = chimeActive ? kGold : (period == CAT_SLEEP ? rgb565(38, 46, 72) : rgb565(70, 82, 105));
    sRect(rx0 - 1, ry0 - 1, rx1 + 1, ry1 + 1, borderCol);

    uint16_t wall565 = rgb565(wallC.r, wallC.g, wallC.b);
    uint16_t wallHi565 = rgb565(wallHi.r, wallHi.g, wallHi.b);
    uint16_t floor565 = rgb565(floorC.r, floorC.g, floorC.b);
    uint16_t floorSeam = rgb565(max(0, floorC.r - 12), max(0, floorC.g - 10), max(0, floorC.b - 8));
    uint16_t desk565 = rgb565(deskC.r, deskC.g, deskC.b);
    uint16_t deskDark565 = rgb565(deskDark.r, deskDark.g, deskDark.b);

    for (int y = ry0; y < 41; ++y) {
      for (int x = rx0; x < rx1; ++x) spx(x, y, (x % 6 == 0) ? wallHi565 : wall565);
    }
    sRect(rx0, 40, rx1, 42, deskDark565);
    sRect(rx0, 42, rx1, ry1, floor565);
    sRect(rx0, 46, rx1, 47, floorSeam);
    sRect(rx0, 51, rx1, 52, floorSeam);

    // 3. 左侧窗户 (x: 16..54, y: 5..32)
    const int wx0 = 16, wy0 = 5, wx1 = 54, wy1 = 32;
    for (int y = wy0; y < wy1; ++y) {
      float t = static_cast<float>(y - wy0) / static_cast<float>(wy1 - wy0 - 1);
      RGB sc = lerpRgb(skyTop, skyBot, t);
      uint16_t c565 = rgb565(sc.r, sc.g, sc.b);
      for (int x = wx0; x < wx1; ++x) spx(x, y, c565);
    }

    int cloudDrift = static_cast<int>((now / 2500UL) % 12UL) - 4;
    if (period == CAT_MORNING) {
      sCircle(42, 28, 5.8f, rgb565(255, 228, 120));
      sCloud(25 + cloudDrift / 2, 13, rgb565(255, 232, 215), wx0, wx1);
    } else if (period == CAT_WORK) {
      sCloud(26 + cloudDrift / 2, 12, rgb565(250, 252, 255), wx0, wx1);
      sCloud(43 - cloudDrift / 3, 18, rgb565(240, 248, 255), wx0, wx1);
    } else if (period == CAT_SUNSET) {
      sCircle(35, 28, 6.2f, rgb565(255, 200, 88));
      for (int x = wx0; x < wx1; ++x) {
        int mh = static_cast<int>(4.0f + 2.8f * sinf((x - wx0) * 0.22f));
        for (int y = wy1 - mh; y < wy1; ++y) spx(x, y, rgb565(55, 34, 58));
      }
      int birdX = 24 + ((now / 1200) % 2);
      spx(birdX - 1, 12, rgb565(45, 25, 40)); spx(birdX, 13, rgb565(45, 25, 40)); spx(birdX + 1, 12, rgb565(45, 25, 40));
      spx(birdX + 4, 15, rgb565(45, 25, 40)); spx(birdX + 5, 16, rgb565(45, 25, 40)); spx(birdX + 6, 15, rgb565(45, 25, 40));
    } else {
      // 深夜弯月 + 闪烁星点 + 窗外流星
      sCircle(25, 12, 3.8f, rgb565(250, 236, 165));
      RGB midSky = lerpRgb(skyTop, skyBot, 0.26f);
      sCircle(27, 11, 3.3f, rgb565(midSky.r, midSky.g, midSky.b));
      const uint8_t sPts[5][2] = {{37,9},{46,13},{33,19},{48,23},{22,23}};
      for (int i = 0; i < 5; ++i) {
        bool dim = ((now / 900 + i) % 3 == 0);
        spx(sPts[i][0], sPts[i][1], dim ? rgb565(130, 155, 210) : rgb565(255, 250, 220));
      }
      if (meteorStartMs != 0 && now >= meteorStartMs) {
        float p = static_cast<float>(now - meteorStartMs) / 1300.0f;
        if (p >= 0.0f && p <= 1.0f) {
          int mx = 48 - static_cast<int>(p * 24.0f);
          int my = 9 + static_cast<int>(p * 12.0f);
          for (int k = 0; k < 5; ++k) {
            int px = mx + k, py = my - k / 2;
            if (px >= wx0 && px < wx1 && py >= wy0 && py < wy1) {
              spxBlend(px, py, kWhite, static_cast<uint8_t>((5 - k) * 48));
            }
          }
        }
      }
    }

    // 整点窗户柔和金色微光
    if (chimeActive) {
      float p = static_cast<float>(now - hourlyChimeStartMs) / 3200.0f;
      uint8_t glowA = static_cast<uint8_t>(sinf(p * kPi) * 90.0f);
      for (int y = wy0; y < wy1; ++y)
        for (int x = wx0; x < wx1; ++x)
          spxBlend(x, y, kGold, glowA);
    }

    // 十字木窗框
    uint16_t frameC = (period == CAT_SLEEP) ? rgb565(62, 56, 72) : rgb565(155, 118, 86);
    sRect(wx0 - 1, wy0 - 1, wx1 + 1, wy0 + 1, frameC);
    sRect(wx0 - 1, wy1 - 1, wx1 + 1, wy1 + 2, frameC);
    sRect(wx0 - 1, wy0, wx0 + 1, wy1, frameC);
    sRect(wx1 - 1, wy0, wx1 + 1, wy1, frameC);
    sRect((wx0 + wx1) / 2, wy0, (wx0 + wx1) / 2 + 1, wy1, frameC);
    sRect(wx0, (wy0 + wy1) / 2, wx1, (wy0 + wy1) / 2 + 1, frameC);

    // 4. 窗台专注成长小盆栽（与今日专注次数联动）
    sRect(19, 28, 26, 32, period == CAT_SLEEP ? rgb565(85, 48, 42) : rgb565(185, 92, 62));
    sRect(18, 27, 27, 28, period == CAT_SLEEP ? rgb565(98, 56, 48) : rgb565(205, 108, 74));
    uint16_t leafC = (period == CAT_SLEEP) ? rgb565(36, 95, 58) : rgb565(68, 192, 88);
    // 0 颗：小嫩芽
    spx(22, 26, leafC); spx(22, 25, leafC); spx(21, 25, leafC);
    if (todayCnt >= 1) { // 1~2 颗：茂盛绿叶
      spx(23, 25, leafC); spx(20, 24, leafC); spx(24, 24, leafC); spx(22, 24, leafC); spx(22, 23, leafC);
    }
    if (todayCnt >= 3) { // 3~4 颗：开花
      spx(20, 23, rgb565(255, 225, 90));
      spx(24, 23, rgb565(255, 225, 90));
    }
    if (todayCnt >= 5) { // 5 颗以上：结出小红番茄果子
      spx(20, 24, rgb565(255, 75, 75));
      spx(24, 24, rgb565(255, 75, 75));
      spx(22, 22, rgb565(255, 95, 85));
    }

    // 5. 地板粉色毛线球（QMI8658 重力感应左右滚动！）
    int yBallX = static_cast<int>(lroundf(yarnX));
    if (period != CAT_SLEEP || happy) {
      sCircle(static_cast<float>(yBallX), 46.0f, 3.2f, rgb565(235, 105, 130), true, rgb565(135, 45, 65));
      // 随位置旋转的小线头
      int tailDir = (yarnVx >= 0.0f) ? -1 : 1;
      spx(yBallX + tailDir * 3, 48, rgb565(235, 105, 130));
      spx(yBallX + tailDir * 4, 49, rgb565(235, 105, 130));
      spx(yBallX + tailDir * 5, 49, rgb565(235, 105, 130));
    }

    // 6. 家具与主角小猫
    bool blink = ((now % 4800UL) < 170UL);
    int tailSwing = ((now / 900UL) % 2 == 0) ? 0 : 1;
    int eyeShift = (yBallX < 26) ? -1 : ((yBallX > 40) ? 1 : 0);

    if (period == CAT_SLEEP && !happy && !chimeActive) {
      // 床头柜与暖黄小夜灯
      sRect(46, 35, 54, 46, rgb565(68, 48, 38));
      for (int y = 21; y < 39; ++y) {
        for (int x = 38; x < 63; ++x) {
          float dist = hypotf(x - 50.0f, y - 29.0f);
          if (dist < 11.0f && x >= rx0 && x < rx1 && y >= ry0 && y < ry1) {
            uint8_t a = static_cast<uint8_t>((1.0f - dist / 11.0f) * 105.0f);
            spxBlend(x, y, rgb565(255, 215, 115), a);
          }
        }
      }
      sRect(49, 31, 51, 35, rgb565(180, 150, 95));
      sRect(47, 27, 53, 31, rgb565(255, 225, 130));
      drawPixelCatSprite(80, 39, CAT_SLEEP, false, false, false, 0, 0, now);
    } else {
      // 书桌
      sRect(56, 33, 106, 36, desk565);
      sRect(56, 36, 106, 38, deskDark565);
      sRect(60, 38, 63, 50, deskDark565);
      sRect(100, 38, 103, 50, deskDark565);

      // 桌上咖啡杯与飘动热气
      sRect(60, 28, 65, 33, rgb565(240, 238, 232));
      spx(65, 29, rgb565(240, 238, 232));
      spx(65, 31, rgb565(240, 238, 232));
      spx(66, 30, rgb565(240, 238, 232));
      if (period == CAT_MORNING || period == CAT_WORK) {
        int stOff = ((now / 700UL) % 2 == 0) ? 0 : -1;
        spx(61, 26 + stOff, rgb565(210, 225, 240));
        spx(62, 24 + stOff, rgb565(210, 225, 240));
        spx(63, 26 - stOff, rgb565(210, 225, 240));
        spx(64, 23 - stOff, rgb565(210, 225, 240));
      }

      if (period == CAT_WORK) {
        sRect(67, 31, 75, 33, rgb565(245, 242, 230));
        sRect(71, 31, 72, 33, rgb565(180, 175, 165));
      }

      // 桌上台灯
      if (lampOn || chimeActive) {
        for (int y = 15; y < 35; ++y) {
          for (int x = 88; x < 108; ++x) {
            float dist = hypotf(x - 99.0f, y - 24.0f);
            if (dist < 11.0f && x >= rx0 && x < rx1 && y >= ry0 && y < ry1) {
              uint8_t a = static_cast<uint8_t>((1.0f - dist / 11.0f) * 112.0f);
              spxBlend(x, y, rgb565(255, 210, 110), a);
            }
          }
        }
      }
      sRect(97, 32, 102, 33, rgb565(120, 125, 138));
      sRect(99, 26, 100, 32, rgb565(150, 155, 168));
      sRect(96, 23, 102, 26, (lampOn || chimeActive) ? rgb565(255, 225, 130) : rgb565(165, 175, 190));

      drawPixelCatSprite(82, 25, period, happy, chimeActive, blink, tailSwing, eyeShift, now);
    }

    blitPixelScene4x(fb);
  }

  // 顶部导航栏、左下角息屏按钮、右下角时钟主题切换按钮（均叠加在天幕/背景之上）
  drawHeader(power, rtc);
  gfx->fillCircle(36, 444, 22, 0x0125);
  gfx->drawCircle(36, 444, 22, kCyan);
  drawScreenOffIcon(36, 444, kIce);
  drawClockStyleButton(cTheme);

  dirty.full();
  lastClockTheme = cTheme;
}
