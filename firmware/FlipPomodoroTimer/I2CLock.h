#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Global recursive mutex protecting the shared I2C bus (Wire on GPIO 47/48)
// across Core 0 (audio / async NTP tasks) and Core 1 (main loop).
inline SemaphoreHandle_t i2cMutex() {
  static StaticSemaphore_t storage;
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutexStatic(&storage);
  return mutex;
}

class I2CLock {
public:
  I2CLock() { xSemaphoreTakeRecursive(i2cMutex(), portMAX_DELAY); }
  ~I2CLock() { xSemaphoreGiveRecursive(i2cMutex()); }
  I2CLock(const I2CLock &) = delete;
  I2CLock &operator=(const I2CLock &) = delete;
};
