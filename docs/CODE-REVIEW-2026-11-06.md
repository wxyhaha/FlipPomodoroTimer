# 2026-11-06 代码检查记录

## 范围与方法

检查当前工作区，包括已有的“时钟页翻面保持页面”未提交修改。此次只检查并记录，不修改固件、不烧录、不提交。

- 阅读主草图、计时状态机、姿态、输入、电源、RTC、音频、显示合成与帧缓冲管理，以及它们使用的本地驱动接口；补查 HardwareTest 和资源/CAD 辅助脚本。
- 对当前源代码生成主机测试程序：保留实际计时、IMU、输入和主循环逻辑，以模拟时钟、传感器和显示替代硬件。通过 WSL g++、AddressSanitizer 和 UndefinedBehaviorSanitizer 执行。测试不代表设备验收，也不证明 ESP32 上的并发或时序表现。
- 临时验证文件位于被 Git 忽略的 `build/review-20261106-tests.py`、`build/review-20261106-qa.py` 和 `build/review-host/`，不是正式自动化测试套件。

## 待修复问题

### 1. P1：音频与面板控制对共享 TCA9554 输出缓存的访问没有互斥

位置：`firmware/FlipPomodoroTimer/AudioManager.cpp:135`、`DisplayUI.cpp:127`，以及 `firmware/libraries/GFX_Library_for_Arduino/src/databus/Arduino_XCA9554SWSPI.cpp:225`。

音频任务在 core 0 切换功放 GPIO，主循环在息屏/唤醒时通过同一个 expander 发送面板命令。驱动的 digitalWrite 先读取 output_buf，再写整个输出寄存器，最后更新缓存；没有覆盖整个读改写操作的互斥。单次 Wire 事务的互斥不能保护这段缓存读改写。

验证：用当前驱动 digitalWrite 原函数模拟两任务交错，LCD 将 CS 拉低期间插入功放开启，预期输出 `0x8e`，实际为 `0x86`，功放更新被覆盖。反向交错也可能覆盖 LCD 控制引脚状态。已证明竞争路径，实际发生频率及面板命令受扰表现未实测。

建议：对共享 expander 的完整读改写做任务级互斥，或将其所有运行期访问集中到同一任务。不可仅保护音频请求队列，也不宜持自旋锁执行阻塞 I2C。

### 2. P2：USB 自动暂停在拔线后永久阻挡换面计时，直到手动恢复或重置

位置：`firmware/FlipPomodoroTimer/FlipPomodoroTimer.ino:49`、`:213`。

复现：运行 30 分钟计时 → 插 USB，自动暂停 → 拔 USB，回番茄钟页 → 翻到 10 分钟面。当前 pauseReason 仍为 PAUSE_BY_USB，侧立分支无条件跳过，任何新面都不能启动。现有未提交改动去掉了原来的 stableVbusIn 限定，因此问题扩展到拔 USB 之后。

主机测试结果：物理姿态已变，activeFace 仍为 3，状态仍暂停，pauseReason 为 USB。保留“拔线不自动继续”是合理的，但不应同时永久禁止之后的主动换面；需要区分拔线本身与后续新手势。

### 3. P2：完成确认后的显示时长与实际启动时长不一致

位置：`firmware/FlipPomodoroTimer/PomodoroState.h:132`、`:111`。

复现：30 分钟专注结束 → 点击确认完成 → 显示 05:00 → 再次点击开始。confirmCompleted 将总时长改为休息时长，但 toggle 在 IDLE 无条件调用 startFocus，立刻变回 30:00。startBreak 没有来自主交互的调用路径。

主机测试输出：`displayed=300, started=1800`。建议明确产品规则：若仍是四面独立计时，完成确认后应保持一致的面时长；若需要专注/休息循环，则增加明确的待启动阶段，不能用时长相等判断阶段（5 分钟面与默认休息时长相同）。

### 4. P2：朝下开机时，初始姿态被消费但息屏动作未执行

位置：`firmware/FlipPomodoroTimer/IMUSensor.h:52`、`FlipPomodoroTimer.ino:182`。

当初始化第一次加速度采样成功并读到屏幕朝下，init 直接将 candidateOrientation 复制为 currentOrientation；随后静置不会产生 oriChanged。面板关闭只在 oriChanged 分支执行，setup 只设置显示方向，因此设备可一直亮屏，直到发生下一次姿态变化。

主机测试保持朝下两秒后：`orientation=FACE_DOWN, panelOff=false`。建议启动时应用一次已确认姿态的显示/计时策略，或使初始有效姿态经过统一的事件路径。初始化读样时序仍需设备测试。

### 5. P2：动画期间开始的触摸在动画结束后释放仍会操作计时

位置：`firmware/FlipPomodoroTimer/FlipPomodoroTimer.ino:230`、`InputManager.h:86`。

setRotation 只取消方向变化时已经存在的触摸。之后动画中按下，会正常记录 touchStartTime；若动画结束后再松手，主循环只检查释放当下的 isAnimating，点击因此被接受，可能意外开始、暂停计时或切页。

主机测试：动画标记开启时按下，300ms 后结束动画并松手，IDLE 变为 FOCUS_RUNNING。建议动画期间持续禁止建立有效触摸，且保持抑制到该次手指释放。

