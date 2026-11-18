#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "pin_config.h"
#include "SensorQMI8658.hpp"
#include "I2CLock.h"

enum DeviceOrientation {
  ORIENTATION_UNKNOWN,
  ORIENTATION_BACKWARD_PAUSE, // 向后翻转平放（屏幕朝上仰卧桌面，向后翻转暂停）
  ORIENTATION_FACE_DOWN,      // 翻转盖在桌面上（屏幕朝下盖下，完全息屏节能暂停）
  ORIENTATION_FACE_1,         // 面一：上边着台（默认5分钟）
  ORIENTATION_FACE_2,         // 面二：左边着台（默认10分钟）
  ORIENTATION_FACE_3,         // 面三：下边着台（默认60分钟）
  ORIENTATION_FACE_4,         // 面四：右边着台（默认30分钟）
  ORIENTATION_MOVING          // 拿取/移动防抖中
};

class IMUSensor {
public:
  bool isReady = false;
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;

  DeviceOrientation currentOrientation = ORIENTATION_FACE_3;
  DeviceOrientation candidateOrientation = ORIENTATION_FACE_3;
  unsigned long candidateStartTime = 0;

  // 连续滚转角（度，-180 ~ +180）与平滑滤波
  float rollAngleDeg = 0.0f;
  float smoothRollAngle = 0.0f;

  // 防抖稳定时间（毫秒）
  static constexpr unsigned long STABLE_DURATION_MS = 600;
  // Display responds sooner than the timer; hysteresis keeps diagonals stable.
  static constexpr unsigned long VISUAL_STABLE_MS = 100;

  bool init(TwoWire &wire = Wire) {
    {
      I2CLock lock;
      if (!qmi.begin(wire, QMI8658_L_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        isReady = false;
        return false;
      }
      // 配置加速度计与陀螺仪
      qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
      qmi.enableAccelerometer();

      qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_112_1Hz, SensorQMI8658::LPF_MODE_0);
      qmi.enableGyroscope();
    }

    isReady = true;

    // 读取初始姿态
    update();
    currentOrientation = candidateOrientation;
    smoothRollAngle = rollAngleDeg;
    visualRotation = getDisplayRotation(0);
    visualCandidate = visualRotation;
    return true;
  }

  // 返回 true 表示姿态刚刚确认发生了切换
  bool update() {
    if (!isReady) return false;

    bool accOk = false;
    {
      I2CLock lock;
      accOk = qmi.getAccelerometer(ax, ay, az);
      if (accOk) {
        qmi.getGyroscope(gx, gy, gz);
      }
    }
    if (!accOk) return false;

    // 计算当前在屏幕平面的重力倾角
    if (fabsf(ax) > 0.1f || fabsf(ay) > 0.1f) {
      rollAngleDeg = atan2f(ax, ay) * (180.0f / 3.14159265f);
      // 平滑滤波
      smoothRollAngle += (rollAngleDeg - smoothRollAngle) * 0.25f;
    }

    DeviceOrientation rawOri = classifyRawOrientation(ax, ay, az);
    unsigned long now = millis();
    updateVisualRotation(now);

    if (rawOri != candidateOrientation) {
      candidateOrientation = rawOri;
      candidateStartTime = now;
    } else {
      // 检查是否持续稳定超过防抖阈值
      if (candidateOrientation != ORIENTATION_MOVING && candidateOrientation != currentOrientation) {
        if (now - candidateStartTime >= STABLE_DURATION_MS) {
          currentOrientation = candidateOrientation;
          return true; // 发生有效姿态转换
        }
      }
    }
    return false;
  }

  // 当翻面处于 100ms ~ 600ms 稳定缓冲期时，返回即将切换的目标侧面编号 (1..4)，否则返回 0
  uint8_t getPendingFace() const {
    if (!isReady) return 0;
    if (candidateOrientation >= ORIENTATION_FACE_1 && candidateOrientation <= ORIENTATION_FACE_4 &&
        candidateOrientation != currentOrientation) {
      unsigned long elapsed = millis() - candidateStartTime;
      if (elapsed >= VISUAL_STABLE_MS && elapsed < STABLE_DURATION_MS) {
        return static_cast<uint8_t>(candidateOrientation - ORIENTATION_FACE_1 + 1);
      }
    }
    return 0;
  }

