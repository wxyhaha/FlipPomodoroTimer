#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "pin_config.h"
#include "SensorQMI8658.hpp"

enum DeviceOrientation {
  ORIENTATION_UNKNOWN,
  ORIENTATION_BACKWARD_PAUSE, // 向后翻转平放（屏幕朝上仰卧桌面，向后翻转暂停）
  ORIENTATION_FACE_DOWN,      // 翻转盖在桌面上（屏幕朝下盖下，完全息屏节能暂停）
  ORIENTATION_FACE_1,         // 面一：上边着台（5分钟短休息/闪电番茄）
  ORIENTATION_FACE_2,         // 面二：左边着台（10分钟快速任务）
  ORIENTATION_FACE_3,         // 面三：下边着台（30分钟深度番茄）
  ORIENTATION_FACE_4,         // 面四：右边着台（60分钟长程专注）
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
    if (!qmi.begin(wire, QMI8658_L_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      isReady = false;
      return false;
    }
    // 配置加速度计与陀螺仪
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();

    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_112_1Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableGyroscope();

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

    if (!qmi.getAccelerometer(ax, ay, az)) {
      return false;
    }
    qmi.getGyroscope(gx, gy, gz);

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

  // 视频经典四面时长配置：5 / 10 / 30 / 60 分钟
  unsigned long getFaceDurationSec() const {
    switch (currentOrientation) {
      case ORIENTATION_FACE_1: return 5 * 60;   // 5 分钟 (顶边着台)
      case ORIENTATION_FACE_2: return 10 * 60;  // 10 分钟 (左边着台)
      case ORIENTATION_FACE_3: return 30 * 60;  // 30 分钟 (底边着台，标准深度专注)
      case ORIENTATION_FACE_4: return 60 * 60;  // 60 分钟 (右边着台，长程学习)
      default:                 return 30 * 60;
    }
  }

  // 根据当前重力加速度计算自适应屏幕旋转方向 (0: 0°, 1: 90° CW, 2: 180°, 3: 270° CW)
  uint8_t getDisplayRotation(uint8_t fallbackRotation = 0) const {
    switch (currentOrientation) {
      case ORIENTATION_FACE_3: return 0; // 下边着台 (正常正向，30分)
      case ORIENTATION_FACE_2: return 1; // 左边着台 (顺时针90°，10分)
      case ORIENTATION_FACE_1: return 2; // 上边着台 (180°倒置，5分)
      case ORIENTATION_FACE_4: return 3; // 右边着台 (顺时针270°，60分)
      default:                 return fallbackRotation; // 向后翻平或移动中保持当前屏幕方向
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
      case ORIENTATION_FACE_3:         return "面三(30分)";
      case ORIENTATION_FACE_4:         return "面四(60分)";
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
    // 实际实测物理重力分量特征：
    // 1. 翻转盖在桌面上（屏幕朝下）：az 显著为正值 (约为 +1.0g)
    if (z > 0.50f) {
      return ORIENTATION_FACE_DOWN;
    }

    // 2. 向后翻转平放（屏幕朝上仰卧桌面，向后翻转暂停）：az 显著为负值 (约为 -0.7g ~ -1.0g)，且 x,y 水平分量较小
    if (z < -0.50f && fabsf(x) < 0.38f && fabsf(y) < 0.38f) {
      return ORIENTATION_BACKWARD_PAUSE;
    }

    // 3. 四个侧立面/倾角面：z 轴分量衰减，重力主要落在 x 或 y 轴上
    if (fabsf(y) >= fabsf(x) && fabsf(y) > 0.35f) {
      // Y 轴主导：y > 0 为下边着台（面三 30分），y < 0 为上边着台（面一 5分）
      return (y > 0.0f) ? ORIENTATION_FACE_3 : ORIENTATION_FACE_1;
    } else if (fabsf(x) > fabsf(y) && fabsf(x) > 0.35f) {
      // X 轴主导：x < 0 为左边着台（面二 10分），x > 0 为右边着台（面四 60分）
      return (x < 0.0f) ? ORIENTATION_FACE_2 : ORIENTATION_FACE_4;
    }

    return ORIENTATION_MOVING;
  }
};
