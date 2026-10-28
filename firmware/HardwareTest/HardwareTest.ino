#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "SensorQMI8658.hpp"
#include "XPowersLib.h"

// Hardware instances
Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
  BOARD_EXPANDER_LCD_RST,
  BOARD_EXPANDER_LCD_CS,
  BOARD_EXPANDER_LCD_SCL,
  BOARD_EXPANDER_LCD_SDA,
  &Wire,
  BOARD_EXPANDER_ADDRESS);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  BOARD_LCD_DE, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_PCLK,
  BOARD_LCD_R0, BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4,
  BOARD_LCD_G0, BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5,
  BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4,
  1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, rgbpanel, 0 /* rotation */, true /* auto_flush */,
  expander, GFX_NOT_DEFINED, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

SensorQMI8658 qmi;
XPowersPMU power;

bool imu_ok = false;
bool pmu_ok = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==========================================");
  Serial.println("  ESP32-S3-Touch-LCD-4B Hardware Test");
  Serial.println("==========================================");

  // Backlight pin
  pinMode(BOARD_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(BOARD_LCD_BACKLIGHT, HIGH);

  // Wire I2C
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  // Reset Touch and LCD via Expander
  expander->pinMode(BOARD_EXPANDER_TOUCH_RST, OUTPUT);
  expander->pinMode(BOARD_EXPANDER_TOUCH_INT, OUTPUT);
  expander->digitalWrite(BOARD_EXPANDER_TOUCH_INT, LOW);
  delay(100);
  expander->digitalWrite(BOARD_EXPANDER_TOUCH_RST, LOW);
  delay(100);
  expander->digitalWrite(BOARD_EXPANDER_TOUCH_RST, HIGH);
  delay(100);

  // Initialize LCD
  Serial.print("Init Display... ");
  if (gfx->begin()) {
    Serial.println("OK!");
    gfx->fillScreen(RGB565_NAVY);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(30, 40);
    gfx->println("Flip Pomodoro");
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_GREEN);
    gfx->setCursor(30, 80);
    gfx->println("Hardware Diagnostic v1.0");
  } else {
    Serial.println("FAILED!");
  }

  // Initialize QMI8658
  Serial.print("Init QMI8658 IMU... ");
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
    imu_ok = true;
    Serial.println("OK!");
  } else {
    Serial.println("FAILED!");
  }

  // Initialize AXP2101
  Serial.print("Init AXP2101 PMIC... ");
  if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();
    power.enableTemperatureMeasure();
    pmu_ok = true;
    Serial.println("OK!");
  } else {
    Serial.println("FAILED!");
  }

  delay(1000);
}

unsigned long last_print = 0;

void loop() {
  unsigned long now = millis();
  if (now - last_print >= 500) {
    last_print = now;

    float ax = 0, ay = 0, az = 0;
    if (imu_ok) {
      qmi.getAccelerometer(ax, ay, az);
    }

    uint16_t vbus_mv = pmu_ok ? power.getVbusVoltage() : 0;
    uint16_t batt_mv = pmu_ok ? power.getBattVoltage() : 0;
    bool is_charging = pmu_ok ? power.isCharging() : false;
    bool is_vbus_in = pmu_ok ? power.isVbusIn() : false;
    bool batt_connected = pmu_ok ? power.isBatteryConnect() : false;
    int batt_pct = (pmu_ok && batt_connected) ? power.getBatteryPercent() : -1;

    // Orientation detection:
    // When screen faces up: az is usually near +1.0g (or -1.0g depending on mount)
    // When flipped face down: az sign is inverted
    const char *orientation_str = "NORMAL";
    if (az < -0.65) {
      orientation_str = "FACE DOWN (FLIPPED)";
    } else if (az > 0.65) {
      orientation_str = "FACE UP";
    } else if (ay > 0.65) {
      orientation_str = "STAND UPRIGHT";
    } else if (ay < -0.65) {
      orientation_str = "STAND INVERTED";
    }

    // Print to Serial
    Serial.printf("[DIAG] IMU: ax=%.2f ay=%.2f az=%.2f | Ori: %s | VBUS=%umV (in:%d) | BATT=%umV (pct:%d%%, chg:%d)\n",
                  ax, ay, az, orientation_str, vbus_mv, is_vbus_in, batt_mv, batt_pct, is_charging);

    // Update screen section
    gfx->fillRect(30, 120, 420, 320, RGB565_NAVY);
    gfx->setCursor(30, 130);
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_YELLOW);
    gfx->printf("Orientation:\n  %s\n\n", orientation_str);

    gfx->setTextColor(RGB565_WHITE);
    gfx->printf("Accel (g):\n");
    gfx->printf("  X: %+.2f\n  Y: %+.2f\n  Z: %+.2f\n\n", ax, ay, az);

    gfx->setTextColor(RGB565_CYAN);
    gfx->printf("Power:\n");
    gfx->printf("  VBUS: %u mV (%s)\n", vbus_mv, is_vbus_in ? "CONNECTED" : "DISCONNECTED");
    if (batt_connected) {
      gfx->printf("  BATT: %u mV (%d%%) %s\n", batt_mv, batt_pct, is_charging ? "[CHARGING]" : "");
    } else {
      gfx->printf("  BATT: Not connected\n");
    }
  }
}
