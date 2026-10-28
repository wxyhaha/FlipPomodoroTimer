#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "TouchDrvGT911.hpp"

enum UserAction {
  ACTION_NONE,
  ACTION_TAP,         // 短按或触摸轻触
  ACTION_LONG_PRESS   // 长按重置
};

class InputManager {
public:
  bool touchReady = false;
  uint8_t currentRotation = 0;

  void setRotation(uint8_t r) {
    currentRotation = r & 3;
  }

  bool init(TwoWire &wire = Wire) {
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);

    if (touch.begin(wire, GT911_SLAVE_ADDRESS_L, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      touch.setMaxTouchPoint(1);
      touchReady = true;
    }
    return true;
  }

  UserAction update() {
    unsigned long now = millis();
    UserAction result = ACTION_NONE;

    // 1. 处理硬件 BOOT 按键 (GPIO 0, 低电平有效)
    bool buttonPressed = (digitalRead(BOARD_BOOT_PIN) == LOW);
    if (buttonPressed) {
      if (!btnDown) {
        btnDown = true;
        btnDownTime = now;
        btnLongPressedTriggered = false;
      } else {
        if (!btnLongPressedTriggered && (now - btnDownTime >= 1500)) {
          btnLongPressedTriggered = true;
          result = ACTION_LONG_PRESS;
        }
      }
    } else {
      if (btnDown) {
        btnDown = false;
        if (!btnLongPressedTriggered && (now - btnDownTime >= 50)) {
          // 短按触发
          result = ACTION_TAP;
        }
      }
    }

    // 如果按键已触发，直接返回
    if (result != ACTION_NONE) return result;

    // 2. 处理触摸屏触控事件（全屏轻触交互）
    if (touchReady) {
      int16_t x[1], y[1];
      uint8_t touched = touch.getPoint(x, y, 1);
      if (touched > 0) {
        if (!isTouched) {
          isTouched = true;
          touchStartTime = now;
        }
      } else {
        if (isTouched) {
          isTouched = false;
          // 触摸释放，判断是否为有效点击（50ms ~ 1200ms）
          if (now - touchStartTime >= 50 && now - touchStartTime <= 1200) {
            result = ACTION_TAP;
          }
        }
      }
    }

    return result;
  }

private:
  TouchDrvGT911 touch;

  // 按键防抖与长按状态
  bool btnDown = false;
  unsigned long btnDownTime = 0;
  bool btnLongPressedTriggered = false;

  // 触摸状态
  bool isTouched = false;
  unsigned long touchStartTime = 0;
};
