# FlipPomodoroTimer

基于 ESP32-S3 的桌面翻转番茄钟：正面朝上专注倒计时，翻转盖下自动暂停并熄灭屏幕背光节能，翻回正面无缝恢复；支持屏幕电容触控与物理按键启停、状态机单调时钟防漂移计算、AXP2101 电源与电量监测、ES8311 音频提示。

本项目采用纯 CLI 工具链开发，支持完全从命令行复现构建与烧录，无需重型 IDE 依赖。

---

## 当前状态与实机验证

项目已完成首版功能固件开发并在微雪 **ESP32-S3-Touch-LCD-4B** 实体开发板上烧录验证通过。

### 已实现并经实机验证（Tested & Verified）

- **硬件识别与通信**：通过板载 CH343 USB-to-UART 桥接芯片连接 Windows（`COM3`，波特率 115200 / 烧录 921600），成功识别 ESP32-S3R8（16MB Quad Flash、8MB Octal PSRAM OPI 模式）。
- **显示与抗闪烁渲染**：基于 `GFX_Library_for_Arduino` 驱动 ST7701 4.0 英寸 480×480 RGB 接口 LCD，实现单帧局部防闪烁渲染（居中巨幅时间、圆环动态进度、状态徽章与操作指引卡片）。
- **翻转手势与抗抖动滤波**：驱动 QMI8658 六轴姿态传感器，实测桌面平放屏幕朝上重力加速度 $a_z \approx -1.0g$，翻转盖下 $a_z \approx +1.0g$。加入 600ms 滑动窗口稳定滤波，有效滤除桌面震动与拿取途中的瞬间颠簸。
- **自动暂停与屏幕熄灭省电**：专注计时过程中翻转盖在桌面上自动触发暂停，并拉低 GPIO 4 关闭 LCD 背光节能；翻回正面自动唤醒背光并无缝恢复倒计时。
- **双模交互控制**：GT911 电容触摸屏轻触、或板载 BOOT 实体按键（GPIO 0）单击实现「开始/暂停」切换；长按 $\ge 1.5$ 秒实现安全计时复位。
- **单调时钟状态机**：以 ESP32 单调时钟 `millis()` 为基准计算耗时与剩余时间，暂停期间保持剩余时间不变，彻底避免因界面刷新调度导致的计时累积漂移。
- **电源管理与安全性**：AXP2101 PMIC I2C 通信正常，实时采集 VBUS 输入电压（实测 5.1V）与电池电量，预设安全锂电池充电截止电压 4.2V 与充电电流 500mA。
- **音频系统初始化**：ES8311 音频 DAC 芯片与 TCA9554 EXIO3 功率放大器使能引脚已在固件中初始化就绪，预置按键反馈音与完成双音提示。

### 待实机验证事项（Pending Verification）

1. **锂电池续航实测**：目前使用 USB 供电调试，3.7V PH2.0-2P 锂电池组的无缝插拔切换与全亮/息屏续航数据待实测。
2. **外接扬声器响度**：板载 MX1.25 扬声器接口支持 8Ω 2W 喇叭，实体喇叭发声响度与音质待装壳后进一步试听微调。
3. **四面多时长独立倒计时**：当前固件已实现姿态分类器（正面朝上、翻转盖下、立式放置），后续将结合外壳建模升级为四面不同预设时长（如 5/15/25/45 分钟）独立翻面切换。

---

## 硬件规格与引脚映射

