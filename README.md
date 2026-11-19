# 基于 STM32 与 LoRa 的室内 VOC 事件检测系统

STM32F407 在设备端采集并判断 **已经发生的 VOC 指数扰动**，通过 LoRa 点对点发送给 ESP32 网关。网关负责显示、串口转发和可选的 OneNET 上报、中文邮件。电脑可查看实时曲线并保存 CSV。

- **采样与判断**：BME680 采温湿度和气压，SGP40 提供 VOC 指数。节点约每秒采一次、每 10 秒处理一个窗口。自适应 MLP 与固定规则并行；AI 分数在节点计算，电脑只显示。正式告警和恢复邮件仍由节点固定规则触发。
- **低功耗**：OLED 自动熄屏，空闲时尝试进入 STOP，由板载 32.768 kHz 晶振提供 RTC 时钟。已验证实板可休眠和唤醒；AI 就绪、熄屏时电池端常见读数约 6.8 mA。
- **用途**：在宿舍、办公室或小型工作间记录扰动何时出现、何时回落，结合现场活动排查可能原因。

当前模型的权重在 [voc_mlp_adapt_model.h](APP/voc_mlp_adapt_model.h)，推理代码在 [voc_mlp.c](APP/voc_mlp.c)。

## 使用入口

1. 新设备接线与固件构建：[首次安装与烧录](docs/OPEN_SOURCE_SETUP_CN.md)。
2. 已烧录设备：[启动与验收](docs/OPEN_SOURCE_SETUP_CN.md#4-验收)。
3. Windows 电脑查看实时数据：[监视器使用说明](docs/VOC_MONITOR_WINDOWS_CN.md)。
4. 独立配置：[ESP32 网关](ESP32_Gateway/README.md) · [邮件](docs/ALARM_MAIL_CN.md) · [低功耗](docs/LOW_POWER_PROFILE_CN.md)。
