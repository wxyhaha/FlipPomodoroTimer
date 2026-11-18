#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include "pin_config.h"
#include "SensorPCF85063.hpp"
#include "I2CLock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class RTCManager {
public:
  static constexpr unsigned long RESYNC_INTERVAL_MS = 24UL * 3600UL * 1000UL;

  bool isReady = false;
  bool ntpSynced = false;
  bool timeValid = false;
  bool lastReadOk = false;
  volatile bool syncInProgress = false;
  RTC_DateTime currentDateTime;
  char timeStr[16] = "--:--";

  bool init(TwoWire &wire = Wire) {
    I2CLock lock;
    if (!rtc.begin(wire, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      Serial.println("[RTC] PCF85063 begin failed!");
      isReady = false;
      return false;
    }
    if (!rtc.isRunning()) {
      rtc.start();
    }
    isReady = true;
    timeValid = rtc.isClockIntegrityGuaranteed();
    if (!timeValid) {
      Serial.println("[RTC] Time invalid; awaiting synchronization");
      return true;
    }
    currentDateTime = rtc.getDateTime();
    lastReadOk = true;
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
    Serial.printf("[RTC] PCF85063 Ready. Current Time: %04u-%02u-%02u %02u:%02u:%02u\n",
                  currentDateTime.getYear(), currentDateTime.getMonth(), currentDateTime.getDay(),
                  currentDateTime.getHour(), currentDateTime.getMinute(), currentDateTime.getSecond());
    return true;
  }

  void startAsyncSync(const char *ssid, const char *password, int8_t tzHours = 8, unsigned long timeoutMs = 10000) {
    if (!ssid || strlen(ssid) == 0 || syncInProgress) return;

    SyncParams *params = new SyncParams();
    strncpy(params->ssid, ssid, sizeof(params->ssid) - 1);
    if (password) strncpy(params->pass, password, sizeof(params->pass) - 1);
    params->tzHours = tzHours;
    params->timeoutMs = timeoutMs;
    params->self = this;

    syncInProgress = true;
    if (xTaskCreatePinnedToCore(syncTaskEntry, "ntp-sync", 4096, params, 1, nullptr, 0) != pdPASS) {
      syncInProgress = false;
      delete params;
      Serial.println("[WIFI] Failed to launch async NTP task");
    }
  }

  void update(unsigned long now) {
    if (!isReady) return;
    if (now - lastUpdate >= 1000) {
      lastUpdate = now;
      bool valid = false;
      RTC_DateTime dt;
      {
        I2CLock lock;
        valid = rtc.isClockIntegrityGuaranteed();
        if (valid) {
          dt = rtc.getDateTime();
        }
      }
      // 简单合理性校验，避免 I2C 读错时显示异常年份或小时
      bool sane = valid && dt.getYear() >= 2020 && dt.getYear() <= 2099 &&
                  dt.getMonth() >= 1 && dt.getMonth() <= 12 &&
                  dt.getDay() >= 1 && dt.getDay() <= 31 &&
                  dt.getHour() < 24 && dt.getMinute() < 60 && dt.getSecond() < 60;
      lastReadOk = sane;
      if (!sane) {
        timeValid = false;
        strcpy(timeStr, "--:--");
        return;
      }
      timeValid = true;
      currentDateTime = dt;
      snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
    }
  }

  bool shouldPeriodicResync(unsigned long now) const {
    if (syncInProgress || lastSyncAttemptMs == 0) return false;
    return (now - lastSyncAttemptMs) >= RESYNC_INTERVAL_MS;
  }

  uint32_t getDateYyyyMmDd() const {
    if (!timeValid) return 0;
    return static_cast<uint32_t>(currentDateTime.getYear()) * 10000UL +
           static_cast<uint32_t>(currentDateTime.getMonth()) * 100UL +
           static_cast<uint32_t>(currentDateTime.getDay());
  }

  const char* getTimeString() const {
    return timeStr;
  }

  uint8_t getHour() const { return currentDateTime.getHour(); }
  uint8_t getMinute() const { return currentDateTime.getMinute(); }
  uint8_t getSecond() const { return currentDateTime.getSecond(); }

private:
  struct SyncParams {
    RTCManager *self = nullptr;
    char ssid[64] = "";
    char pass[64] = "";
    int8_t tzHours = 8;
    unsigned long timeoutMs = 10000;
  };

  SensorPCF85063 rtc;
  unsigned long lastUpdate = 0;
  unsigned long lastSyncAttemptMs = 0;

  static void syncTaskEntry(void *arg) {
    SyncParams *p = static_cast<SyncParams *>(arg);
    p->self->runSyncBlocking(p->ssid, p->pass, p->tzHours, p->timeoutMs);
    p->self->syncInProgress = false;
    delete p;
    vTaskDelete(nullptr);
  }

  bool runSyncBlocking(const char *ssid, const char *password, int8_t tzHours, unsigned long timeoutMs) {
    lastSyncAttemptMs = millis();
    Serial.printf("[WIFI] Async NTP connecting to SSID: %s ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutMs)) {
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    bool gotNTP = false;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] Connected! IP: %s, RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      configTime(static_cast<long>(tzHours) * 3600L, 0, "ntp.aliyun.com", "pool.ntp.org", "time.asia.apple.com");

      struct tm timeinfo;
      unsigned long ntpStart = millis();
      while (millis() - ntpStart < 5000) {
        if (getLocalTime(&timeinfo, 500)) {
          gotNTP = true;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
      }

      if (gotNTP) {
        Serial.printf("[NTP] Sync Success: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        if (isReady) {
          I2CLock lock;
          rtc.setDateTime(RTC_DateTime(timeinfo));
          timeValid = rtc.isClockIntegrityGuaranteed();
          if (timeValid) {
            currentDateTime = rtc.getDateTime();
            snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
          }
        }
        ntpSynced = true;
      } else {
        Serial.println("[NTP] Sync timeout, using hardware RTC fallback.");
      }
    } else {
      Serial.println("[WIFI] Connection timeout, continuing with offline hardware RTC.");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] WiFi disconnected and RF powered off.");
    return gotNTP;
  }
};
