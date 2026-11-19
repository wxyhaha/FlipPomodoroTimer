#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

struct AppConfig {
  char wifiSsid[64] = "";
  char wifiPass[64] = "";
  uint16_t faceMinutes[4] = {5, 10, 60, 30}; // 面1..面4 默认分钟数
  uint16_t breakMinutes = 5;                 // 默认休息时长（分钟）
  bool autoBreak = true;                     // 专注完成后是否自动建议/进入休息
  uint8_t audioVolume = 75;                  // ES8311 音量 (0=静音, 1..85, 75=0dB)
  uint8_t brightness = 255;                  // 屏幕亮度 (30..255)
  uint16_t autoScreenOffSec = 30;            // 完成后无操作自动息屏秒数 (0=关闭)
  int8_t timezoneOffsetHours = 8;            // 时区偏移（默认东八区 GMT+8）
  uint8_t theme = 0;                         // 番茄钟表盘主题 (0..4)
  uint8_t clockTheme = 0;                    // 时钟页主题 (0=极简天象, 1=像素小猫)
  bool hourlyChime = true;                   // 整点报时开关（时钟页生效，22:00-07:00自动静默）
  bool scheduleMode = false;                 // 日程模式开关（false=自由翻转番茄钟，true=每日日程模式）
};

class ConfigManager {
public:
  AppConfig data;

  void begin(const char *fallbackSsid = "", const char *fallbackPass = "") {
    Preferences prefs;
    if (!prefs.begin("flip_pomo", true)) {
      applyFallbackWifi(fallbackSsid, fallbackPass);
      return;
    }

    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    if (ssid.length() > 0) {
      strncpy(data.wifiSsid, ssid.c_str(), sizeof(data.wifiSsid) - 1);
      strncpy(data.wifiPass, pass.c_str(), sizeof(data.wifiPass) - 1);
    } else {
      applyFallbackWifi(fallbackSsid, fallbackPass);
    }

    data.faceMinutes[0] = constrain(prefs.getUShort("f1", 5), 1, 180);
    data.faceMinutes[1] = constrain(prefs.getUShort("f2", 10), 1, 180);
    data.faceMinutes[2] = constrain(prefs.getUShort("f3", 60), 1, 180);
    data.faceMinutes[3] = constrain(prefs.getUShort("f4", 30), 1, 180);
    data.breakMinutes = constrain(prefs.getUShort("brk", 5), 1, 60);
    data.autoBreak = prefs.getBool("abrk", true);
    data.audioVolume = constrain(prefs.getUChar("vol", 75), 0, 85);
    data.brightness = constrain(prefs.getUChar("bri", 255), 30, 255);
    data.autoScreenOffSec = prefs.getUShort("aoff", 30);
    if (data.autoScreenOffSec > 600) data.autoScreenOffSec = 600;
    data.timezoneOffsetHours = constrain(prefs.getChar("tz", 8), -12, 14);
    data.theme = constrain(prefs.getUChar("thm", 0), 0, 4);
    data.clockTheme = constrain(prefs.getUChar("cthm", 0), 0, 1);
    data.hourlyChime = prefs.getBool("hchm", true);
    data.scheduleMode = prefs.getBool("schm", false);
    prefs.end();
  }

  bool save() {
    Preferences prefs;
    if (!prefs.begin("flip_pomo", false)) return false;
    prefs.putString("ssid", data.wifiSsid);
    prefs.putString("pass", data.wifiPass);
    prefs.putUShort("f1", constrain(data.faceMinutes[0], 1, 180));
    prefs.putUShort("f2", constrain(data.faceMinutes[1], 1, 180));
    prefs.putUShort("f3", constrain(data.faceMinutes[2], 1, 180));
    prefs.putUShort("f4", constrain(data.faceMinutes[3], 1, 180));
    prefs.putUShort("brk", constrain(data.breakMinutes, 1, 60));
    prefs.putBool("abrk", data.autoBreak);
    prefs.putUChar("vol", constrain(data.audioVolume, 0, 85));
    prefs.putUChar("bri", constrain(data.brightness, 30, 255));
    prefs.putUShort("aoff", data.autoScreenOffSec > 600 ? 600 : data.autoScreenOffSec);
    prefs.putChar("tz", constrain(data.timezoneOffsetHours, -12, 14));
    prefs.putUChar("thm", constrain(data.theme, 0, 4));
    prefs.putUChar("cthm", constrain(data.clockTheme, 0, 1));
    prefs.putBool("hchm", data.hourlyChime);
    prefs.putBool("schm", data.scheduleMode);
    prefs.end();
    return true;
  }

  unsigned long getFaceDurationSec(uint8_t faceIndex) const {
    if (faceIndex >= 1 && faceIndex <= 4) {
      return static_cast<unsigned long>(data.faceMinutes[faceIndex - 1]) * 60UL;
    }
    return static_cast<unsigned long>(data.faceMinutes[2]) * 60UL;
  }

  unsigned long getBreakDurationSec() const {
    return static_cast<unsigned long>(data.breakMinutes) * 60UL;
  }

private:
  void applyFallbackWifi(const char *ssid, const char *pass) {
    if (ssid && strlen(ssid) > 0) {
      strncpy(data.wifiSsid, ssid, sizeof(data.wifiSsid) - 1);
      data.wifiSsid[sizeof(data.wifiSsid) - 1] = '\0';
      if (pass) {
        strncpy(data.wifiPass, pass, sizeof(data.wifiPass) - 1);
        data.wifiPass[sizeof(data.wifiPass) - 1] = '\0';
      }
    }
  }
};
