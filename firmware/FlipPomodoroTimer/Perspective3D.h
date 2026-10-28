#pragma once
#include <Arduino.h>
#include <math.h>
#include "display/Arduino_RGB_Display.h"

class Perspective3D {
public:
  bool isReady = false;
  bool isTilting = false;

  bool init(Arduino_RGB_Display *display) {
    gfx = display;
    if (!gfx) return false;

    // 在 8MB PSRAM 中申请 480x480 RGB565 快照纹理缓冲区 (~460KB)
    snapshot = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));
    if (!snapshot) {
      Serial.println("[3D] Failed to allocate PSRAM snapshot buffer!");
      isReady = false;
      return false;
    }
    memset(snapshot, 0, 480 * 480 * sizeof(uint16_t));
    isReady = true;
    Serial.println("[3D] Perspective 3D Engine initialized with PSRAM buffer.");
    return true;
  }

  // 抓取当前屏幕画面存入离屏快照缓冲区
  void captureSnapshot() {
    if (!isReady || !gfx) return;
    uint16_t *fb = gfx->getFramebuffer();
    if (fb && snapshot) {
      memcpy(snapshot, fb, 480 * 480 * sizeof(uint16_t));
    }
  }

  // 渲染带有立体透视收缩与侧向阴影的倾斜画面
  // tiltDeg: 偏离当前正向的倾角（-45° ~ +45°）
  void renderPerspective(float tiltDeg) {
    if (!isReady || !gfx || !snapshot) return;

    uint16_t *fb = gfx->getFramebuffer();
    if (!fb) return;

    // 限制倾斜角范围
    float angle = constrain(tiltDeg, -42.0f, 42.0f);
    float rad = angle * (3.14159265f / 180.0f);

    float cosA = cosf(rad);
    float sinA = sinf(rad);

    // 计算透视投影缩放与边界
    // 宽度沿 X 轴压缩比例
    float widthRatio = fabsf(cosA);
    int newWidth = (int)(480.0f * widthRatio);
    if (newWidth < 120) newWidth = 120;
    int xOffset = (480 - newWidth) / 2;

    // 倾斜导致的两侧高度差系数（近大远小）
    float heightShear = sinA * 65.0f; // 最大两侧高差约 65 像素

    // 清空帧缓冲背景为纯黑
    memset(fb, 0, 480 * 480 * sizeof(uint16_t));

    // 计算光照衰减因子（模拟 3D 侧光阴影）
    float shadeFactor = 0.65f + 0.35f * widthRatio;

    // 逐列执行扫描线透视缩放与色彩映射
    for (int col = 0; col < newWidth; ++col) {
      int screenX = xOffset + col;
      if (screenX < 0 || screenX >= 480) continue;

      // 对应源纹理的 X 坐标
      int srcX = (int)((float)col / (float)newWidth * 479.0f);
      if (srcX < 0) srcX = 0;
      if (srcX >= 480) srcX = 479;

      // 当前列的透视高差与起始/结束 Y
      float colRatio = ((float)col / (float)newWidth) - 0.5f; // -0.5 ~ +0.5
      float colShear = colRatio * heightShear;

      float colHeight = 480.0f + colShear * 2.0f;
      int colYStart = (int)((480.0f - colHeight) * 0.5f);
      int colYEnd = colYStart + (int)colHeight;

      if (colYStart < 0) colYStart = 0;
      if (colYEnd > 480) colYEnd = 480;

      int curHeight = colYEnd - colYStart;
      if (curHeight <= 0) continue;

      uint16_t *srcColPtr = &snapshot[srcX];
      uint16_t *dstColPtr = &fb[screenX];

      for (int y = colYStart; y < colYEnd; ++y) {
        int srcY = (int)((float)(y - colYStart) / (float)curHeight * 479.0f);
        if (srcY < 0) srcY = 0;
        if (srcY >= 480) srcY = 479;

        uint16_t pixel = srcColPtr[srcY * 480];

        // 阴影快速定点数微调
        if (shadeFactor < 0.98f) {
          uint8_t r = ((pixel >> 11) & 0x1F);
          uint8_t g = ((pixel >> 5) & 0x3F);
          uint8_t b = (pixel & 0x1F);
          r = (uint8_t)(r * shadeFactor);
          g = (uint8_t)(g * shadeFactor);
          b = (uint8_t)(b * shadeFactor);
          pixel = (r << 11) | (g << 5) | b;
        }

        dstColPtr[y * 480] = pixel;
      }
    }

    // 刷新高速缓存
    gfx->flush(true);
  }

  void destroy() {
    if (snapshot) {
      free(snapshot);
      snapshot = nullptr;
    }
    isReady = false;
  }

private:
  Arduino_RGB_Display *gfx = nullptr;
  uint16_t *snapshot = nullptr;
};
