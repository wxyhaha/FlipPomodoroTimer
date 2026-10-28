#pragma once

// ESP32-S3-Touch-LCD-4B assignments cross-checked against the public schematic.
// Keep this header aligned with hardware/pin-audit.md and the official BSP.
#define XPOWERS_CHIP_AXP2101

#define BOARD_I2C_SDA            47
#define BOARD_I2C_SCL            48

#define BOARD_EXPANDER_ADDRESS   0x20
#define BOARD_EXPANDER_LCD_RST   7
#define BOARD_EXPANDER_LCD_CS    0
#define BOARD_EXPANDER_LCD_SCL   2
#define BOARD_EXPANDER_LCD_SDA   1
#define BOARD_EXPANDER_AMP_CTRL  3
#define BOARD_EXPANDER_TOUCH_RST 5
#define BOARD_EXPANDER_TOUCH_INT 6

#define BOARD_LCD_DE             17
#define BOARD_LCD_VSYNC          3
#define BOARD_LCD_HSYNC          46
#define BOARD_LCD_PCLK           9
#define BOARD_LCD_R0             10
#define BOARD_LCD_R1             11
#define BOARD_LCD_R2             12
#define BOARD_LCD_R3             13
#define BOARD_LCD_R4             14
#define BOARD_LCD_G0             21
#define BOARD_LCD_G1             8
#define BOARD_LCD_G2             18
#define BOARD_LCD_G3             45
#define BOARD_LCD_G4             38
#define BOARD_LCD_G5             39
#define BOARD_LCD_B0             40
#define BOARD_LCD_B1             41
#define BOARD_LCD_B2             42
#define BOARD_LCD_B3             2
#define BOARD_LCD_B4             1
#define BOARD_LCD_BACKLIGHT      4
#define BOARD_LCD_WIDTH          480
#define BOARD_LCD_HEIGHT         480

#define PIN_ES7210_BCLK       16
#define PIN_ES7210_LRCK       7
#define PIN_ES7210_DIN        15
#define PIN_ES7210_MCLK       5
#define PIN_ES8311_DOUT       6
