# 本项目对本地依赖的修改

## GFX Library for Arduino 1.6.0

修改文件：

- `GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel.h`
- `GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel.cpp`

本项目增加 `enableDoubleBuffer()` 和只读 `panelHandle()`。启用后配置 `num_fbs=2`，供应用通过 ESP-IDF RGB API 提交后台帧缓冲及注册 VSYNC 回调。默认配置仍为单缓冲，原有调用不自动改变。

当前目标环境为 ESP32-S3、ESP32 Arduino 3.3.11；不宣称已在其他 SDK 版本验证。应用的 FramePresenter 管理双缓冲交接，不能同时使用旧的直接向 Arduino_RGB_Display 前缓冲写入的路径。

升级库时必须迁移或替换此扩展，并重新验证屏幕初始化、同步交接、局部更新和旋转。正常构建使用本仓库依赖目录，不能直接以未打补丁的上游版本替代。保留原库许可证及版权说明。
