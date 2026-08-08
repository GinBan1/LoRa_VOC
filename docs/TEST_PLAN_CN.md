# 系统联调与验收清单

本文用于验收当前版本的 STM32 环境节点与 ESP32 网关。除“未来 30 分钟预测”外，下面各项均可在现有硬件和固件上完成。

## 1. 烧录前检查

- STM32：E22-400M22S、BME680、SGP40、OLED 均使用 3.3 V 逻辑并共地，LoRa 天线已接好。
- ESP32：LoRa 与 OLED 接线和 `ESP32_Gateway/README.md` 的引脚表一致。
- 两端 LoRa 参数一致：470.3 MHz、SF7、BW125 kHz、CR 4/5、private sync word、CRC 开启。
- ESP32 已通过 `idf.py menuconfig` 配置本机 Wi-Fi 和 OneNET 凭据，`sdkconfig` 不提交 Git。

## 2. STM32 独立启动

1. 烧录 `MDK-ARM/LoRaWAN/LoRaWAN.hex` 后退出调试器，再按一次板载复位键。
2. USART1 使用 115200、8-N-1，应出现传感器、TinyML 和 LoRa 初始化信息。
3. OLED 应在不连接调试器的情况下正常显示。
4. 等待约 90 秒：前 60 秒为 SGP40/VOC 算法预热，随后收集 30 个 1 Hz 点；之后 AI 状态才有效。

通过标准：冷启动和按键复位均不依赖 Keil Debug；没有 `[FATAL]`、HardFault、Malloc Failed 或 Stack Overflow。

## 3. LoRa 连续链路与休眠唤醒

1. 两端上电，观察 ESP32 串口日志。
2. STM32 每约 5 秒发送一帧 `AQ1,...*CRC`，E22 在两次上报之间进入 warm sleep。
3. 连续观察至少 10 分钟，记录 ESP32 的序号、RSSI、CRC/协议错误计数。

通过标准：序号持续递增；无连续丢包；CRC/协议错误不增长；ESP32 OLED 数据每次收到新帧后更新。偶发单包丢失可以由 `seq` 发现，但不能出现 LoRa 任务永久停止。

## 4. 网关断网与恢复

1. ESP32 正常联网并确认 OneNET 收到属性。
2. 关闭路由器或断开上级网络 2 分钟。
3. 断网期间继续观察 ESP32 OLED 与 LoRa 接收日志。
4. 恢复网络，等待 Wi-Fi/MQTT 自动重连。

通过标准：断网不影响 LoRa 接收和 OLED；恢复网络后无需复位即可重新连接并上传最新有效序号；不会重复上传同一个 `seq`。

## 5. 数据有效性与陈旧状态

- BME680 无效时，温湿度和气压不得显示或上传伪造的 0。
- SGP40 无效时，VOC Index 不得显示或上传。
- AI 未完成预热/窗口采集时，显示 `WARMUP`，不得把分数当作有效预警。
- 超过 15 秒没有收到有效 AQ1 帧时，ESP32 OLED 显示 `STALE`，OneNET 停止上传陈旧数据。
- CRC、字段数量或数值范围错误的帧必须被拒绝，同时保留最后一份正确数据。

## 6. FreeRTOS 稳定性

1. 连续运行至少 2 小时，建议最终做 24 小时稳定性测试。
2. 每 60 秒检查串口是否出现 `[RTOS] WARN`。
3. 同时确认传感器 1 Hz、TinyML、OLED 和 LoRa 5 秒上报均持续工作。

当前告警阈值：ApplicationTask 剩余栈低于 1024 字节、LoRaTask 低于 512 字节时打印警告。出现警告后再增加相应任务栈，而不是盲目扩大所有栈。

## 7. OneNET 属性

在 OneNET 上核对：`Temp`、`humidity`、`pressure`、`voc_index`、`ai_state`、`warning_score`、`lora_rssi`。数值应与 ESP32 串口/节点 OLED 的同一时刻数据相符。

注意：当前 MQTT 连接仍为明文 TCP 1883。它适合局域网原型验证，不应承载敏感数据；正式公网部署前应迁移到平台支持的 TLS 接口并校验证书。

## 8. 暂缓项目

- 新环境事件数据采集、标签清洗和训练/测试集按场景隔离。
- “未来 30 分钟趋势”模型训练与独立测试。
- 根据新模型的输入窗口、输出含义和资源占用，替换当前 Edge Impulse 模型及 OLED 文案。

当前模型判断的是“最近 30 秒窗口处于 normal 或 warning”，不是未来 30 分钟预测；在新数据与新标签准备好之前，不应将它描述为 30 分钟预测模型。
