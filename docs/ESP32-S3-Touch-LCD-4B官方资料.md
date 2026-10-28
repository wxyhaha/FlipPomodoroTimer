# ESP32-S3-Touch-LCD-4B

URL: https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/

[![ESP32-S3-Touch-LCD-4B](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-4B-1-f799db9025fc3b3634ce4374cf5ce639.webp)](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-4B.htm)

ESP32-S3-Touch-LCD-4B 是一款由微雪电子 (Waveshare) 推出的搭载 ESP32-S3 2.4GHz Wi-Fi 和 BLE 5 模组的开发板，集成 16MB Flash 和 8MB PSRAM。板载 4 英寸 480 × 480 分辨率 RGB 接口的 LCD 屏，可流畅运行 LVGL 等 GUI 界面程序。结合多种外设接口，可快速开发 ESP32-S3 的 HMI，适用于智能中控面板、家庭网关、智能交互面板、工业控制以及智能灯控等应用场景。

**如果你在寻找：**

- **使用 AI 工具编程** ：点击 **发给 AI** 并选择 AI 工具，询问与本页面相关的问题；也可以选择 **复制为 Markdown** ，将页面内容复制并粘贴到 AI 工具中辅助编程。
- **原理图、规格书和示例代码** ：参见 [相关资料](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/Resources-And-Documents)
- **出厂固件** ：参见 [使用说明](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/Instructions-For-Use)
- **Arduino 开发** ：参见 [Arduino 开发](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/Arduino)
- **ESP-IDF 开发** ：参见 [ESP-IDF 开发](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/ESP-IDF)
- **问题排查** ：联系 [技术支持](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4B/Technical-Support)

| SKU | 产品 |
| --- | --- |
| 32052 | ESP32-S3-Touch-LCD-4B |

## 产品特性

- 搭载 ESP32-S3R8 高性能 Xtensa 32 位 LX7 双核处理器，主频高达 240MHz
- 支持 2.4GHz Wi-Fi (802.11 b/g/n) 和 Bluetooth 5 (LE)，板载天线
- 内置 512KB 的 SRAM 和 384KB ROM，叠封 8MB PSRAM 和外接 16MB Flash
- 采用 Type-C 接口，提高了用户的使用便捷性和设备的兼容性

## 硬件说明

- 板载 4 英寸电容触摸屏，480 × 480 分辨率，65K 彩色
- 内置 ST7701 驱动芯片和 GT911 电容触控芯片，分别使用 RGB 和 I2C 接口通信
- 板载 QMI8658 六轴惯性测量单元 (3 轴加速度、3 轴陀螺仪），可检测运动姿态、计步等功能
- 板载 PCF85063 RTC 芯片，通过 AXP2101 接入电池，实现不间断供电
- 板载 PWR、BOOT 两个可自定义功能的侧边按钮，方便使用按钮进行自定义功能开发
- 板载 3.7V PH2.0 锂电池充放电接口
- 使用 AXP2101 的好处包括高效的电源管理、支持多种输出电压、充电和电池管理功能以及对电池寿命的优化

## 资源简介

![ESP32-S3-Touch-LCD-4B-details-intro](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-4B-details-intro-71b4ad1cfe93a92cbd624555a480adb4.webp)

1. **ESP32-S3-WROOM-1-N16R8 模组** Wi-Fi 蓝牙 SoC 模块，240MHz 运行频率，封装 8MB PSRAM 和 16MB Flash
2. **AXP2101** 高集成度的电源管理芯片
3. **ES7210 回声消除算法芯片** 可用于消除回声，提高音频采集准度
4. **ES8311 低功耗音频编解码芯片** 用于处理音频编码、解码
5. **PCF85063** RTC 时钟芯片
6. **QMI8658** 六轴惯性测量单元 (IMU)，包含一个 3 轴陀螺仪和一个 3 轴加速度计
7. **TCA9554PWR** IO 扩展芯片
8. **LCD 接口** 连接 4 寸 RGB 显示屏
9. **Type-C 接口 USB TO UART** 可用于供电与程序烧录、调试
10. **USB Type-C 接口** ESP32-S3 USB 接口，用于供电、烧录程序和日志打印
11. **贴片麦克风** 麦克风输入及回声消除
12. **扬声器** MX1.25 2P 连接器，支持 8Ω 2W 喇叭
13. **PWRKEY 电源按键** 可控制电源通断，支持自定义功能
14. **BOOT 按键** 用于设备启动和功能调试
15. **CHG LED 灯** 锂电池充电指示灯
16. **PH2.0 锂电池接口** PH2.0 2P 连接器，可用于接入 3.7V 锂电池，支持充放电
17. **扩展接口（2.0mm 间距）** 可用于外接 IO

## 产品尺寸

![ESP32-S3-Touch-LCD-4B-details-size](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-4B-details-size-10826e511c001d8541b2a98abc4337d6.webp)

[文档反馈](https://wj.qq.com/s2/26077836/3c81/?q-2-rR1G=https%3A%2F%2Fdocs.waveshare.net%2FESP32-S3-Touch-LCD-4B%2F)