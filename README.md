# FlipPomodoroTimer

基于 ESP32-S3 的桌面多面翻转番茄钟：全中文原生界面，四面不同时长独立倾角倒计时（5/15/25/45 分钟）；**向后翻转平放自动暂停倒计时**，**四面立起屏幕自适应重力方向自动旋转保持正向**；翻转完全盖下自动息屏节能，立回原面无缝恢复；支持屏幕电容触控与物理按键启停、状态机单调时钟防漂移计算、AXP2101 电源与电量监测、ES8311 最大音量开始/完成提示音。

本项目采用纯 CLI 工具链开发，支持完全从命令行复现构建与烧录，无需重型 IDE 依赖。

---

## 当前状态与实机验证

项目已完成功能升级并在微雪 **ESP32-S3-Touch-LCD-4B** 实体开发板上烧录并通过实机遥测验证。

### 已实现并经实机验证（Tested & Verified）

- **硬件识别与通信**：通过板载 CH343 USB-to-UART 桥接芯片连接 Windows（`COM3`，波特率 115200 / 烧录 921600），成功识别 ESP32-S3R8（16MB Quad Flash、8MB Octal PSRAM OPI 模式）。
- **原生全中文 UTF-8 显示**：
  - 激活 `Arduino_GFX` 底层的 `U8G2_WITH_UNICODE` 与 `U8G2_USE_LARGE_FONTS` 编译宏，打通 Unicode 编码 > 255 的中文字模解码通道；
  - 集成 `u8g2_font_unifont_t_chinese4`（覆盖 7199 个标准常用汉字），开启 `gfx->setUTF8Print(true)`；
  - 全屏界面彻底消除乱码，清晰显示「专注计时中」、「休息进行中」、「倒计时完成」、「向后翻平(暂停)」、「翻转盖下(息屏)」、「面三(25分)」等原生汉字。
- **向后翻转暂停与翻转盖下息屏**：
  - **向后翻转平放（`az <= -0.50g`）**：将设备向后推倒平放于桌面（屏幕朝上仰卧桌面），姿态识别为 `ORIENTATION_BACKWARD_PAUSE`，立即自动暂停倒计时，并保持 100% 满血高亮背光，让用户清晰看清当前暂停状态与轮数；立回原面自动播放清脆和弦音继续倒计时；
  - **翻转完全盖下（`az >= +0.50g`）**：将设备屏幕朝下扣在桌面上，识别为 `ORIENTATION_FACE_DOWN`，自动暂停并切断背光完全息屏节能。
- **自适应重力方向屏幕自动旋转**：
  - 针对 4.0 英寸 480×480 正方形屏幕，基于 QMI8658 加速度计实时解算重力方向，在硬件层面动态调用 `gfx->setRotation(0..3)`：
    - **面三（下边着台，`ay > +0.35g`）**：25 分钟标准番茄钟，屏幕旋转 0（正向 0°）；
    - **面二（左边着台，`ax < -0.35g`）**：15 分钟快速任务，屏幕旋转 1（顺时针 90°）；
    - **面一（上边着台，`ay < -0.35g`）**：5 分钟短休息，屏幕旋转 2（倒置 180°）；
    - **面四（右边着台，`ax > +0.35g`）**：45 分钟深度专注，屏幕旋转 3（顺时针 270°）；
    - **向后翻平或移动中**：保持前一次屏幕方向，避免平放或拿取时旋转跳动；
  - 旋转触发时单帧重绘全界面，所有文本与圆环永远正向朝向用户，无任何画面拉伸与错位。
- **100% 满血高亮背光控制**：依据硬件原理图纠正背光控制反转逻辑，GPIO 4 输出 `LOW` 实现 100% 满血最高亮度，彻底告别暗屏；翻转盖下输出 `HIGH` 完全切断背光，实现零光害极低功耗。
- **彻底消除每秒闪烁与点击跳动**：
  - 废除每秒调用 `fillRect` 刷黑数字矩形的做法，改用 `setTextColor(UI_COLOR_WHITE, UI_COLOR_BG)` 前背景双色直写模式，每秒时间数字原地覆盖，单帧无缝，肉眼完全零闪烁；
  - 废除屏幕点击状态切换时的 `fillScreen(UI_COLOR_BG)` 全屏刷黑重绘，仅做胶囊徽章与底部卡片的微局部平滑重绘，点击界面纹丝不动。
- **四面独立倾角倒计时模式**：
  - 基于 QMI8658 六轴姿态传感器 $(a_x, a_y)$ 重力分量精确识别 4 个侧立面；
  - 侧立换面自动无缝切换至对应面的预设时长并重新开始全新倒计时；立回原面或向后翻平自动记忆状态；
  - 保持 600ms 滑动窗口平滑滤波，手持拿取移动或桌面敲击不误触。
