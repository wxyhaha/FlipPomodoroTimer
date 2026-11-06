#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

inline SemaphoreHandle_t expanderMutex() {
  static StaticSemaphore_t storage;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
  return mutex;
}
class ExpanderLock {
public:
  ExpanderLock() { xSemaphoreTake(expanderMutex(), portMAX_DELAY); }
  ~ExpanderLock() { xSemaphoreGive(expanderMutex()); }
};
