# V5 界面实现与验收（历史记录）

用户后续实拍确认仍有字形模糊、每秒闪动、占屏偏小和方向直接跳转问题。修复方案及当前结果见 [V6 验收记录](UI-V6-VALIDATION.md)；以下保留当时实现和检查边界。

本轮实现对应 [V5 静态稿](design/UI-DESIGN.md)：20 段四主色圆环、沙漏/时钟模式图标、无环时钟页及中文星期。

## 实现

- DisplayUI.cpp：用 PSRAM 二维画布组成整页后提交 LCD；仅在时间、状态、电量、日期、模式或方向变化时绘制。不再逐帧变形，不把中间清屏画面直接提交屏幕。
- 20 个等间距圆弧段，18° 间距、约 16° 有色主体、轻微圆角；每 5 段为一组主色。使用 2×2 子像素覆盖缓存边缘，四组内分别插值，避免多余主色。
- 大号数字使用原创七段字形灰度蒙版及双线性缩放；tools/generate_digit_font.py 可重建已入库的 DigitFont.h，不依赖第三方字体下载。
- 顶部只保留沙漏模式按钮、电量与表盘模式按钮。未接电池显示短横线，不显示虚假满电。
- 时钟页不绘制圆环；显示 HH:MM、小秒数、年月日及中文星期。RTC 初始化失败时显示占位符。
- 面号、完成轮次、底部操作卡片已从显示层移除；不删除原有状态机内部统计。
- 显示方向使用 100ms 候选稳定确认、1.25 倍主轴优势；计时面仍为 600ms。二者独立，避免为了画面及时转正而提前重新计时。
- 四方向下同步变换触控坐标；旋转期间正在进行的触摸取消至手指释放。模式按钮只切页，时钟页空白触摸不切换后台计时；BOOT 原操作保留。

## 本地检查与固件交付

2026-10-28 本轮结果：

- 编译通过（退出码 0）：Flash 1,335,787 字节 / 3,145,728 字节（42%），静态 RAM 48,308 字节 / 327,680 字节（14%）。PSRAM 动态画布和圆环缓存不计入这一静态 RAM 数字。
- 已烧录 COM3（CH343，VID 1A86 / PID 55D3），写入校验通过并正常重启（上传退出码 0）。
- 被动串口观察捕获到 30:00 → 29:59 → 29:58 的连续计时，随后捕获 `[DISPLAY] 2D direction -> 90 deg`、`[POMO] Tilt to New Face 2 (600 sec) -> Start Timer!` 和 10:00 → 09:59；之后继续记录到 09:10 → 09:03，设备保持运行。
- 记录窗口内没有捕获到 Guru Meditation / Backtrace；本次监测在启动后接入，不能据此宣称捕获了全部上电自检。
- 本次日志验证了计时运行和一次实际换面/方向事件，没有测量端到端方向响应延迟，也没有代替时钟页、四方向触控和显示闪烁的人工验收。
- 本机完整编译和烧录输出分别在 `build/compile-ui-v5.log`、`build/upload-ui-v5.log`；筛选掉 Wi-Fi 身份信息的运行日志在 `build/ui-v5-boot.log`。build 已被 Git 忽略，公开复现依据本文命令与以上结果摘要。

复现构建（项目根目录）：

```powershell
arduino-cli compile --jobs 4 --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none --libraries firmware/libraries --output-dir build/ui-v5 firmware/FlipPomodoroTimer
```

确认实际串口后上传此次构建，避免上传历史缓存：

```powershell
arduino-cli upload --port COM3 --fqbn esp32:esp32:esp32s3 --input-dir build/ui-v5 firmware/FlipPomodoroTimer
```

COM3 是本机端口示例，其他电脑应执行 `arduino-cli board list` 后替换。构建产物位于已忽略的 build 目录，不提交二进制到源码工作区。

## 需要用户在设备上验收

- 左右上角沙漏/时钟能切换页面，当前模式高亮正确；四个方向均能点中。
- 圆环恰好 20 段，每组 5 段，完整显示；大数字、状态图标与当前时间不重叠。
- 时钟页无圆环，中文年月日、星期及秒数正确，跨日后日期能刷新。
- 每秒更新、切页与翻面没有明显黑闪、残影或撕裂。离屏合成不等于 VSYNC 原子换帧，仍需目测。
- 四侧方向转正响应可接受；对角线附近不反复跳转；向后暂停、原面恢复、换面直接重开。
- 电池续航、充电和外壳效果不在本次显示改版的验收结论中。
