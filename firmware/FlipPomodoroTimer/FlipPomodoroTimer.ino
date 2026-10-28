#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "PomodoroState.h"
#include "IMUSensor.h"
#include "PowerManager.h"
#include "DisplayUI.h"
#include "InputManager.h"
#include "AudioManager.h"

PomodoroEngine engine;
IMUSensor imu;
PowerManager power;
DisplayUI ui;
InputManager input;
AudioManager audio;

unsigned long lastTelemetryTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==================================================");
  Serial.println("  Flip Pomodoro Timer (ESP32-S3-Touch-LCD-4B)");
  Serial.println("==================================================");

  // 1. 初始化 I2C 总线
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  // 2. 初始化显示屏 (原生 U8g2 汉字支持，零闪烁渲染)
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

  // 4. 初始化六轴姿态传感器 (QMI8658 四面独立倾角解算)
  Serial.print("Init QMI8658 IMU... ");
  if (imu.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 5. 初始化触控与按键 (GT911 & BOOT Pin)
  Serial.print("Init Input (Touch & Button)... ");
  if (input.init()) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED!");
  }

  // 6. 初始化音频提示 (ES8311 Codec 最大音量提示音)
  Serial.print("Init Audio (ES8311 Codec)... ");
  if (audio.init(ui.getExpander())) {
    Serial.println("OK");
  } else {
    Serial.println("DISABLED/NOT FOUND");
  }

  // 7. 初始化番茄钟状态机
  engine.begin();

  delay(600);

  // 首次完整绘制全中文界面
  ui.fullRedraw(engine, imu, power);
  Serial.println("System Ready! Tilt to select face or tap screen.\n");
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

  // 3. 轮询姿态传感器（带 600ms 滤波防抖）
  bool oriChanged = imu.update();
  if (oriChanged) {
    Serial.printf("[IMU] Orientation Changed -> %s (ax=%.2f, ay=%.2f, az=%.2f)\n",
                  imu.getOrientationName(), imu.ax, imu.ay, imu.az);

    if (imu.isFlippedDown()) {
      // 动作：设备翻转盖在桌面上
      if (engine.state == STATE_FOCUS_RUNNING || engine.state == STATE_BREAK_RUNNING) {
        Serial.println("[POMO] Auto Pause by Flip Down!");
        engine.pause(PAUSE_BY_FLIP);
        power.dimForFlipDown(); // 关断背光完全息屏省电
      } else if (engine.state == STATE_COMPLETED) {
        Serial.println("[POMO] Confirm Completed by Flip Down");
        engine.confirmCompleted();
        power.dimForFlipDown();
      }
    } else {
      // 动作：设备屏幕可见（桌面平放或侧立面）
      power.restoreFromFlipUp(); // 100% 满血点亮屏幕

      if (imu.isTiltedFace()) {
        uint8_t face = imu.getFaceIndex();
        unsigned long faceSec = imu.getFaceDurationSec();

        // 如果立到了新的面，或者之前在待机/完成状态，切换到对应面独立倒计时
        if (engine.activeFace != face || engine.state == STATE_IDLE || engine.state == STATE_COMPLETED) {
          Serial.printf("[POMO] Tilt to New Face %u (%lu sec) -> Start Timer!\n", face, faceSec);
          audio.playStartTone();
          engine.startFace(face, faceSec);
        } else if ((engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) &&
                   engine.pauseReason == PAUSE_BY_FLIP) {
          // 如果是同一个面从翻转盖下中恢复
          Serial.printf("[POMO] Tilt back to Same Face %u -> Resume Timer!\n", face);
          audio.playStartTone();
          engine.resume();
        }
      } else if (imu.currentOrientation == ORIENTATION_FACE_UP) {
        // 桌面平放：如果是盖下暂停后平放回来，自动恢复继续倒计时
        if ((engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) &&
            engine.pauseReason == PAUSE_BY_FLIP) {
          Serial.println("[POMO] Auto Resume by Flip Face Up!");
          audio.playStartTone();
          engine.resume();
        }
      }
    }
  }

  // 4. 轮询触摸与物理按键输入
  UserAction action = input.update();
  if (action == ACTION_TAP) {
    Serial.println("[USER] Tap Event (Screen Touch / BOOT Button)");
    if (engine.state == STATE_IDLE || engine.state == STATE_FOCUS_PAUSED || engine.state == STATE_BREAK_PAUSED) {
      audio.playStartTone();
    } else {
      audio.playClickTone();
    }
    engine.toggle();
  } else if (action == ACTION_LONG_PRESS) {
    Serial.println("[USER] Long Press Event -> RESET TIMER");
    audio.playClickTone();
    engine.reset();
  }

  // 5. 驱动界面局部高画质无闪烁刷新
  ui.update(engine, imu, power);

  // 6. 周期性串口遥测输出（每 1 秒打印一次状态）
  if (now - lastTelemetryTime >= 1000) {
    lastTelemetryTime = now;
    unsigned long rem = engine.getRemainingSeconds();
    Serial.printf("[STATUS] Time: %02lu:%02lu | Face: %u | State: %d | Ori: %s | VBUS: %umV (in:%d) | Batt: %umV (%d%%)\n",
                  rem / 60, rem % 60, engine.activeFace, engine.state, imu.getOrientationName(),
                  power.vbusVoltageMv, power.vbusIn, power.battVoltageMv, power.batteryPercent);
  }

  delay(10);
}
