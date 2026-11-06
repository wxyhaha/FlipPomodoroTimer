# V12.9 屏幕开口检查

微雪 [ESP32-S3-Touch-LCD-4B 官方尺寸图](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-4B-details-size-10826e511c001d8541b2a98abc4337d6.webp)给出的显示区域为 71.93×71.93mm，面板外形为 86.50×86.50mm。模型最窄开口为 72.8×72.8mm，内口圆角 R0.8mm；假设显示区居中，名义直边余量每侧约 0.435mm，四角余量更小。

V12.9 网格检查共 81 种视角与对位组合：9 种正视对位未发现遮挡；有 14 种斜视组合存在少量遮挡。检查结果见 [screen-check.json](v12.9/screen-check.json)，对应模板为 [screen-opening-template.svg](v12.9/screen-opening-template.svg)。

这是模型检查，假设显示区相对外框居中，未模拟显示层深度或玻璃折射；水平偏移取 ±0.05mm、垂直偏移取 ±0.2mm。面板右侧未设置独立防脱锁，实物偏移可能不同。打印收缩、毛边和装配误差也可能改变遮挡情况，实际可视范围需结合成品观察。
