# FlipPomodoroTimer

基于 ESP32-S3 的桌面多面翻转时钟与番茄钟：拟真科技感 48 段彩虹分段外环，板载 PCF85063 硬件 RTC 真实时钟常驻药丸框显示；开机自动连接 Wi-Fi 执行 NTP 授时后自动切断射频节能；四面不同时长独立倾角倒计时（5/10/30/60 分钟）；**实时 2.5D/3D 动力学透视物理跟随渲染**；**向后翻转平放自动暂停倒计时**，**四面立起屏幕自适应重力方向自动旋转保持正向**；双交互徽章（番茄钟模式 / 桌面大时钟模式）随时触控切换；翻转完全盖下自动息屏节能，立回原面无缝恢复；支持屏幕电容触控与物理按键启停、状态机单调时钟防漂移计算、AXP2101 电源与电量监测、ES8311 最大音量开始/完成和弦音。

本项目采用纯 CLI 工具链开发，支持完全从命令行复现构建与烧录，无需重型 IDE 依赖。

---

## 当前状态与实机验证

项目已完成拟真产品级重塑升级，并在微雪 **ESP32-S3-Touch-LCD-4B** 实体开发板上烧录并通过实机遥测验证。

### 已实现并经实机验证（Tested & Verified）

- **硬件识别与通信**：通过板载 CH343 USB-to-UART 桥接芯片连接 Windows（`COM3`，波特率 115200 / 烧录 921600），成功识别 ESP32-S3R8（16MB Quad Flash、8MB Octal PSRAM OPI 模式）。
- **板载硬件 RTC 与 Wi-Fi NTP 自动授时**：
  - 驱动板载 PCF85063 超低功耗日历时钟芯片（I2C 地址 `0x51`）；
  - 开机自动连接配置的 Wi-Fi 热点，请求阿里云/国际 NTP 时间同步（GMT+8 北京时间），成功将年月日时分秒校准写入 PCF85063 硬件寄存器；
  - 授时完成后立即主动关闭 Wi-Fi 射频（`WiFi.disconnect(true); WiFi.mode(WIFI_OFF);`），消除射频发热并节省电池电量；
  - 屏幕正下方绘制深色拟物药丸胶囊框，常驻精准显示 `HH:MM` 真实生活时间。
- **2.5D / 3D 动力学实时透视渲染引擎**：
  - 在 8MB Octal PSRAM 中通过 `ps_malloc` 分配 480×480 RGB565 快照纹理缓冲区（约 460.8KB）；
  - 采用**方案 B 物理跟随**：当用户手持立方体在侧立面之间旋转翻滚时，根据 QMI8658 实时解算的重力倾角偏移 $\theta = \text{atan2}(a_x, a_y)$，以约 35FPS 逐列进行扫描线梯形透视投影，并叠加侧向模拟光照明暗着色（Tilt Shading）；
  - 当立方体落稳在桌面时平滑回正，完全消除机械生硬感。
- **48 段彩虹分段式外环与全新视觉设计**：
  - 外径 218、内径 196，精细划分为 48 个圆弧刻度段，呈现平滑过渡的彩虹光谱（青蓝 $\to$ 橙红 $\to$ 翠绿 $\to$ 霓虹紫）；
  - 倒计时或秒针推进时，按进度点亮饱和高亮色彩，未激活分段保留 20% 暗色微光荧光质感；
  - 中央超大高对比白色时间字体原地双色无缝刷新，彻底消除闪烁。
- **双模态触控交互徽章**：
  - **左上角徽章「⏳ 番茄钟」**：专注/休息倒计时模式，显示剩余时间、轮数与立面时长；
  - **右上角徽章「🕒 时钟」**：桌面大时钟模式，中央显示 `HH:MM:SS` 实时走时，彩虹环显示秒走字，底部显示年月日星期；
  - GT911 触摸屏根据当前屏幕旋转重映射坐标，随时轻触徽章即刻无缝切换模式。
