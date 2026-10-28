#include "FramePresenter.h"
#include <math.h>
#include <esp_heap_caps.h>

bool IRAM_ATTR FramePresenter::onVsync(esp_lcd_panel_handle_t,
    const esp_lcd_rgb_panel_event_data_t *, void *context) {
  ++static_cast<FramePresenter *>(context)->vsyncCount;
  return false;
}

bool IRAM_ATTR FramePresenter::onFrameComplete(esp_lcd_panel_handle_t,
    const esp_lcd_rgb_panel_event_data_t *, void *context) {
  ++static_cast<FramePresenter *>(context)->frameCompleteCount;
  return false;
}

bool FramePresenter::begin(Arduino_ESP32RGBPanel *panel) {
  handle = panel->panelHandle();
  void *a = nullptr, *b = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(handle, 2, &a, &b) != ESP_OK) return false;
  front = static_cast<uint16_t *>(a); back = static_cast<uint16_t *>(b);
  // SDK allocations start zeroed; make the write buffer explicitly match.
  memcpy(back, front, 480 * 480 * sizeof(uint16_t));
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = onVsync;
  callbacks.on_frame_buf_complete = onFrameComplete;
  if (esp_lcd_rgb_panel_register_event_callbacks(handle, &callbacks, this) != ESP_OK) return false;
  Serial.println("[UI] RGB double buffer enabled; 10-line bounce buffers; driver frame-release fence");
  return true;
}

bool FramePresenter::swap(UiRect dirty, bool synchronize) {
  // Bounce DMA reads internal RAM; the SDK copies cached PSRAM into it.
  // This presenter requires the configured bounce-buffer mode.

  if (esp_lcd_panel_draw_bitmap(handle, 0, 0, 480, 480, back) != ESP_OK) {
    healthy = false;
    Serial.println("[UI] RGB frame submission failed; retain last frame");
    return false;
  }
  // In bounce mode the SDK switches its source at frame completion and then
  // invokes this callback. It explicitly declares the old source reusable.
  uint32_t startCount = frameCompleteCount, startMs = millis(), waitStart = micros();
  while (static_cast<uint32_t>(frameCompleteCount - startCount) < 1) {
    if (millis() - startMs > 150) {
      ++timeouts; healthy = false;
      Serial.println("[UI] frame-release timeout; stop writes to protect front buffer");
      return false;
    }
    delay(1);
  }
  lastWaitUs = micros() - waitStart;
  uint16_t *old = front; front = back; back = old;
  // Synchronize only the changed physical region after old front is retired.
  if (synchronize) {
    for (int y = dirty.y; y < dirty.bottom; ++y) {
      memcpy(back + y * 480 + dirty.x, front + y * 480 + dirty.x,
             (dirty.right - dirty.x) * sizeof(uint16_t));
    }
  }
  buffersDiverged = !synchronize;
  ++swaps;
  return true;
}

bool FramePresenter::present(const uint16_t *canvas, uint8_t rotation, UiRect dirty) {
  if (!healthy || dirty.empty()) return false;
  if (buffersDiverged) dirty.full();
  UiRect physical;
  int r = rotation & 3;
  for (int y = dirty.y; y < dirty.bottom; ++y) {
    if (!r) {
      memcpy(back + y * 480 + dirty.x, canvas + y * 480 + dirty.x,
             (dirty.right - dirty.x) * sizeof(uint16_t));
    } else for (int x = dirty.x; x < dirty.right; ++x) {
      int px, py;
      if (r == 1) { px = 479 - y; py = x; }
      else if (r == 2) { px = 479 - x; py = 479 - y; }
      else { px = y; py = 479 - x; }
      back[py * 480 + px] = canvas[y * 480 + x];
    }
  }
  if (!r) physical = dirty;
  else if (r == 1) physical.include(480 - dirty.bottom, dirty.x,
      dirty.bottom - dirty.y, dirty.right - dirty.x);
  else if (r == 2) physical.include(480 - dirty.right, 480 - dirty.bottom,
      dirty.right - dirty.x, dirty.bottom - dirty.y);
  else physical.include(dirty.y, 480 - dirty.right,
      dirty.bottom - dirty.y, dirty.right - dirty.x);
  return swap(physical);
}

