#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "XPowersLib.h"

class PowerManager {
public:
  bool isReady = false;
  XPowersPMU pmu;

  uint16_t vbusVoltageMv = 0;
  uint16_t battVoltageMv = 0;
  bool vbusIn = false;
  bool batteryConnected = false;
  bool charging = false;
  int batteryPercent = -1;

  uint8_t currentBrightness = 255;
  uint8_t targetBrightness = 255;

  bool init(TwoWire &wire = Wire) {
    // 初始化背光控制引脚（GPIO 4）
    pinMode(BOARD_LCD_BACKLIGHT, OUTPUT);
    setBacklight(255);

    if (!pmu.begin(wire, AXP2101_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      isReady = false;
      return false;
    }

    // 启用内部 ADC 测量通道
    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.enableTemperatureMeasure();

    // 配置安全充电参数：目标截止电压 4.2V，恒流充电 500mA
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);

    isReady = true;
    update();
    return true;
  }

  void update() {
    if (!isReady) return;

    vbusIn = pmu.isVbusIn();
    vbusVoltageMv = pmu.getVbusVoltage();
    batteryConnected = pmu.isBatteryConnect();
    battVoltageMv = pmu.getBattVoltage();
    charging = pmu.isCharging();

    if (batteryConnected) {
      batteryPercent = pmu.getBatteryPercent();
      // 限幅保证合理百分比
      if (batteryPercent > 100) batteryPercent = 100;
      if (batteryPercent < 0) batteryPercent = 0;
    } else {
      batteryPercent = -1;
    }
  }

  void setBacklight(uint8_t brightness) {
    targetBrightness = brightness;
    currentBrightness = brightness;
    // 硬件物理特性：GPIO 4 经反相晶体管 Q8 接入升压芯片反馈回路。
    // 输出 LOW 为 100% 满血最大亮度；输出 HIGH 为关断背光/最低微漏电流。
    if (brightness > 0) {
      digitalWrite(BOARD_LCD_BACKLIGHT, LOW);
    } else {
      digitalWrite(BOARD_LCD_BACKLIGHT, HIGH);
    }
  }

  void dimForFlipDown() {
    // 翻转朝下时关闭背光，极大降低功耗并消除桌面杂散漏光
    setBacklight(0);
  }

  void restoreFromFlipUp() {
    // 翻回正面时立刻恢复背光
    setBacklight(255);
  }
};
