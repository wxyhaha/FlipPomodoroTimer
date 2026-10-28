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
#include "Perspective3D.h"

// Wi-Fi 自动校时凭证
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";

PomodoroEngine engine;
IMUSensor imu;
PowerManager power;
DisplayUI ui;
InputManager input;
AudioManager audio;
RTCManager rtc;
Perspective3D p3d;

unsigned long lastTelemetryTime = 0;
bool isTilting3D = false;
unsigned long lastTiltRenderTime = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n==================================================");
  Serial.println("  Cube Flip Clock & Timer (ESP32-S3-Touch-LCD-4B)");
  Serial.println("==================================================");

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

  // 3. 初始化电源管理 (AXP2101 + 100% 满血高亮背光)
  Serial.print("Init Power Manager... ");
  if (power.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 4. 初始化硬件 RTC 并通过 Wi-Fi NTP 自动授时
  Serial.print("Init RTC & WiFi NTP Sync... ");
  if (rtc.init()) {
    Serial.println("OK");
    // 尝试连接用户 Wi-Fi 并同步网络时间，授时完成后立即关闭 Wi-Fi 节能
    rtc.syncTimeWithWiFi(WIFI_SSID, WIFI_PASS, 8000);
  } else {
    Serial.println("FAILED!");
  }

  // 5. 初始化六轴姿态传感器 (QMI8658 陀螺仪与加速度计)
  Serial.print("Init QMI8658 IMU... ");
  if (imu.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 6. 初始化 2.5D/3D 透视渲染引擎
  Serial.print("Init Perspective 3D Engine... ");
  if (p3d.init(ui.getDisplay())) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 7. 初始化触控与按键 (GT911 & BOOT Pin)
  Serial.print("Init Input (Touch & Button)... ");
  if (input.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 8. 初始化音频提示 (ES8311 Codec 最大音量提示音)
  Serial.print("Init Audio (ES8311 Codec)... ");
  if (audio.init(ui.getExpander())) {
    Serial.println("OK");
  } else {
    Serial.println("DISABLED/NOT FOUND");
  }

  // 9. 初始化番茄钟状态机
  engine.begin();

  // 根据启动时的初始姿态自动校准屏幕显示方向
  uint8_t initialRotation = imu.getDisplayRotation(0);
  ui.setRotation(initialRotation);
  input.setRotation(initialRotation);

  delay(400);

  // 首次完整绘制拟真科技感全界面
  ui.fullRedraw(engine, imu, power, rtc);
  p3d.captureSnapshot();
  Serial.println("System Ready! Tilt cube or tap badges.\n");
}

void loop() {
  unsigned long now = millis();

  // 1. 推进单调时钟计时器与完成提示音
  TimerState prevState = engine.state;
  engine.tick(now);
  if (prevState != STATE_COMPLETED && engine.state == STATE_COMPLETED) {
    Serial.println("[ALERT] Countdown Finished! Ringing loud chime.");
    audio.playCompletionChime();
  }

  // 2. 轮询电源状态（USB 插拔、电量）
  power.update();

  // 3. 推进硬件 RTC 时间走时
  rtc.update(now);

  // 4. 轮询姿态传感器（125Hz 采样与 600ms 滤波）
  bool oriChanged = imu.update();

  // 5. [方案 B 物理跟随] 实时 3D 动力学透视跟随渲染
  if (p3d.isReady && !imu.isPauseOrientation() && ui.isReady) {
    float tiltOffset = imu.getTiltOffsetDeg(ui.getRotation());

    if (fabsf(tiltOffset) > 4.5f) {
      // 处于手持倾斜滚转中
      if (!isTilting3D) {
        p3d.captureSnapshot();
        isTilting3D = true;
      }
      // 限制 3D 物理渲染帧率约 35 FPS (每 28ms 渲染一帧)
      if (now - lastTiltRenderTime >= 28) {
        lastTiltRenderTime = now;
        p3d.renderPerspective(tiltOffset);
      }
    } else {
      // 倾角回正平稳
      if (isTilting3D) {
        isTilting3D = false;
        ui.fullRedraw(engine, imu, power, rtc);
        p3d.captureSnapshot();
      }
    }
  }

  // 6. 姿态发生稳态切换事件
  if (oriChanged) {
    Serial.printf("[IMU] Orientation Changed -> %s (ax=%.2f, ay=%.2f, az=%.2f, roll=%.1f deg)\n",
                  imu.getOrientationName(), imu.ax, imu.ay, imu.az, imu.smoothRollAngle);

    // 检查并更新自适应重力方向屏幕旋转 (0: 0°, 1: 90°, 2: 180°, 3: 270°)
    uint8_t targetRot = imu.getDisplayRotation(ui.getRotation());
    if (targetRot != ui.getRotation()) {
      Serial.printf("[DISPLAY] Auto-Rotate: %u -> %u (%u deg)\n",
                    ui.getRotation(), targetRot, targetRot * 90);
      ui.setRotation(targetRot);
      input.setRotation(targetRot);
      ui.fullRedraw(engine, imu, power, rtc);
      p3d.captureSnapshot();
    }

    if (imu.isFlippedDown()) {
      // 动作：设备屏幕完全朝下盖在桌面上（完全息屏节能暂停）
      if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
        Serial.println("[POMO] Auto Pause by Flip Down (Screen Dim)");
        engine.pause(PAUSE_BY_FLIP);
        power.dimForFlipDown();
      } else if (engine.state == STATE_COMPLETED) {
        Serial.println("[POMO] Confirm Completed by Flip Down");
        engine.confirmCompleted();
        power.dimForFlipDown();
      }
    } else if (imu.isBackwardPaused()) {
      // 动作：向后翻转平放在桌面上（用户核心需求：向后翻转暂停，保持 100% 满血高亮以便看清状态）
      power.restoreFromFlipUp();
      if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
        Serial.println("[POMO] Auto Pause by Backward Flip!");
        engine.pause(PAUSE_BY_FLIP);
      } else if (engine.state == STATE_COMPLETED) {
        Serial.println("[POMO] Confirm Completed by Backward Flip");
        engine.confirmCompleted();
      }
    } else if (imu.isTiltedFace()) {
      // 动作：设备立在 4 个侧立面之一
      power.restoreFromFlipUp();
      uint8_t face = imu.getFaceIndex();
      unsigned long faceSec = imu.getFaceDurationSec();

      // 如果立到了新的面，或者之前在待机/完成状态，切换到对应面独立倒计时
      if (engine.activeFace != face || engine.state == STATE_IDLE || engine.state == STATE_COMPLETED) {
        Serial.printf("[POMO] Tilt to New Face %u (%lu sec) -> Start Timer!\n", face, faceSec);
        audio.playStartTone();
        // 如果当前在时钟模式，翻面自动切回番茄倒计时模式
        if (ui.getMode() != UI_MODE_POMODORO) {
          ui.setMode(UI_MODE_POMODORO);
        }
        engine.startFace(face, faceSec);
        ui.fullRedraw(engine, imu, power, rtc);
        p3d.captureSnapshot();
      } else if ((engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) &&
                 engine.pauseReason == PAUSE_BY_FLIP) {
        // 同一个面从暂停中重新立起恢复
        Serial.printf("[POMO] Tilt back to Same Face %u -> Resume Timer!\n", face);
        audio.playStartTone();
        engine.resume();
      }
    }
  }

  // 7. 轮询触摸与物理按键输入
  UserAction action = input.update();
  if (action == ACTION_TAP) {
    Serial.printf("[USER] Tap Event at (%d, %d)\n", input.lastTapX, input.lastTapY);

    // 检查是否点击了顶部模式徽章
    int clickedMode = ui.checkBadgeClick(input.lastTapX, input.lastTapY);
    if (clickedMode >= 0) {
      if (clickedMode != (int)ui.getMode()) {
        ui.setMode((UIMode)clickedMode);
        Serial.printf("[UI] Mode Switched to %s\n", (clickedMode == UI_MODE_CLOCK) ? "CLOCK" : "POMODORO");
        audio.playClickTone();
        ui.fullRedraw(engine, imu, power, rtc);
        p3d.captureSnapshot();
      }
    } else {
      // 全屏普通点击：番茄钟开始/暂停
      if (engine.state == STATE_IDLE || engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) {
        audio.playStartTone();
      } else {
        audio.playClickTone();
      }
      engine.toggle();
      p3d.captureSnapshot();
    }
  } else if (action == ACTION_LONG_PRESS) {
    Serial.println("[USER] Long Press Event -> RESET TIMER");
    audio.playClickTone();
    engine.reset();
    ui.fullRedraw(engine, imu, power, rtc);
    p3d.captureSnapshot();
  }

  // 8. 驱动非 3D 倾斜状态下的常规零闪烁局部刷新
  if (!isTilting3D) {
    ui.update(engine, imu, power, rtc);
  }

  // 9. 周期性串口遥测输出（每 1 秒打印一次状态）
  if (now - lastTelemetryTime >= 1000) {
    lastTelemetryTime = now;
    unsigned long rem = engine.getRemainingSeconds();
    Serial.printf("[STATUS] Mode: %s | Time: %02lu:%02lu | RTC: %s | Face: %u (Rot:%u) | State: %d | Ori: %s | VBUS: %umV | Batt: %umV (%d%%)\n",
                  (ui.getMode() == UI_MODE_CLOCK) ? "CLOCK" : "POMO",
                  rem / 60, rem % 60, rtc.getTimeString(), engine.activeFace, ui.getRotation(),
                  engine.state, imu.getOrientationName(),
                  power.vbusVoltageMv, power.battVoltageMv, power.batteryPercent);
  }

  delay(10);
}
