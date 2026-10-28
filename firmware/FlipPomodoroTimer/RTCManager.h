#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include "pin_config.h"
#include "SensorPCF85063.hpp"

class RTCManager {
public:
  bool isReady = false;
  bool ntpSynced = false;
  RTC_DateTime currentDateTime;
  char timeStr[16] = "--:--";

  bool init(TwoWire &wire = Wire) {
    if (!rtc.begin(wire, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      Serial.println("[RTC] PCF85063 begin failed!");
      isReady = false;
      return false;
    }
    if (!rtc.isRunning()) {
      rtc.start();
    }
    isReady = true;
    currentDateTime = rtc.getDateTime();
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
    Serial.printf("[RTC] PCF85063 Ready. Current Time: %04u-%02u-%02u %02u:%02u:%02u\n",
                  currentDateTime.getYear(), currentDateTime.getMonth(), currentDateTime.getDay(),
                  currentDateTime.getHour(), currentDateTime.getMinute(), currentDateTime.getSecond());
    return true;
  }

  bool syncTimeWithWiFi(const char* ssid, const char* password, unsigned long timeoutMs = 8000) {
    if (!ssid || strlen(ssid) == 0) return false;

    Serial.printf("[WIFI] Connecting to SSID: %s ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutMs)) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] Connected! IP: %s, RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());

      // 配置中国标准时间 GMT+8
      configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.asia.apple.com");

      struct tm timeinfo;
      bool gotNTP = false;
      unsigned long ntpStart = millis();
      while (millis() - ntpStart < 5000) {
        if (getLocalTime(&timeinfo, 500)) {
          gotNTP = true;
          break;
        }
        delay(200);
      }

      if (gotNTP) {
        Serial.printf("[NTP] Sync Success: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        if (isReady) {
          rtc.setDateTime(RTC_DateTime(timeinfo));
          currentDateTime = rtc.getDateTime();
          snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
        }
        ntpSynced = true;
      } else {
        Serial.println("[NTP] Sync timeout, using hardware RTC fallback.");
      }

      // 授时完成即刻断开 Wi-Fi 并关闭射频，节能省电
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      Serial.println("[WIFI] WiFi disconnected and RF powered off for power saving.");
      return gotNTP;
    } else {
      Serial.println("[WIFI] Connection timeout, continuing with offline hardware RTC.");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return false;
    }
  }

  void update(unsigned long now) {
    if (!isReady) return;
    if (now - lastUpdate >= 1000) {
      lastUpdate = now;
      currentDateTime = rtc.getDateTime();
      snprintf(timeStr, sizeof(timeStr), "%02u:%02u", currentDateTime.getHour(), currentDateTime.getMinute());
    }
  }

  const char* getTimeString() const {
    return timeStr;
  }

  uint8_t getHour() const { return currentDateTime.getHour(); }
  uint8_t getMinute() const { return currentDateTime.getMinute(); }
  uint8_t getSecond() const { return currentDateTime.getSecond(); }

private:
  SensorPCF85063 rtc;
  unsigned long lastUpdate = 0;
};
