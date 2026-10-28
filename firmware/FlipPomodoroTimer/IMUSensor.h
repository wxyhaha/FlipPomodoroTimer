#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "SensorQMI8658.hpp"

enum DeviceOrientation {
  ORIENTATION_UNKNOWN,
  ORIENTATION_FACE_UP,      // 屏幕朝上（平放看屏）
  ORIENTATION_FACE_DOWN,    // 屏幕朝下（翻转盖在桌面上）
  ORIENTATION_UPRIGHT,      // 站立斜放（支架或靠立）
  ORIENTATION_MOVING        // 移动/晃动中
};

class IMUSensor {
public:
  bool isReady = false;
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;

  DeviceOrientation currentOrientation = ORIENTATION_FACE_UP;
  DeviceOrientation candidateOrientation = ORIENTATION_FACE_UP;
  unsigned long candidateStartTime = 0;

  // 防抖稳定时间（毫秒）
  static constexpr unsigned long STABLE_DURATION_MS = 600;

  bool init(TwoWire &wire = Wire) {
    if (!qmi.begin(wire, QMI8658_L_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      isReady = false;
      return false;
    }
    // 配置加速度计与低通滤波
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
    isReady = true;

    // 读取初始姿态
    update();
    currentOrientation = candidateOrientation;
    return true;
  }

  // 返回 true 表示姿态刚刚确认发生了切换
  bool update() {
    if (!isReady) return false;

    if (!qmi.getAccelerometer(ax, ay, az)) {
      return false;
    }

    DeviceOrientation rawOri = classifyRawOrientation(ax, ay, az);
    unsigned long now = millis();

    if (rawOri != candidateOrientation) {
      candidateOrientation = rawOri;
      candidateStartTime = now;
    } else {
      // 检查是否持续稳定
      if (candidateOrientation != ORIENTATION_MOVING && candidateOrientation != currentOrientation) {
        if (now - candidateStartTime >= STABLE_DURATION_MS) {
          currentOrientation = candidateOrientation;
          return true; // 发生有效姿态转换
        }
      }
    }
    return false;
  }

  bool isFlippedDown() const {
    return currentOrientation == ORIENTATION_FACE_DOWN;
  }

  bool isDisplayVisible() const {
    return currentOrientation == ORIENTATION_FACE_UP || currentOrientation == ORIENTATION_UPRIGHT;
  }

  const char* getOrientationName() const {
    switch (currentOrientation) {
      case ORIENTATION_FACE_UP:   return "正面朝上";
      case ORIENTATION_FACE_DOWN: return "翻转盖下";
      case ORIENTATION_UPRIGHT:   return "立式放置";
      case ORIENTATION_MOVING:    return "拿取/移动中";
      default:                    return "未知";
    }
  }

private:
  SensorQMI8658 qmi;

  DeviceOrientation classifyRawOrientation(float x, float y, float z) {
    // 实际实测：平放屏幕朝上时 az 约为 -1.0g
    // 翻转盖在桌面上时 az 约为 +1.0g
    if (z > 0.45f) {
      return ORIENTATION_FACE_DOWN;
    } else if (z < -0.40f) {
      return ORIENTATION_FACE_UP;
    } else if (abs(y) > 0.45f || abs(x) > 0.45f) {
      return ORIENTATION_UPRIGHT;
    }
    return ORIENTATION_MOVING;
  }
};
