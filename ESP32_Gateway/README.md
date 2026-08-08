# ESP32 LoRa 网关

此目录是空气质量监测项目的 ESP32 接收端：接收 STM32F407 通过 E22-400M22S（SX1268）发送的遥测帧，显示关键状态，并经 Wi-Fi 上传至 OneNET MQTT。

> 凭据不在源码中保存。请通过 `menuconfig` 写入本机的 `sdkconfig`；不要提交该文件。

## 硬件连接

所有模块必须共地，LoRa 模块和 OLED 均使用 **3.3 V** 供电。E22 必须接好 433/470 MHz 匹配天线后再发送。

| 功能 | ESP32 GPIO | 说明 |
| --- | ---: | --- |
| LoRa MOSI | GPIO23 | SPI MOSI |
| LoRa MISO | GPIO19 | SPI MISO |
| LoRa SCK | GPIO18 | SPI 时钟 |
| LoRa NSS/CS | GPIO27 | SPI 片选 |
| LoRa BUSY | GPIO34 | 仅输入 |
| LoRa TXEN | GPIO32 | 射频发射使能 |
| LoRa RXEN | GPIO33 | 射频接收使能 |
| LoRa NRST | GPIO25 | 复位 |
| LoRa DIO1 | GPIO26 | 中断/状态 |
| OLED SDA | GPIO21 | I²C 数据 |
| OLED SCL | GPIO22 | I²C 时钟 |

OLED 使用 SSD1306 兼容的 0.96 英寸 I²C 屏，程序优先探测地址 `0x3C`，必要时回退到 `0x3D`。

## 配置、构建与烧录

建议使用兼容的 ESP-IDF 5.x（本工程使用新版 I²C 主机驱动接口）。在 ESP-IDF 终端中执行：

```powershell
cd E:\HOME\LoRaWAN\ESP32_Gateway
idf.py set-target esp32
idf.py menuconfig
```

在 **“空气环境 LoRa 网关配置”** 中填写：

- Wi-Fi SSID 与密码；
- OneNET 产品 ID、设备名称、Access Key；
- Token 过期时间（Unix 时间戳，单位秒，必须是未来时间）。

随后构建和烧录：

```powershell
idf.py build
idf.py -p COMx flash monitor
```

将 `COMx` 换成实际串口。`sdkconfig.defaults` 只包含 ESP32、4 MB Flash 等安全基础配置，不含任何个人凭据。

## AQ1 遥测协议

STM32 发送的 ASCII 帧格式如下，`*` 前内容采用 CRC-16/CCITT-FALSE 校验，校验值为 4 位大写十六进制：

```text
AQ1,seq,uptime_ms,flags,temp_x10,humidity_x10,pressure_x10,sraw_voc,voc_index,ai_state,confidence_x1000,normal_x1000,warning_x1000*CRC16
```

- `flags`：bit0=BME680 有效，bit1=SGP40 有效，bit2=AI 推理有效。
- 温度、湿度、气压分别以 0.1 °C、0.1 %RH、0.1 hPa 缩放。
- `ai_state`：0 预热、1 采集、2 正常、3 预警、4 不确定、5 错误。
- 三个 `*_x1000` 字段是 0~1000 的置信度/类别分数。

网关仅接受 CRC、格式和范围均有效的帧；连接 OneNET 后仅上传最新且未重复的序列号。

## OneNET 物模型标识符

当前上传字段需在 OneNET 物模型中创建为只读属性：

| 标识符 | 建议类型与范围 | 含义 |
| --- | --- | --- |
| `Temp` | float，0~50 | 温度（°C） |
| `humidity` | float，0~100 | 相对湿度（%RH） |
| `pressure` | float，300~1100 | 气压（hPa） |
| `voc_index` | int，0~500 | VOC Index，无物理单位 |
| `ai_state` | int，0~5 | AI 状态枚举 |
| `warning_score` | float，0~1，步长 0.001 | 预警分数，仅 AI 有效时上传 |
| `lora_rssi` | int，-150~0 | 最近一帧 LoRa RSSI（dBm） |

网络任务独立运行，Wi-Fi/MQTT 重连不会阻塞 LoRa 接收或 OLED 刷新。