### 6. P2：RTC 连接成功被等同于时间有效，未检查掉电后的时钟完整性

位置：`firmware/FlipPomodoroTimer/RTCManager.h:22`。

RTC begin 成功后即设置 isReady 并显示寄存器时间；未检查本地 SensorPCF85063 已提供的 isClockIntegrityGuaranteed。无 Wi-Fi 配置或 NTP 失败时，掉电后不可信的时间也会作为正常时间显示，用户无法区分。读取日期时间时的 I2C 错误也没有在应用层传播为无效状态。

此项为源码确认，未执行真实 RTC 掉电测试。建议区分设备可通信与时间有效，成功授时并核验前使用占位/待校时提示。

### 7. P3：触摸初始化失败仍打印 OK，手动息屏后可能失去触摸唤醒入口

位置：`firmware/FlipPomodoroTimer/InputManager.h:31`。

touch.begin 失败只留下 touchReady=false，init 仍无条件返回 true，所以主草图报告初始化 OK。BOOT 仍可工作，但用户无法从启动日志判断触摸不可用，息屏交互也缺少降级提示。

建议分别暴露 BOOT 与触摸的可用性；启动日志明确触摸失败，并说明可用的替代唤醒方式。此项由失败路径代码确认，未断开实物触摸排线测试。

### 8. P2（诊断草图）：HardwareTest 使用反向背光电平，无法可靠用于屏幕排障

位置：`firmware/HardwareTest/HardwareTest.ino:44`。

项目当前 PowerManager 已依据本机硬件规定 GPIO 4 LOW 为点亮、HIGH 为关闭，但 HardwareTest 初始化写 HIGH，之后未恢复 LOW；因此单独烧录该草图会保持关闭背光，容易误判屏幕损坏。其 Z 轴 FACE UP / FACE DOWN 标签也与当前主固件的实测方向相反。

建议同步诊断固件的背光与姿态约定，并在调用 expander 引脚方法前完成初始化。此次未烧录诊断草图。

## 需要明确规则的边界，不计为确定缺陷

- 时钟页切到 10 分钟物理面，再切回番茄钟、点击开始，可能使用先前的 30 分钟时长。主机已复现；但文档也规定切页不重置计时，因此是否应该按当前面重选，只能结合“已有计时”和“从未开始”的产品规则决定，不应贸然修改。
- 扣放后通过 BOOT 再次启动，当前暂停动作只在姿态变化时执行，不会持续强制暂停。应明确“扣放期间始终禁止计时”还是“扣放事件暂停一次”。
- GT911 驱动忽略数据就绪标志、先清状态再读坐标；应用又将返回 0 直接视为手指释放。需要结合实际触控采样和设备日志确认是否存在误释放，不将推测计入已复现问题。

## 验证结果与边界

- 主机原逻辑测试：暂停/恢复/正常到期基线通过；复现上述第 2、3、4、5 项以及时钟切回后的时长选择边界。
- 对当前 expander digitalWrite 原函数进行受控交错，复现第 1 项丢失更新。
- 主机程序以 `-Wall -Wextra -fsanitize=address,undefined` 编译运行；此覆盖不替代双核硬件并发测试。
- 在隔离目录重新生成 DigitFont.h、PianoAudio.h，均与仓库逐字节一致；提示音长度分别为 1.03 秒、2.10 秒。
- 9 个项目 Python 脚本语法解析通过；17 份项目维护的 Markdown 本地链接检查通过（检查时尚未加入本报告）。未重新生成、覆盖或验收外壳 STL。
- `git diff --check` 通过；已有修改保留。
- 整机编译通过（退出码 0）：Arduino CLI 1.5.1、ESP32 Arduino 3.3.11，使用 README 的 ESP32-S3 / 16MB Flash / OPI PSRAM 参数。Flash 1,599,327 / 3,145,728 字节（50%），静态 RAM 49,036 / 327,680 字节（14%）。日志为 `build/review-20261106-compile.log`。这是包含本地配置的本机验证构建，不是公开分发固件。
- 未烧录、未串口操控设备；PWRKEY 双击、低电量/充电、长期运行、真实触摸和面板开关并发仍需实机验收。没有审计所有第三方库的全部代码，也不声称不存在其他缺陷。

## 后续修复（2026-11-06）

针对上述检查结果已修改固件：共享 TCA9554 的面板命令与音频功放切换使用同一互斥锁；拔除 USB 后保留暂停但允许新面手势；完成确认后保持当前面的预设时长；初始化时若读到朝下姿态则关闭面板和背光；动画期间的触摸抑制至松手。触摸初始化失败会报告失败；RTC 检查时钟完整性并在无效时显示占位；HardwareTest 修正背光极性和姿态标签。

这些修改需要实机验证，特别是屏幕与音频并发、USB 插拔、初始朝下、触摸和 RTC 掉电/NTP 校时。RTC 驱动读取失败未完整向应用传播；不能将完整性位检查等同于所有 I2C 错误均已处理。

修复后整机编译通过：Flash 1,601,227 字节，静态 RAM 49,156 字节。主机模拟验证了暂停/恢复、USB 拔线后换面、完成后的时长和动画触摸抑制；初始朝下处理在 setup 中，未由该模拟程序覆盖。尚未烧录或进行实机验收。