bool FramePresenter::prepareAnimation(const uint16_t *canvas) {
  if (animationTexture) return true;
  bool internal = false;
  const int sizes[] = {240, 160, 120};
  for (int size : sizes) {
    size_t bytes = size * size * sizeof(uint16_t);
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < bytes + 48 * 1024) continue;
    animationTexture = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (animationTexture) { textureSize = size; internal = true; break; }
  }
  if (!animationTexture) {
    textureSize = 240;
    animationTexture = static_cast<uint16_t *>(heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!animationTexture) return false;
  int scale = 480 / textureSize, samples = scale * scale;
  for (int y = 0; y < textureSize; ++y) for (int x = 0; x < textureSize; ++x) {
    int red = 0, green = 0, blue = 0;
    for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
      uint16_t c = canvas[(y * scale + dy) * 480 + x * scale + dx];
      red += (c >> 11) & 31; green += (c >> 5) & 63; blue += c & 31;
    }
    animationTexture[y * textureSize + x] = ((red / samples) << 11) |
        ((green / samples) << 5) | (blue / samples);
  }
  Serial.printf("[UI] animation cache=%ux%u %s\n", textureSize, textureSize, internal ? "DRAM" : "PSRAM");
  return true;
}

void FramePresenter::endAnimation() {
  free(animationTexture); animationTexture = nullptr; textureSize = 0;
  Serial.printf("[UI] last animation render=%lu us, release wait=%lu us\n",
      static_cast<unsigned long>(lastRenderUs), static_cast<unsigned long>(lastWaitUs));
}

bool FramePresenter::presentRotated(const uint16_t *canvas, float degrees) {
  if (!healthy || (!animationTexture && !prepareAnimation(canvas))) return false;
  uint32_t started = micros();
  float rad = degrees * 0.0174532925199f;
  int32_t c = lroundf(cosf(rad) * 65536), s = lroundf(sinf(rad) * 65536);
  int size = textureSize, scale = 480 / size, center = size - 1;
  for (int y = 0; y < size; ++y) {
    int32_t dy = 2 * y - center;
    int32_t sx = ((-center * c + dy * s) / 2) + center * 32768 + 32768;
    int32_t sy = ((center * s + dy * c) / 2) + center * 32768 + 32768;
    uint16_t *row = back + y * scale * 480;
    for (int x = 0; x < size; ++x) {
      int ix = sx >> 16, iy = sy >> 16;
      uint16_t color = (ix >= 0 && ix < size && iy >= 0 && iy < size)
          ? animationTexture[iy * size + ix] : 0;
      if (scale == 2) {
        uint32_t pair = static_cast<uint32_t>(color) | (static_cast<uint32_t>(color) << 16);
        reinterpret_cast<uint32_t *>(row)[x] = pair;
        reinterpret_cast<uint32_t *>(row + 480)[x] = pair;
      } else {
        for (int yy = 0; yy < scale; ++yy) for (int xx = 0; xx < scale; ++xx)
          row[yy * 480 + x * scale + xx] = color;
      }
      sx += c; sy -= s;
    }
  }
  lastRenderUs = micros() - started;
  UiRect full; full.full();
  // Every intermediate frame overwrites the back buffer. No mirror copy needed.
  return swap(full, false);
}

void FramePresenter::report() {
  if (millis() - lastReport < 10000) return;
  lastReport = millis();
  Serial.printf("[UI] sync swaps=%lu timeouts=%lu healthy=%u\n",
      static_cast<unsigned long>(swaps), static_cast<unsigned long>(timeouts), healthy);
}
