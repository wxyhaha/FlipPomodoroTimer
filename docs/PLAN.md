# 开发与复现计划

## 开发方式与技术架构

本项目遵循“**底层硬件全打通 $\to$ 核心状态机 $\to$ 传感器手势闭环 $\to$ 低功耗与外壳装配**”的循序渐进路径。
固件采用纯命令行工具链（`arduino-cli v1.5.1` + `esp32:esp32@3.3.11`），消除对庞大图形 IDE 的环境依赖，确保任何开发者均可快速一键复现。

---

## 阶段进展与验收交付

| 阶段 | 工作内容 | 完成状态 | 已验证的交付成果 / 同步留存资料 |
| :--- | :--- | :--- | :--- |
| **1. 到货检查与首次上电** | 检查微雪开发板实物丝印；USB 连接电脑识别；核对硬件配置。 | **已完成** (实机验证) | 确认型号为微雪 ESP32-S3-Touch-LCD-4B（16MB Flash, 8MB PSRAM OPI）；识别串口为 `COM3`（CH343 芯片，波特率 115200）。详情见 [HARDWARE.md](HARDWARE.md)。 |
| **2. 编译环境与硬件驱动** | 建立 arduino-cli 编译与本地依赖环境；跑通屏幕、I2C 外设驱动。 | **已完成** (实机验证) | 编写并实测 `HardwareTest` 与本地库，成功点亮 ST7701 480×480 RGB 屏，打通 TCA9554、AXP2101、QMI8658、GT911、ES8311 芯片总线。 |
| **3. 中文 UI 与无闪烁渲染** | 原生 U8g2 UTF-8 汉字排版；双色直写抗每秒闪烁；微局部刷新防点击跳动。 | **已完成** (实机验证) | 启用 `u8g2_font_unifont_t_chinese4` 7199 常用字库，界面全中文原生显示；彻底废除每秒 `fillRect` 刷黑与点击 `fillScreen` 全屏清空，实现极致平滑稳定的视觉体验。 |
| **4. 四面倾角与翻转控制** | QMI8658 四面倾角识别（5/15/25/45 分钟）；翻转盖下自动暂停熄屏；翻回恢复。 | **已完成** (实机验证) | 实测四面侧立倾角重力加速度分布，识别面一（5m）、面二（15m）、面三（25m）、面四（45m）独立无缝切面计时；实测平放恢复与盖下息屏节能；引入 600ms 滑动窗口防抖。 |
| **5. 满血高亮与最大音量** | 纠正 GPIO 4 背光反转（LOW 100% 满血亮度）；拉满 ES8311 音频音量与开始/结束和弦音。 | **已完成** (实机验证) | 修正背光三极管驱动逻辑，全屏 100% 清晰高亮；配置 ES8311 硬件音量 100、正弦波满幅 28000；加入计时开始双音和弦与计时结束三连音警报。 |
| **6. 电池接入与续航实测** | 核对 PH2.0 锂电极性；设置 AXP2101 安全充电；断电无缝切换与续航实测。 | **进行中** (硬件待接) | AXP2101 I2C 监测与 4.2V/500mA 安全充电配置已在固件中生效并验证通过；待 3.7V 电池到货后接入实测断电切换与连续续航。 |
| **7. 外壳设计与装配** | 测量屏幕外形与电池尺寸；设计 3D 打印四面倾角放置仓体；预留 Type-C 口。 | **待开展** | 待根据实物尺寸进行 CAD 建模，提供可编辑模型文件、STL 与切片打印参数，契合四个侧立倾角。 |
| **8. GitHub 复现发布** | 整理代码仓库、清理敏感信息；编写完整复现教程、配置开源许可证。 | **进行中** | 遵循 AGENTS.md 规范进行提交前全量文档同步；整理代码与依赖结构。 |

---

## 翻面计时规则设计（已实机落地版本）

结合 QMI8658 的 $(a_x, a_y, a_z)$ 重力加速度向量分布，已实现完整的四面独立倒计时手势状态机：

- **桌面平放（Screen Up）**：屏幕 100% 点亮，居中超大字体显示当前面倒计时与环形进度条。若此前为盖下暂停，平放回桌面即自动无缝恢复。
- **面一（前倾立面，`ay < -0.40g`）**：5 分钟（短休息 / 闪电番茄）。立起此面播放开始提示音并立即开始 5 分钟倒计时。
- **面二（左倾立面，`ax < -0.40g`）**：15 分钟（快速任务 / 敏捷冲刺）。立起此面播放开始提示音并立即开始 15 分钟倒计时。
- **面三（后倾立面，`ay > +0.40g`）**：25 分钟（标准番茄钟专注）。立起此面播放开始提示音并立即开始 25 分钟倒计时。
- **面四（右倾立面，`ax > +0.40g`）**：45 分钟（深度学习 / 长程专注）。立起此面播放开始提示音并立即开始 45 分钟倒计时。
- **翻转完全盖下（Screen Down）**：将开发板屏幕向下盖在桌面上（$a_z > +0.45g$），自动暂停当前倒计时，并拉高 GPIO 4 关闭背光（完全熄屏），实现零光害极低功耗静默待机。
- **轻触 / 按键干预**：轻触屏幕任意区域或单击板载 BOOT 键，可随时手动暂停或继续。
- **长按复位**：长按板载 BOOT 键 $\ge 1.5$ 秒，安全复位当前计时器。
- **倒计时归零**：倒计时结束时，触发高分贝三连音和弦警报，徽章平滑呼吸闪烁提示。

---

## 固件调试与复现命令速查

| 操作 | 对应命令 |
| :--- | :--- |
| **编译固件** | `arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none --libraries "firmware/libraries" "firmware/FlipPomodoroTimer"` |
| **烧录固件** | `arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none "firmware/FlipPomodoroTimer"` |
| **串口监听** | `python -c "import serial, time; ser = serial.Serial('COM3', 115200, timeout=1); [print(ser.readline().decode('utf-8', errors='replace').strip()) for _ in iter(int, 1)]"` |
