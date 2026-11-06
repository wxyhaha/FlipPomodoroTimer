# 提示音

| 场景 | 当前提示 |
| --- | --- |
| 开始、换面重新计时、暂停后恢复 | 柔和电钢琴双音，约 1.03 秒 |
| 倒计时到期 | 柔和电钢琴上扬旋律，约 2.10 秒，播放一次 |
| 普通操作 | 15ms 短提示音，不抢占计时提示 |

使用 ES8311、板载扬声器和功放；提示音由 FreeRTOS 后台任务播放，不要求主循环等待。确认完成、重置或开始新计时会取消旧的完成提示。音色和抢占的完整到期场景仍需结合设备实听检查。

## 喇叭测试

设备启动后通过 USB UART（115200）发送 `AUDIO_TEST` 和换行，依次播放开始音与完成音，不改变倒计时。安装 `pyserial` 后也可运行：

```powershell
python -m pip install pyserial
python tools/test_audio.py --port COM3
```

将 `COM3` 换成实际串口，避免其他串口工具同时占用。脚本确认软件播放完成；音量、音质和喇叭输出需要实际听取。

## 资源复现

`python tools/generate_piano_audio.py` 生成 `firmware/FlipPomodoroTimer/PianoAudio.h` 和本地试听文件；生成头文件已入库，普通构建无需重新生成。音频为 16kHz、16 位，双声道输出相同信号；DAC 音量设为 0dB。
