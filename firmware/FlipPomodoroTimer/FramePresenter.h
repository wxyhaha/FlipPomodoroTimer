#pragma once
#include <Arduino.h>
#include "databus/Arduino_ESP32RGBPanel.h"

struct UiRect {
  int x = 480, y = 480, right = 0, bottom = 0;
  bool empty() const { return right <= x || bottom <= y; }
  void include(int left, int top, int width, int height) {
    x = min(x, max(0, left)); y = min(y, max(0, top));
    right = max(right, min(480, left + width));
    bottom = max(bottom, min(480, top + height));
  }
  void full() { x = y = 0; right = bottom = 480; }
};

// Driver-owned front/back buffers. Never draw into the scanned-out buffer.
class FramePresenter {
public:
  bool begin(Arduino_ESP32RGBPanel *panel);
  bool present(const uint16_t *canvas, uint8_t rotation, UiRect dirty);
  bool presentRotated(const uint16_t *canvas, float angleDegrees);
  bool prepareAnimation(const uint16_t *canvas);
  void endAnimation();
  void report();
private:
  esp_lcd_panel_handle_t handle = nullptr;
  uint16_t *front = nullptr, *back = nullptr;
  volatile uint32_t vsyncCount = 0;
  volatile uint32_t frameCompleteCount = 0;
  bool healthy = true;
  bool buffersDiverged = false;
  uint16_t *animationTexture = nullptr;
  int textureSize = 0;
  uint32_t lastRenderUs = 0, lastWaitUs = 0;
  uint32_t swaps = 0, timeouts = 0, lastReport = 0;
  static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t,
      const esp_lcd_rgb_panel_event_data_t *, void *context);
  static bool IRAM_ATTR onFrameComplete(esp_lcd_panel_handle_t,
      const esp_lcd_rgb_panel_event_data_t *, void *context);
  bool swap(UiRect physicalDirty, bool synchronize = true);
};
