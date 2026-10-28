# FlipPomodoroTimer

基于微雪 ESP32-S3-Touch-LCD-4B 的翻转番茄钟。四个侧面对应 5 / 10 / 30 / 60 分钟，向后翻平暂停，立回原面继续，换到其他面直接重新计时。支持触摸切换番茄钟与桌面时钟页。

## 当前版本：V6.1 稳定刷新与轻量二维旋转

本轮在 V5 实屏反馈基础上放大布局、改为原生字形、局部更新与 VSYNC 双缓冲，并加入二维旋转动画。编译、烧录和诊断记录见 [V6 验收记录](docs/UI-V6-VALIDATION.md)；用户已确认数字显示正常，并确认 V6.1“动画顺畅，仍然不闪”；其他功能的覆盖范围见验收记录。

- 番茄钟：20 段圆环，每组 5 段红、黄橙、绿、青蓝主色；组内轻微渐变，圆弧边缘抗锯齿，角部轻微圆润；外径从 360px 放大为 400px，圆心上移。
- 中央使用本项目自绘七段数码字，显示 MM:SS；下方以较小数字显示当前 HH:MM。
- 时钟页无圆环，显示大号 HH:MM、小号秒数、中文年月日和“星期日”至“星期六”。
- 两页左上是沙漏模式图标，右上是时钟模式图标；当前模式青色高亮，非当前模式灰色。
- 移除面号、完成轮数和底部说明；内部仍保留原有计时统计，不显示到界面。
- 移除 3D 透视与明暗变形。静止时只重画变化的数字区域，使用两个驱动帧缓冲及 SDK 帧释放回调交接；不再每秒重传整张画面到前缓冲。
- 翻面加入目标 240ms 的平面旋转过渡，快速连续翻面时转向最新方向；动画末帧使用整数坐标。动画用临时小纹理降低计算与复制开销，落位恢复原生清晰画面；实际时长与帧数由诊断记录，未承诺固定帧率。
- 屏幕方向使用 100ms 候选稳定时间及 1.25 倍轴向优势判断，计时面识别仍保持 600ms 防抖。实际响应还受主循环、绘制和提示音耗时影响。

## 操作

| 操作 | 行为 |
| --- | --- |
| 点左上沙漏 | 切换到番茄钟页，不重置计时 |
| 点右上时钟 | 切换到时钟页，计时可在后台继续 |
| 点番茄钟页非模式按钮区域 | 开始、暂停或继续 |
| 点时钟页空白区域 | 无动作，避免误操作后台计时 |
| 单击 BOOT | 开始、暂停或继续计时 |
| 长按 BOOT 至少 1.5 秒 | 重置计时 |
| 翻到不同计时面 | 无需确认，从该面的预设时长重新开始，并切回番茄钟页 |
| 向后翻平，屏幕朝上 | 暂停并保持屏幕点亮 |
| 从翻转暂停立回原面 | 恢复剩余时间 |
| 屏幕朝下扣放 | 暂停并关闭背光 |

图标可见直径约 52px，触摸热区为 56×56px：沙漏 x=16..71，时钟 x=408..463，均为 y=8..63。坐标随显示方向映射，旋转中已开始的触摸会取消，避免释放时误触。

圆环表示剩余时间：开始时 20 段全亮，每段对应总时长的 5%，耗尽部分降为暗色；暂停时整体略暗，并显示金色暂停标识。未接电池显示空电池及短横线，不将未知电量画成满电。

## 硬件与技术栈

- 主板：ESP32-S3-Touch-LCD-4B，16MB Flash、8MB OPI PSRAM、4 英寸 480×480 LCD。
- 外设：QMI8658、GT911、PCF85063、AXP2101、ES8311，具体接口见[硬件文档](docs/HARDWARE.md)。
- 工具：Arduino CLI、ESP32 Arduino 核心、仓库内 Arduino_GFX / SensorLib / XPowersLib。
- 开机尝试 Wi-Fi NTP 授时至板载 RTC，然后关闭 Wi-Fi；离线使用 RTC，RTC 初始化失败时显示占位符。
- Wi-Fi 凭证来自 Git 忽略的本地配置，公开源码不包含私人网络信息；未配置时跳过联网校时，继续使用硬件 RTC。
- 电池插拔切换、续航与外壳仍待实测，不由本次 UI 改版认定完成。

## 文档入口

- [项目开发注意事项](AGENTS.md)：提交前必须检查文档并同步更新。
- [硬件与条件](docs/HARDWARE.md)
- [开发计划](docs/PLAN.md)
- [UI 设计历史与当前规格](docs/design/UI-DESIGN.md)
- [V6 实现与验收记录](docs/UI-V6-VALIDATION.md)
- [V5 历史实现记录](docs/UI-V5-VALIDATION.md)
- [本地依赖补丁](firmware/libraries/LOCAL_PATCHES.md)
- [官方资料摘录](docs/ESP32-S3-Touch-LCD-4B官方资料.md)
- [微雪官方 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)

## 快速上手与编译烧录指南

项目完全支持通过 `arduino-cli` 命令行一键编译与上传，无需配置繁重的 IDE。

### 1. 编译环境要求

- **操作系统**：Windows / macOS / Linux
- **工具链**：`arduino-cli v1.5.1`（原环境记录）
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

## 可选的 Wi-Fi 校时配置

将 `firmware/FlipPomodoroTimer/WiFiConfig.example.h` 复制为同目录的 `WiFiConfig.local.h`，填入自己的 SSID 和密码后编译。这个文件已被 Git 忽略，不应提交或分享。未创建本地配置时仍可构建并离线运行；硬件 RTC 本身需要具有正确时间。

公开发布固件二进制时，应在没有私人本地配置的环境中构建，避免将本机凭证包含在固件中。

## 数码字资源复现

`firmware/FlipPomodoroTimer/DigitFont.h` 为本项目原创几何字形在四个最终像素尺寸生成的数字蒙版（64×104、70×116、24×40、23×39），静止显示时直接按原生像素绘制，不使用低分辨率蒙版放大。生成文件已入库，正常构建不需要 Python。

修改字形时运行 `python tools/generate_digit_font.py`，只需要 Python 3.10+ 标准库。图形设计稿不作为固件运行时图片使用。

## 公开复现前仍需补齐

确认完整的工具链安装与固定版本流程、第三方库及字库许可证、项目许可证、电池测试、外壳和干净环境复现。当前能本地编译不代表这些发布条件已经完成。