  bool isBackwardPaused() const {
    return currentOrientation == ORIENTATION_BACKWARD_PAUSE;
  }

  bool isFlippedDown() const {
    return currentOrientation == ORIENTATION_FACE_DOWN;
  }

  bool isPauseOrientation() const {
    return currentOrientation == ORIENTATION_BACKWARD_PAUSE || currentOrientation == ORIENTATION_FACE_DOWN;
  }

  bool isDisplayVisible() const {
    return currentOrientation != ORIENTATION_FACE_DOWN;
  }

  bool isTiltedFace() const {
    return currentOrientation >= ORIENTATION_FACE_1 && currentOrientation <= ORIENTATION_FACE_4;
  }

  uint8_t getFaceIndex() const {
    if (currentOrientation >= ORIENTATION_FACE_1 && currentOrientation <= ORIENTATION_FACE_4) {
      return (uint8_t)(currentOrientation - ORIENTATION_FACE_1 + 1);
    }
    return 0;
  }

  // 默认四面时长配置：面一 5分 / 面二 10分 / 面三 60分 / 面四 30分
  unsigned long getFaceDurationSec() const {
    switch (currentOrientation) {
      case ORIENTATION_FACE_1: return 5 * 60;
      case ORIENTATION_FACE_2: return 10 * 60;
      case ORIENTATION_FACE_3: return 60 * 60;
      case ORIENTATION_FACE_4: return 30 * 60;
      default:                 return 60 * 60;
    }
  }

  // 根据当前重力加速度计算自适应屏幕旋转方向 (0: 0°, 1: 90° CW, 2: 180°, 3: 270° CW)
  uint8_t getDisplayRotation(uint8_t fallbackRotation = 0) const {
    switch (currentOrientation) {
      case ORIENTATION_FACE_3: return 0;
      case ORIENTATION_FACE_2: return 1;
      case ORIENTATION_FACE_1: return 2;
      case ORIENTATION_FACE_4: return 3;
      default:                 return fallbackRotation;
    }
  }

  uint8_t getVisualRotation(uint8_t fallback = 0) const {
    return isReady ? visualRotation : fallback;
  }

  const char* getOrientationName() const {
    switch (currentOrientation) {
      case ORIENTATION_BACKWARD_PAUSE: return "向后翻平(暂停)";
      case ORIENTATION_FACE_DOWN:      return "翻转盖下(息屏)";
      case ORIENTATION_FACE_1:         return "面一(5分)";
      case ORIENTATION_FACE_2:         return "面二(10分)";
      case ORIENTATION_FACE_3:         return "面三(60分)";
      case ORIENTATION_FACE_4:         return "面四(30分)";
      case ORIENTATION_MOVING:         return "移动防抖中";
      default:                         return "未知姿态";
    }
  }

private:
  SensorQMI8658 qmi;
  uint8_t visualRotation = 0;
  uint8_t visualCandidate = 0;
  unsigned long visualCandidateSince = 0;

  void updateVisualRotation(unsigned long now) {
    uint8_t target = visualRotation;
    float x = fabsf(ax), y = fabsf(ay);
    if (fabsf(az) < 0.85f) {
      if (y > 0.35f && y > x * 1.25f) target = ay > 0 ? 0 : 2;
      else if (x > 0.35f && x > y * 1.25f) target = ax < 0 ? 1 : 3;
    }
    if (target != visualCandidate) {
      visualCandidate = target;
      visualCandidateSince = now;
    } else if (now - visualCandidateSince >= VISUAL_STABLE_MS) {
      visualRotation = target;
    }
  }

  DeviceOrientation classifyRawOrientation(float x, float y, float z) {
    if (z > 0.50f) {
      return ORIENTATION_FACE_DOWN;
    }
    if (z < -0.50f && fabsf(x) < 0.38f && fabsf(y) < 0.38f) {
      return ORIENTATION_BACKWARD_PAUSE;
    }
    if (fabsf(y) >= fabsf(x) && fabsf(y) > 0.35f) {
      return (y > 0.0f) ? ORIENTATION_FACE_3 : ORIENTATION_FACE_1;
    } else if (fabsf(x) > fabsf(y) && fabsf(x) > 0.35f) {
      return (x < 0.0f) ? ORIENTATION_FACE_2 : ORIENTATION_FACE_4;
    }
    return ORIENTATION_MOVING;
  }
};