- **四面独立预设时长与自适应重力方向屏幕自动旋转**：
  - 匹配实体立方体产品的 4 面预设时长：
    - **面三（下边着台，`ay > +0.35g`）**：30 分钟深度专注，屏幕旋转 0（正向 0°）；
    - **面二（左边着台，`ax < -0.35g`）**：10 分钟快速冲刺，屏幕旋转 1（顺时针 90°）；
    - **面一（上边着台，`ay < -0.35g`）**：5 分钟短休/闪电番茄，屏幕旋转 2（倒置 180°）；
    - **面四（右边着台，`ax > +0.35g`）**：60 分钟长程专注，屏幕旋转 3（顺时针 270°）；
  - 旋转触发时单帧重绘全界面，所有文本与圆环永远正向朝向用户，无任何画面拉伸与错位。
- **向后翻转暂停与翻转盖下息屏**：
  - **向后翻转平放（`az <= -0.50g`）**：将设备向后推倒平放于桌面（屏幕朝上仰卧桌面），姿态识别为 `ORIENTATION_BACKWARD_PAUSE`，立即自动暂停倒计时，并保持 100% 满血高亮背光，让用户清晰看清当前暂停状态与轮数；立回原面自动播放清脆和弦音继续倒计时；
  - **翻转完全盖下（`az >= +0.50g`）**：将设备屏幕朝下扣在桌面上，识别为 `ORIENTATION_FACE_DOWN`，自动暂停并切断背光完全息屏节能。
- **100% 满血高亮背光控制**：依据硬件原理图纠正背光控制反转逻辑，GPIO 4 输出 `LOW` 实现 100% 满血最高亮度，彻底告别暗屏；翻转盖下输出 `HIGH` 完全切断背光，实现零光害极低功耗。
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
Sketch uses 1320307 bytes (41%) of program storage space. Maximum is 3145728 bytes.
Global variables use 48348 bytes (14%) of dynamic memory, leaving 279332 bytes for local variables. Maximum is 327680 bytes.
```

### 3. 烧录固件到开发板

将开发板的 `USB TO UART` Type-C 接口连接至电脑，确认串口号（如 Windows 下为 `COM3`）：

```bash
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none "firmware/FlipPomodoroTimer"
```

### 4. 查看运行遥测日志

波特率为 `115200`，可以使用串口调试工具或 Python 脚本查看开机自检、NTP 同步与 1Hz 实时状态输出：

```bash
python -c "import serial, time; ser = serial.Serial('COM3', 115200, timeout=1); [print(ser.readline().decode('utf-8', errors='replace').strip()) for _ in iter(int, 1)]"
```

实机遥测输出示例（实测 Wi-Fi NTP 校时、硬件 RTC 走时、3D 透视引擎与翻转暂停）：
```text
==================================================
  Cube Flip Clock & Timer (ESP32-S3-Touch-LCD-4B)
==================================================
Init Display... OK
Init Power Manager... OK
Init RTC & WiFi NTP Sync... [RTC] PCF85063 Ready. Current Time: 2026-10-28 21:35:58
OK
[WIFI] Connecting to SSID:  ...
......
[WIFI] Connected! IP: 192.168.3.112, RSSI: -67 dBm
[NTP] Sync Success: 2026-10-28 21:36:03
[WIFI] WiFi disconnected and RF powered off for power saving.
Init QMI8658 IMU... OK
Init Perspective 3D Engine... [3D] Perspective 3D Engine initialized with PSRAM buffer.
OK
Init Input (Touch & Button)... OK
Init Audio (ES8311 Codec)... OK
System Ready! Tilt cube or tap badges.

[STATUS] Mode: POMO | Time: 30:00 | RTC: 21:36 | Face: 3 (Rot:0) | State: 0 | Ori: 向后翻平(暂停) | VBUS: 5059mV | Batt: 0mV (-1%)
[STATUS] Mode: POMO | Time: 30:00 | RTC: 21:36 | Face: 3 (Rot:0) | State: 0 | Ori: 向后翻平(暂停) | VBUS: 5057mV | Batt: 0mV (-1%)
[IMU] Orientation Changed -> 面二(10分) (ax=-0.98, ay=0.02, az=-0.14)
[DISPLAY] Auto-Rotate: 0 -> 1 (90 deg)
[POMO] Tilt to New Face 2 (600 sec) -> Start Timer!
[STATUS] Mode: POMO | Time: 10:00 | RTC: 21:36 | Face: 2 (Rot:1) | State: 1 | Ori: 面二(10分) | VBUS: 5057mV | Batt: 0mV (-1%)
```