- **开发板型号**：微雪 [ESP32-S3-Touch-LCD-4B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)
- **核心主控**：ESP32-S3-WROOM-1-N16R8（Xtensa Dual-Core 240MHz, 16MB Flash, 8MB PSRAM）
- **屏幕配置**：4.0 英寸 480×480 RGB 接口（ST7701 驱动，背光引脚 GPIO 4）
- **触控芯片**：GT911（I2C 地址 `0x5D`）
- **姿态传感器**：QMI8658 六轴 IMU（I2C 地址 `0x6B`）
- **电源管理**：AXP2101 PMIC（I2C 地址 `0x34`，PH2.0-2P 锂电接口）
- **IO 扩展**：TCA9554PWR（I2C 地址 `0x20`，控制 LCD 复位、片选、时钟数据及音频功放）
- **音频系统**：ES8311 Codec（I2C 地址 `0x18`，I2S 引脚 BCLK 16, LRCK 7, DIN 15, MCLK 5, DOUT 6，功放门控 EXIO3）
- **板载按键**：BOOT 键（GPIO 0）

---

## 快速上手与编译烧录指南

项目完全支持通过 `arduino-cli` 命令行一键编译与上传，无需配置繁重的 IDE。

### 1. 编译环境要求

- **操作系统**：Windows / macOS / Linux
- **工具链**：`arduino-cli v1.5.1` 或以上
- **ESP32 Arduino 核心**：`esp32:esp32@3.3.11`
- **目标板 FQBN 参数**：
  ```bash
  esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
  ```

### 2. 编译固件

在项目根目录下执行编译命令（本地依赖已集成在 `firmware/libraries/` 中）：

```bash
arduino-cli compile --libraries firmware/libraries --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" firmware/FlipPomodoroTimer
```

编译成功输出示例：
```text
Sketch uses 425376 bytes (13%) of program storage space. Maximum is 3145728 bytes.
Global variables use 24616 bytes (7%) of dynamic memory, leaving 303064 bytes for local variables.
```

### 3. 烧录固件到开发板

将开发板的 `USB TO UART` Type-C 接口连接至电脑，确认串口号（如 Windows 下为 `COM3`）：

```bash
arduino-cli upload -p COM3 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" firmware/FlipPomodoroTimer
```

### 4. 查看运行遥测日志

波特率为 `115200`，可以使用串口调试工具或 Python 脚本查看 1Hz 实时状态输出：

```bash
python -c "import serial; ser=serial.Serial('COM3', 115200); [print(ser.readline().decode('utf-8', errors='replace').strip()) for _ in iter(int, 1)]"
```

典型串口遥测输出：
```text
[STATUS] Time: 24:58 | State: 1 | Ori: 正面朝上 | VBUS: 5102mV (in:1) | Batt: 0mV (-1%)
[IMU] Orientation Changed -> 翻转盖下 (az=0.98)
[POMO] Auto Pause by Flip Down!
[IMU] Orientation Changed -> 正面朝上 (az=-0.99)
[POMO] Auto Resume by Flip Up!
```

---

## 交互操作说明

| 动作 | 方式 | 系统响应 |
| :--- | :--- | :--- |
| **开始 / 暂停** | 轻触屏幕任意区域，或单击板载 BOOT 键 | 在「专注运行」与「暂停」之间切换 |
| **翻转暂停** | 将开发板翻转朝下、屏幕贴桌面放置 | 保持剩余时间，状态变为翻转暂停，自动熄灭背光省电 |
| **翻起继续** | 将开发板翻回正面朝上平放 | 自动点亮背光，恢复专注倒计时 |
| **复位计时** | 长按板载 BOOT 键 $\ge 1.5$ 秒 | 停止当前计时，回到 25 分钟初始准备状态 |
| **完成确认** | 计时结束蜂鸣/动画时，轻触屏幕或翻转 | 进入 5 分钟休息准备或重置为下一番茄钟 |

---

## 文档目录

- [项目开发注意事项](AGENTS.md)：代码提交前强制性文档同步与 Git 提交信息规范。
- [硬件与现有条件](docs/HARDWARE.md)：芯片清单、接口引脚映射、电池安全核对及测试记录。
- [开发与复现计划](docs/PLAN.md)：功能阶段规划、验收标准与后续多面翻转扩展。
- [官方资料汇总](docs/ESP32-S3-Touch-LCD-4B官方资料.md)：微雪原厂硬件与外设特性说明。
