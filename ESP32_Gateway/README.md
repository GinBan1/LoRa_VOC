# ESP32 LoRa 网关

接收 STM32 的 AQ2 遥测，更新 OLED，并通过 USB 串口把带 AI 字段的新帧转发给电脑。可选 OneNET 环境数据上报和中文邮件。网关不执行节点 MLP，正式邮件跟随 STM32 固定规则确认的事件。

## 硬件连接

| 功能 | ESP32 GPIO |
|---|---|
| LoRa SCK / MISO / MOSI | 18 / 19 / 23 |
| LoRa NSS / NRST / BUSY / DIO1 | 27 / 25 / 34 / 26 |
| LoRa TXEN / RXEN | 32 / 33 |
| OLED SCL / SDA | 22 / 21 |

3.3 V LoRa 模块和 I²C OLED 与 ESP32 共地，按模块额定电压供电。GPIO34 只作输入。OLED 使用 SSD1306 兼容屏，探测地址 `0x3C`、`0x3D`。当前工程频点为 470.3 MHz。

## 构建与使用

按[首次安装与烧录](../docs/OPEN_SOURCE_SETUP_CN.md)配置 ESP-IDF 5.5.5 工程。只看 OLED 和串口时，不用启用云端或邮件。监视器连的是 **ESP32 串口**，不是 STM32 串口。

有效的 AQ2 帧携带 VOC 指数、规则事件状态和可选 AI 扩展字段；旧 AQ1 兼容显示不触发正式邮件。超过 15 秒未收到有效更新时，网关将数据标为过期。CRC 只检错，不加密也不认证发送端。

可选 OneNET 上传 AQ2 的 `Temp`、`humidity`、`pressure`、`voc_index`、`lora_rssi` 环境属性；AQ2 的两路事件状态不写入旧预测物模型。连接使用项目内置 OneNET CA，云端更换证书时须更新并重建，不改为明文连接。

启用邮件及测试入口后，可在串口输入 `mail test` 并换行。详细步骤见[邮件说明](../docs/ALARM_MAIL_CN.md)。