- **最大音量与多场景提示音**：
  - ES8311 硬件音量寄存器配置为 `100`（硬件满幅输出）；
  - I2S 16 位音频正弦波幅度拉满至 `28000`（接近 32767 满幅），彻底解决声音偏小的问题；
  - **开始提示音**：计时开始/恢复/切面时，播放专属清脆双音和弦（C5 523Hz $\to$ G5 784Hz）；
  - **完成警报音**：倒计时归零时，播放高分贝三连音和弦警报（E5 659Hz $\to$ G5 784Hz $\to$ C6 1046Hz）；
  - **按键提示音**：精简为 15ms 快速微脉冲，消除触控操作延迟。
- **双模交互控制**：GT911 电容触摸屏轻触、或板载 BOOT 实体按键（GPIO 0）单击实现「开始/暂停」切换；长按 $\ge 1.5$ 秒实现安全计时复位。
- **单调时钟状态机**：以 ESP32 单调时钟 `millis()` 为基准计算耗时与剩余时间，暂停期间保持剩余时间不变，彻底避免因界面刷新调度导致的计时累积漂移。
- **电源管理与安全性**：AXP2101 PMIC I2C 通信正常，实时采集 VBUS 输入电压（实测 5.05V）与电池电量，预设安全锂电池充电截止电压 4.2V 与充电电流 500mA。

### 待实机验证事项（Pending Verification）

1. **锂电池续航实测**：目前使用 USB 供电调试，等待 3.7V PH2.0-2P 锂电池组到货后，进行断电无缝切换与全亮/息屏续航实测。
2. **外接 8Ω 2W 扬声器装壳音质**：板载音频功放驱动实体喇叭发声，装壳后视共鸣腔体效果进一步微调频率。

---

## 硬件规格与引脚映射

- **开发板型号**：微雪 [ESP32-S3-Touch-LCD-4B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)
- **核心主控**：ESP32-S3-WROOM-1-N16R8（Xtensa Dual-Core 240MHz, 16MB Flash, 8MB PSRAM OPI）
- **屏幕配置**：4.0 英寸 480×480 RGB 接口（ST7701 驱动，背光引脚 GPIO 4，LOW 亮屏 / HIGH 息屏）
- **触控芯片**：GT911（I2C 地址 `0x5D`）
- **姿态传感器**：QMI8658 六轴 IMU（I2C 地址 `0x6B`）
- **电源管理**：AXP2101 PMIC（I2C 地址 `0x34`，PH2.0-2P 锂电接口）
- **IO 扩展**：TCA9554PWR（I2C 地址 `0x20`，控制 LCD 复位、片选、时钟数据及音频功放使能）
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
  esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none
  ```

### 2. 编译固件

在项目根目录下执行编译命令（本地依赖已集成在 `firmware/libraries/` 中）：

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none --libraries "firmware/libraries" "firmware/FlipPomodoroTimer"
```

编译成功输出示例：
```text
Sketch uses 726240 bytes (23%) of program storage space. Maximum is 3145728 bytes.
Global variables use 24616 bytes (7%) of dynamic memory, leaving 303064 bytes for local variables. Maximum is 327680 bytes.
```

### 3. 烧录固件到开发板

将开发板的 `USB TO UART` Type-C 接口连接至电脑，确认串口号（如 Windows 下为 `COM3`）：

```bash
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none "firmware/FlipPomodoroTimer"
```

### 4. 查看运行遥测日志

波特率为 `115200`，可以使用串口调试工具或 Python 脚本查看 1Hz 实时状态输出：

```bash
python -c "import serial, time; ser = serial.Serial('COM3', 115200, timeout=1); [print(ser.readline().decode('utf-8', errors='replace').strip()) for _ in iter(int, 1)]"
```

实机遥测输出示例（实测向后翻转暂停、重力屏幕旋转与切面）：
```text
[STATUS] Time: 24:58 | Face: 3 (Rot:0) | State: 1 | Ori: 面三(25分) | VBUS: 5057mV (in:1) | Batt: 0mV (-1%)
[IMU] Orientation Changed -> 向后翻平(暂停) (ax=0.00, ay=0.01, az=-0.99)
[POMO] Auto Pause by Backward Flip!
[STATUS] Time: 24:56 | Face: 3 (Rot:0) | State: 2 | Ori: 向后翻平(暂停) | VBUS: 5058mV (in:1) | Batt: 0mV (-1%)
[IMU] Orientation Changed -> 面二(15分) (ax=-0.98, ay=0.02, az=-0.14)
[DISPLAY] Auto-Rotate: 0 -> 1 (90 deg)
[POMO] Tilt to New Face 2 (900 sec) -> Start Timer!
[STATUS] Time: 15:00 | Face: 2 (Rot:1) | State: 1 | Ori: 面二(15分) | VBUS: 5057mV (in:1) | Batt: 0mV (-1%)
[STATUS] Time: 14:59 | Face: 2 (Rot:1) | State: 1 | Ori: 面二(15分) | VBUS: 5057mV (in:1) | Batt: 0mV (-1%)
```
