#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"
#include "DisplayUI.h"
#include "InputManager.h"
#include "AudioManager.h"
#include "RTCManager.h"
#include "ConfigManager.h"
#include "StatsManager.h"
#include "WebPortalManager.h"
#include "WiFiConfig.h"

PomodoroEngine engine;
IMUSensor imu;
PowerManager power;
DisplayUI ui;
InputManager input;
AudioManager audio;
RTCManager rtc;
ConfigManager config;
StatsManager stats;
WebPortalManager portal;

unsigned long lastTelemetryTime = 0;
bool stableVbusIn = false;
bool candidateVbusIn = false;
bool startIdleTimerAfterUnplug = false;
unsigned long vbusCandidateSince = 0;
constexpr unsigned long USB_MODE_DEBOUNCE_MS = 1000;

void applyRuntimeConfig() {
  power.setUserBrightness(config.data.brightness);
  audio.setVolume(config.data.audioVolume);
  engine.breakDurationSec = config.getBreakDurationSec();
  engine.autoBreakEnabled = config.data.autoBreak;
  if (engine.state == STATE_IDLE && !engine.lastSessionWasBreak) {
    engine.focusDurationSec = config.getFaceDurationSec(engine.activeFace);
    engine.currentTotalDurationSec = engine.focusDurationSec;
  }
  ui.forceRedraw();
}

