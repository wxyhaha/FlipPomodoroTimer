#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "XPowersLib.h"
#include "I2CLock.h"

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

  uint8_t userBrightness = 255;
  uint8_t currentBrightness = 255;
  uint8_t targetBrightness = 255;
  bool lowPowerCpu = false;

  bool init(TwoWire &wire = Wire) {
    // 使用 LEDC PWM 驱动背光引脚 (GPIO 4)，20kHz 8-bit（消除低频背光闪烁与拍频）
    // 硬件物理特性：GPIO 4 经反相晶体管 Q8 接入升压芯片反馈回路，LOW 点亮、HIGH 熄灭。
    ledcAttach(BOARD_LCD_BACKLIGHT, 20000, 8);
    setBacklight(userBrightness);

    I2CLock lock;
    if (!pmu.begin(wire, AXP2101_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
      isReady = false;
      return false;
    }

    // Shutdown is initiated by two short PWRKEY clicks in firmware, not a long hold.
    pmu.disableLongPressShutdown();
    pmu.disableIRQ(XPOWERS_AXP2101_PKEY_LONG_IRQ);
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    pmu.clearIrqStatus();

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
    unsigned long now = millis();
    if (sampled && now - lastSampleMs < 250) return;
    sampled = true;
    lastSampleMs = now;

    I2CLock lock;
    vbusIn = pmu.isVbusIn();
    vbusVoltageMv = pmu.getVbusVoltage();
    batteryConnected = pmu.isBatteryConnect();
    battVoltageMv = pmu.getBattVoltage();
    charging = pmu.isCharging();

    if (batteryConnected) {
      batteryPercent = pmu.getBatteryPercent();
      // Treat read errors or invalid values as unknown, not empty/full.
      if (batteryPercent < 0 || batteryPercent > 100) batteryPercent = -1;
    } else {
      batteryPercent = -1;
    }
  }

  bool sampled = false;
  unsigned long lastSampleMs = 0;

  bool updatePowerKeyDoubleClick(unsigned long now = millis()) {
    if (!isReady) return false;

    // Poll the AXP2101's latched short-press IRQ often enough to separate two clicks.
    if (powerKeyPolled && now - lastPowerKeyPollMs < POWER_KEY_POLL_INTERVAL_MS) {
      if (waitingForSecondPowerKeyClick && now - firstPowerKeyClickMs > POWER_KEY_DOUBLE_CLICK_MS) {
        waitingForSecondPowerKeyClick = false;
      }
      return false;
    }
    powerKeyPolled = true;
    lastPowerKeyPollMs = now;

    uint64_t irqStatus = 0;
    bool shortPress = false;
    {
      I2CLock lock;
      irqStatus = pmu.getIrqStatus();
      shortPress = pmu.isPekeyShortPressIrq();
      if (irqStatus) pmu.clearIrqStatus();
    }

    if (waitingForSecondPowerKeyClick && now - firstPowerKeyClickMs > POWER_KEY_DOUBLE_CLICK_MS) {
      waitingForSecondPowerKeyClick = false;
    }
    if (!shortPress) return false;

    if (waitingForSecondPowerKeyClick) {
      waitingForSecondPowerKeyClick = false;
      return true;
    }

    waitingForSecondPowerKeyClick = true;
    firstPowerKeyClickMs = now;
    return false;
  }

  void shutdown() {
    if (!isReady) return;
    I2CLock lock;
    pmu.shutdown();
  }

  static constexpr unsigned long POWER_KEY_DOUBLE_CLICK_MS = 600;
  static constexpr unsigned long POWER_KEY_POLL_INTERVAL_MS = 20;
  bool powerKeyPolled = false;
  bool waitingForSecondPowerKeyClick = false;
  unsigned long lastPowerKeyPollMs = 0;
  unsigned long firstPowerKeyClickMs = 0;

  void setUserBrightness(uint8_t brightness) {
    userBrightness = constrain(brightness, 30, 255);
    if (currentBrightness > 0) {
      setBacklight(userBrightness);
    }
  }

  void setBacklight(uint8_t brightness) {
    targetBrightness = brightness;
    currentBrightness = brightness;
    // 硬件反相：brightness=255 -> duty=0 (全低电平满亮)；brightness=0 -> duty=255 (全高电平熄灭)
    uint32_t duty = 255 - brightness;
    ledcWrite(BOARD_LCD_BACKLIGHT, duty);
  }

  void dimForFlipDown() {
    setBacklight(0);
  }

  void dimForPause() {
    if (currentBrightness > 0) {
      uint8_t dimmed = max<uint8_t>(28, userBrightness / 3);
      setBacklight(dimmed);
    }
  }

  void restoreFromFlipUp() {
    setBacklight(userBrightness);
  }

  void setLowPowerCpu(bool enable) {
    if (lowPowerCpu == enable) return;
    lowPowerCpu = enable;
    setCpuFrequencyMhz(enable ? 80 : 240);
  }
};