void updateUsbMode(unsigned long now) {
  if (power.vbusIn != candidateVbusIn) {
    candidateVbusIn = power.vbusIn;
    vbusCandidateSince = now;
  }

  if (candidateVbusIn == stableVbusIn || now - vbusCandidateSince < USB_MODE_DEBOUNCE_MS) return;

  stableVbusIn = candidateVbusIn;
  if (stableVbusIn) {
    startIdleTimerAfterUnplug = false;
    if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
      Serial.println("[POWER] USB connected during countdown; keep timer and page");
    } else if (ui.getMode() != UI_MODE_STATS) {
      ui.setMode(UI_MODE_CLOCK);
      Serial.println("[POWER] Stable USB input -> Clock page");
    }
  } else {
    if (engine.pauseReason == PAUSE_BY_USB) engine.pauseReason = PAUSE_BY_USER;
    if (ui.getMode() != UI_MODE_STATS) {
      ui.setMode(UI_MODE_POMODORO);
    }
    startIdleTimerAfterUnplug = engine.state == STATE_IDLE;
    Serial.println("[POWER] USB input removed -> Pomodoro page");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n==================================================");
  Serial.println("  Cube Flip Clock & Timer (ESP32-S3-Touch-LCD-4B)");
  Serial.println("==================================================");

  // 0. 加载 NVS 配置与专注统计
  config.begin(WIFI_SSID, WIFI_PASS);
  stats.begin();

  // 1. 初始化 I2C 总线
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  // 2. 初始化显示屏 (原生全中文支持，480x480 RGB)
  Serial.print("Init Display... ");
  if (ui.init()) {
    Serial.println("OK");
    ui.showBootScreen();
  } else {
    Serial.println("FAILED!");
  }

  // 3. 初始化电源管理 (AXP2101 + PWM 背光)
  Serial.print("Init Power Manager... ");
  power.setUserBrightness(config.data.brightness);
  if (power.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 4. 初始化硬件 RTC 并启动后台异步 Wi-Fi NTP 校时（开机零阻塞）
  Serial.print("Init RTC & Async WiFi NTP... ");
  if (rtc.init()) {
    Serial.println("OK");
    rtc.startAsyncSync(config.data.wifiSsid, config.data.wifiPass, config.data.timezoneOffsetHours, 10000);
  } else {
    Serial.println("FAILED!");
  }

  // 5. 初始化六轴姿态传感器 (QMI8658)
  Serial.print("Init QMI8658 IMU... ");
  if (imu.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 6. 初始化触控与按键 (GT911 & BOOT Pin)
  Serial.print("Init Input (Touch & Button)... ");
  if (input.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 7. 初始化音频提示 (ES8311 Codec)
  Serial.print("Init Audio (ES8311 Codec)... ");
  if (audio.init(ui.getExpander(), config.data.audioVolume)) {
    Serial.println("OK");
  } else {
    Serial.println("DISABLED/NOT FOUND");
  }

  // 8. 初始化番茄钟状态机与 Web 控制台
  engine.begin(config.getFaceDurationSec(3), config.getBreakDurationSec(), config.data.autoBreak);
  portal.begin(config, stats, rtc);

  stableVbusIn = candidateVbusIn = power.vbusIn;
  vbusCandidateSince = millis();
  ui.setMode(stableVbusIn ? UI_MODE_CLOCK : UI_MODE_POMODORO);

  uint8_t initialRotation = imu.getDisplayRotation(0);
  ui.setRotation(initialRotation);
  input.setRotation(initialRotation);

  if (imu.isFlippedDown()) ui.setPanelOutputOff(true, power);
  delay(200);

  ui.fullRedraw(engine, imu, power, rtc, stats, config, portal);
  Serial.println("System Ready! UI V7.0 - async NTP, NVS stats/config, Web portal, low-power CPU.\n");
}

void loop() {
  unsigned long now = millis();

  // 1. Web 控制台服务与配置热更新
  portal.handleClient();
  if (portal.consumeConfigChanged()) {
    applyRuntimeConfig();
    Serial.println("[CONFIG] Runtime config updated from Web portal");
  }
  if ((portal.consumeSyncRequested() || rtc.shouldPeriodicResync(now)) && !portal.isActive()) {
    rtc.startAsyncSync(config.data.wifiSsid, config.data.wifiPass, config.data.timezoneOffsetHours, 10000);
  }

  TimerState stateAtLoopStart = engine.state;
  uint8_t faceAtLoopStart = engine.activeFace;
  engine.tick(now);

  // 专注完成统计入库（仅专注周期计入，防止重复记录）
  if (engine.state == STATE_COMPLETED && !engine.lastSessionWasBreak && !engine.completionRecorded) {
    uint16_t mins = static_cast<uint16_t>(max<unsigned long>(1, engine.focusDurationSec / 60));
    stats.recordCompletion(rtc.getDateYyyyMmDd(), mins);
    engine.completionRecorded = true;
    Serial.printf("[STATS] Recorded %u min focus (Total=%lu)\n", mins, static_cast<unsigned long>(stats.totalPomodoros));
  }

  // Optional serial speaker check
  static char command[24] = {};
  static uint8_t commandLength = 0;
  for (int budget = 0; budget < 32 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      command[commandLength] = 0;
      if (!strcmp(command, "AUDIO_TEST")) audio.playTest();
      else if (!strcmp(command, "CHIME_TEST")) {
        ui.triggerHourlyChimeAnim();
        audio.playHourlyChime();
      } else if (!strcmp(command, "DEMO_DAY")) {
        ui.setMode(UI_MODE_CLOCK);
        ui.toggleClockDemo();
      }
      commandLength = 0;
    } else if (commandLength < sizeof(command) - 1) command[commandLength++] = c;
    else commandLength = 0;
  }

  // 2. 轮询电源状态（USB 插拔、电量、PWRKEY 双击关机）
  power.update();
  if (power.updatePowerKeyDoubleClick(now)) {
    Serial.println("[POWER] PWRKEY double-click -> full shutdown");
    portal.stop();
    audio.stop();
    power.shutdown();
  }
  bool previousStableVbus = stableVbusIn;
  updateUsbMode(now);

  // 3. 推进硬件 RTC 时间走时 & 时钟页整点报时仪式（22:00-07:00 夜间免打扰仅视觉动效）
  rtc.update(now);
  static uint64_t lastChimeHourKey = 0;
  static bool chimeBootSettled = false;
  if (rtc.timeValid) {
    uint64_t hourKey = static_cast<uint64_t>(rtc.getDateYyyyMmDd()) * 100ULL + rtc.getHour();
    if (!chimeBootSettled) {
      chimeBootSettled = true;
      if (rtc.getMinute() == 0 && rtc.getSecond() <= 3) lastChimeHourKey = hourKey;
    } else if (rtc.getMinute() == 0 && rtc.getSecond() <= 2 && hourKey != lastChimeHourKey) {
      lastChimeHourKey = hourKey;
      if (config.data.hourlyChime && ui.getMode() == UI_MODE_CLOCK &&
          !ui.isManualScreenOff() && !ui.isPanelOutputOff()) {
        uint8_t h = rtc.getHour();
        bool quietNight = (h >= 22 || h < 7);
        ui.triggerHourlyChimeAnim();
        if (!quietNight) {
          audio.playHourlyChime();
          Serial.printf("[CLOCK] Hourly chime triggered (%02u:00)\n", h);
        } else {
          Serial.printf("[CLOCK] Hourly visual-only chime (quiet night %02u:00)\n", h);
        }
      }
    }
  }

  // 4. 轮询姿态传感器
  bool oriChanged = imu.update();

  uint8_t targetRot = imu.getVisualRotation(ui.getRotation());
  if (targetRot != ui.getRotation()) {
    power.setLowPowerCpu(false);
    ui.setRotation(targetRot);
    input.setRotation(targetRot);
    Serial.printf("[DISPLAY] 2D direction -> %u deg\n", targetRot * 90);
  }

  // 5. 姿态稳态切换事件
  if (oriChanged) {
    Serial.printf("[IMU] Orientation Changed -> %s (ax=%.2f, ay=%.2f, az=%.2f, roll=%.1f deg)\n",
                  imu.getOrientationName(), imu.ax, imu.ay, imu.az, imu.smoothRollAngle);

    if (imu.isFlippedDown()) {
      ui.setPanelOutputOff(true, power);
      if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
        Serial.println("[POMO] Auto Pause by Flip Down (Panel Off)");
        engine.pause(PAUSE_BY_FLIP);
      } else if (engine.state == STATE_COMPLETED) {
        Serial.println("[POMO] Confirm Completed by Flip Down");
        engine.confirmCompleted();
      }
    } else if (imu.isBackwardPaused()) {
      if (!ui.isManualScreenOff()) {
        power.setLowPowerCpu(false);
        ui.setPanelOutputOff(false, power);
      }
      if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
        Serial.println("[POMO] Auto Pause by Backward Flip!");
        engine.pause(PAUSE_BY_FLIP);
      } else if (engine.state == STATE_COMPLETED) {
        Serial.println("[POMO] Confirm Completed by Backward Flip");
        engine.confirmCompleted();
      }
    } else if (imu.isTiltedFace()) {
      if (!ui.isManualScreenOff()) {
        power.setLowPowerCpu(false);
        ui.setPanelOutputOff(false, power);
      }
      uint8_t face = imu.getFaceIndex();
      unsigned long faceSec = config.getFaceDurationSec(face);

      if (ui.getMode() == UI_MODE_CLOCK || ui.getMode() == UI_MODE_STATS || engine.pauseReason == PAUSE_BY_USB) {
        // 时钟页和统计页下翻面只调整显示方向
      } else if (engine.activeFace != face || engine.state == STATE_IDLE || engine.state == STATE_COMPLETED) {
        Serial.printf("[POMO] Tilt to New Face %u (%lu sec) -> Start Timer!\n", face, faceSec);
        engine.startFace(face, faceSec);
      } else if ((engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) &&
                 engine.pauseReason == PAUSE_BY_FLIP) {
        Serial.printf("[POMO] Tilt back to Same Face %u -> Resume Timer!\n", face);
        engine.resume();
      }
    }
  }

  if (startIdleTimerAfterUnplug) {
    if (stableVbusIn || engine.state != STATE_IDLE || ui.getMode() != UI_MODE_POMODORO) {
      startIdleTimerAfterUnplug = false;
    } else if (imu.isReady && imu.isTiltedFace() &&
               imu.candidateOrientation == imu.currentOrientation &&
               millis() - imu.candidateStartTime >= IMUSensor::STABLE_DURATION_MS) {
      uint8_t face = imu.getFaceIndex();
      engine.startFace(face, config.getFaceDurationSec(face));
      startIdleTimerAfterUnplug = false;
      Serial.println("[POMO] USB removed -> start current stable face");
    }
  }

  // 6. 轮询触摸与物理按键输入
  if (ui.isAnimating()) input.cancelTouch();
  UserAction action = input.update();
  if (action == ACTION_TAP && ui.isAnimating() && !input.lastTapFromButton) {
    action = ACTION_NONE;
  }
  if (action == ACTION_TAP) {
    Serial.printf("[USER] Tap Event at (%d, %d)\n", input.lastTapX, input.lastTapY);

    if (ui.isManualScreenOff()) {
      power.setLowPowerCpu(false);
      ui.setManualScreenOff(false);
      if (!imu.isFlippedDown()) ui.setPanelOutputOff(false, power);
      Serial.println("[UI] Wake display by touch");
    } else if (ui.checkScreenOffClick(input.lastTapX, input.lastTapY) && !input.lastTapFromButton) {
      ui.setManualScreenOff(true);
      ui.setPanelOutputOff(true, power);
      Serial.println("[UI] Manual panel off; tap anywhere to wake");
    } else if (ui.checkClockStyleButtonClick(input.lastTapX, input.lastTapY) && !input.lastTapFromButton) {
      config.data.clockTheme = (config.data.clockTheme + 1) % 2;
      config.save();
      ui.forceRedraw();
      audio.playClickTone();
      Serial.printf("[UI] Clock theme switched to %u\n", config.data.clockTheme);
    } else if (ui.checkClockPetClick(input.lastTapX, input.lastTapY, config.data.clockTheme) && !input.lastTapFromButton) {
      ui.triggerClockPetHappy();
      audio.playPetTone();
      Serial.println("[UI] Clock pixel cat petted!");
    } else if (ui.checkClockDemoClick(input.lastTapX, input.lastTapY) && !input.lastTapFromButton) {
      ui.toggleClockDemo();
      audio.playClickTone();
      Serial.printf("[UI] Clock 24h demo -> %s\n", ui.isClockDemoActive() ? "ON" : "OFF");
    } else if (ui.checkPortalButtonClick(input.lastTapX, input.lastTapY) && !input.lastTapFromButton) {
      if (portal.isActive()) portal.stop();
      else portal.start();
      audio.playClickTone();
    } else if (ui.checkThemeButtonClick(input.lastTapX, input.lastTapY) && !input.lastTapFromButton) {
      config.data.theme = (config.data.theme + 1) % 5;
      config.save();
      ui.forceRedraw();
      audio.playClickTone();
    } else {
      int clickedMode = ui.checkBadgeClick(input.lastTapX, input.lastTapY);
      if (clickedMode >= 0 && !input.lastTapFromButton) {
        if (clickedMode != (int)ui.getMode()) {
          ui.setMode((UIMode)clickedMode);
          Serial.printf("[UI] Mode Switched to %d\n", clickedMode);
          audio.playClickTone();
        }
      } else if (input.lastTapFromButton || ui.getMode() == UI_MODE_POMODORO) {
        engine.toggle();
        if (engine.state != STATE_FOCUS_RUNNING && engine.state != STATE_BREAK_RUNNING) audio.playClickTone();
      }
    }
  } else if (action == ACTION_LONG_PRESS) {
    Serial.println("[USER] Long Press Event -> RESET TIMER");
    audio.stop();
    audio.playClickTone();
    engine.reset();
  }

  // 7. 音频提示与完成态重复提醒（最多重响 3 次，间隔 15 秒）
  static unsigned long lastAlertChimeMs = 0;
  static uint8_t alertRepeatCount = 0;

  bool wasRunning = stateAtLoopStart == STATE_FOCUS_RUNNING || stateAtLoopStart == STATE_BREAK_RUNNING;
  bool running = engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING;
  if (stateAtLoopStart == STATE_COMPLETED && engine.state != STATE_COMPLETED) {
    audio.stopCompletion();
  }
  if (running && (!wasRunning || engine.activeFace != faceAtLoopStart)) {
    audio.playStartTone();
  }
  if (engine.state == STATE_COMPLETED) {
    if (stateAtLoopStart != STATE_COMPLETED) {
      Serial.println("[ALERT] Countdown finished; queue soft piano cue");
      audio.playCompletionChime();
      lastAlertChimeMs = now;
      alertRepeatCount = 0;
    } else if (action == ACTION_NONE && !oriChanged && !ui.isManualScreenOff() && !ui.isPanelOutputOff() &&
               alertRepeatCount < 3 && (now - lastAlertChimeMs >= 15000UL)) {
      audio.playCompletionChime();
      lastAlertChimeMs = now;
      alertRepeatCount++;
    }
  }

  // 8. 暂停时背光微暗节能，恢复时还原亮度
  if (!ui.isManualScreenOff() && !ui.isPanelOutputOff()) {
    bool isPausedNow = (engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED);
    bool wasPausedBefore = (stateAtLoopStart == STATE_FOCUS_PAUSED || stateAtLoopStart == STATE_BREAK_PAUSED);
    if (isPausedNow && !wasPausedBefore) {
      power.dimForPause();
    } else if (!isPausedNow && wasPausedBefore) {
      power.restoreFromFlipUp();
    }
  }

  // 9. 完成后无操作自动息屏
  static unsigned long completedActivityMs = 0;
  if (engine.state == STATE_COMPLETED) {
    unsigned long autoOffMs = static_cast<unsigned long>(config.data.autoScreenOffSec) * 1000UL;
    if (stateAtLoopStart != STATE_COMPLETED || action != ACTION_NONE || oriChanged ||
        previousStableVbus != stableVbusIn) {
      completedActivityMs = millis();
    } else if (autoOffMs > 0 && !ui.isManualScreenOff() && millis() - completedActivityMs >= autoOffMs) {
      ui.setManualScreenOff(true);
      ui.setPanelOutputOff(true, power);
      Serial.println("[UI] Completion inactivity -> panel off");
    }
  }

  // 10. 熄屏时动态降频省电，亮屏或联网时保持 240MHz
  bool shouldLowPower = (ui.isManualScreenOff() || ui.isPanelOutputOff()) &&
                        !portal.isActive() && !rtc.syncInProgress && !ui.isAnimating();
  power.setLowPowerCpu(shouldLowPower);

  // 11. 刷新界面
  ui.update(engine, imu, power, rtc, stats, config, portal);

  // 12. 周期性串口遥测输出
  if (now - lastTelemetryTime >= 1000) {
    lastTelemetryTime = now;
    unsigned long rem = engine.getRemainingSeconds();
    Serial.printf("[STATUS] Mode:%d | Time:%02lu:%02lu | RTC:%s | Face:%u(Rot:%u) | State:%d | Ori:%s | VBUS:%umV | Batt:%umV(%d%%)\n",
                  (int)ui.getMode(), rem / 60, rem % 60, rtc.getTimeString(), engine.activeFace, ui.getRotation(),
                  engine.state, imu.getOrientationName(),
                  power.vbusVoltageMv, power.battVoltageMv, power.batteryPercent);
  }

  delay(ui.isAnimating() ? 1 : (shouldLowPower ? 25 : 10));
}
