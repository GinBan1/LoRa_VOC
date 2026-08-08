# STM32F407 环境边缘 AI 与 LoRa 网关系统

本项目是一套面向室内环境监测的边缘设备原型。STM32F407 负责传感器采集、TinyML 状态判断、OLED 显示和 LoRa 发射；ESP32 网关负责 LoRa 接收、网关 OLED、Wi-Fi 与 OneNET/MQTT 上报。当前模型输出的是 `normal` / `warning` 环境状态，适合做污染事件预警；它不是“提前知道未来会有人放入污染源”的因果预测器。

## 系统架构

```text
BME680 + SGP40
       │ I2C
       ▼
STM32F407VET6 ── TinyML ── 本地 OLED
       │
       │ SPI / E22-400M22S / AQ1
       ▼
ESP32 Gateway ── 网关 OLED
       │
       └── Wi-Fi / MQTT ── OneNET
```

STM32 与网关之间使用私有 LoRa 点对点链路，不依赖 LoRaWAN 网关或运营服务。CRC 只用于发现传输错误，不等同于加密或身份认证。

## 硬件与关键连接

- 主控：STM32F407VET6，系统时钟 168 MHz。
- 环境传感器：BME680（温度、湿度、气压）与 SGP40（原始 VOC、VOC Index）。
- 无线模块：E22-400M22S（SX1268），当前配置 470.3 MHz、10 dBm、SF7、BW125、CR 4/5、private sync word。
- 本地显示：0.96 英寸 I2C OLED。
- 网关：ESP32 + 第二块 E22-400M22S + 0.96 英寸 I2C OLED。

STM32 当前关键引脚：

| 功能 | STM32 引脚 |
| --- | --- |
| SPI1 SCK / MISO / MOSI | PA5 / PA6 / PA7 |
| LoRa NSS / NRST / BUSY / DIO1 | PA4 / PA2 / PA1 / PA0 |
| LoRa TXEN / RXEN | PC4 / PB0 |
| I2C1 SCL / SDA | PB6 / PB7 |
| USART1 TX / RX | PA9 / PA10 |

实际焊接和上电前，请同时核对原理图、模块数据手册与 `LoRaWAN.ioc`；所有器件必须共地，射频模块必须接匹配天线后再发射。

## 目录说明

| 目录或文件 | 用途 |
| --- | --- |
| `APP/` | 环境采集、TinyML、OLED、LoRa、调度与遥测协议等应用模块 |
| `Core/` | STM32CubeMX 生成的 HAL、启动与中断代码 |
| `EdgeImpulse/` | 当前 Edge Impulse C/C++ 推理库、模型参数与 TFLite 模型；纳入 Git |
| `MDK-ARM/` | Keil MDK-ARM 工程；构建产物不纳入 Git |
| `ML/` | 本地采集、清洗和 Edge Impulse 数据准备脚本 |
| `NET/` | STM32 侧预留网络相关代码 |
| `ESP32_Gateway/` | 完整 ESP32 网关工程，独立使用 ESP-IDF 构建 |
| `LoRaWAN.ioc` | STM32CubeMX 工程配置 |

串口记录、训练 CSV、自动生成报告、Keil/ESP-IDF 构建目录和本地 `sdkconfig` 均被 `.gitignore` 排除。仓库保留可复现所需的源码、脚本、工程文件和当前 Edge Impulse 模型。

## STM32 构建与烧录

1. 安装 STM32CubeMX、Keil MDK-ARM、STM32F4 Device Family Pack 和 ST-Link 驱动。
2. 用 STM32CubeMX 打开 `LoRaWAN.ioc`，核对时钟、SPI1、I2C1、USART1、GPIO 与中断配置。
3. 需要重新生成代码时选择 MDK-ARM 工具链。保留 `USER CODE` 区域，并确认自定义 `APP/`、`EdgeImpulse/` 文件及包含路径仍在 Keil 工程中。
4. 用 Keil 打开 `MDK-ARM/LoRaWAN.uvprojx`，执行 Build。
5. 连接 ST-Link，烧录并打开 USART1 串口查看启动、传感器、TinyML 和 LoRa 状态。

STM32 的 FreeRTOS 应用入口位于 `APP/rtos_app.c`。正常模式下 `ApplicationTask` 以 1 Hz 完成环境采样、TinyML 和 OLED 刷新；较高优先级的 `LoRaTask` 由 DIO1 事件或发送截止时间唤醒，不进行忙轮询。E22 在两次 5 秒上报之间使用 warm sleep，CPU 空闲时由 FreeRTOS Idle Hook 执行 `WFI`。数据采集模式和 LoRa 链路测试模式由对应 APP 头文件中的编译开关控制。

任务栈每 60 秒自动检查一次，只有低于保守阈值时才打印 `[RTOS] WARN`。当前完整 Keil 构建结果为 0 Error、0 Warning；不应仅因代码文件较多就盲目扩大任务栈。

## AQ1 LoRa 遥测协议

ASCII 帧格式固定为：

```text
AQ1,seq,uptime_ms,flags,temp_x10,humidity_x10,pressure_x10,sraw_voc,voc_index,ai_state,confidence_x1000,normal_x1000,warning_x1000*CCCC
```

- `seq`：包序号；`uptime_ms`：STM32 启动后的毫秒数。
- `flags`：bit0 表示 BME680 有效，bit1 表示 SGP40 有效，bit2 表示 AI 结果有效。
- 温度、湿度和气压以定点整数传输，接收端分别除以 10。
- 概率和置信度除以 1000 后得到 0～1 浮点值。
- `ai_state`：0 `WARMING_UP`，1 `COLLECTING_WINDOW`，2 `NORMAL`，3 `WARNING`，4 `UNCERTAIN`，5 `ERROR`。
- `CCCC`：4 位大写十六进制 CRC16-CCITT-FALSE，poly `0x1021`、init `0xFFFF`，只覆盖 `*` 之前的正文。

接收端必须严格检查字段数、CRC、数值范围与包龄。解析失败时应保留最后一份有效数据，并分别统计协议错误和 CRC 错误。

## OneNET 属性映射

网关仅在 Wi-Fi、MQTT 和遥测快照有效且数据未过期时上传现有物模型属性：

| OneNET 标识符 | 来源 | 平台范围 |
| --- | --- | --- |
| `Temp` | `temp_x10 / 10.0` | 0.0～50.0，步长 0.1 |
| `humidity` | `humidity_x10 / 10.0` | 0.0～100.0，步长 0.1 |
| `pressure` | `pressure_x10 / 10.0` | 300.0～1100.0 hPa，步长 0.1 |
| `voc_index` | SGP40 VOC Index | 0～500，无量纲指数 |
| `ai_state` | TinyML 状态编号 | 0～5 |
| `warning_score` | `warning_x1000 / 1000.0` | 0.000～1.000 |
| `lora_rssi` | 网关接收 RSSI | -150～0 dBm |

网关应按 `seq` 去重；发送失败不能把该序号标记为已上传。以上标识符必须先在 OneNET 物模型中按表创建，ESP32 才会上传对应属性。

## 数据与模型复刻

1. 将 STM32 切换到采集模式，传感器预热后以 1 Hz 记录完整事件和正常环境。
2. 把本地 CSV 放入 `ML/raw_sessions/`，命名与字段约定见该目录的 `README_CN.md`。
3. 安装脚本依赖并生成训练/测试文件：

   ```powershell
   python -m pip install numpy pandas
   python ML/prepare_edge_impulse_sessions_v2.py
   ```

4. 按 `ML/edge_impulse_warning_state_v2/README_CN.md` 将生成数据分别上传到 Edge Impulse 的 Training 和 Test，并采用固定的窗口、归一化与训练参数。
5. 在独立测试集上确认混淆矩阵，重点检查 `warning` 召回率；然后导出 C++ Library，替换 `EdgeImpulse/` 中的模型并重新构建 STM32。
6. 先用串口和 OLED 验证本地结果，再验证 AQ1 收发和 OneNET 上报。

原始 CSV 可能包含环境特征、时间和位置相关信息，且体积较大，因此不纳入 Git。脚本生成的 CSV 也不纳入 Git；它们可由本地原始记录重复生成。

## 安全与合规

- 禁止把 Wi-Fi 密码、OneNET 产品密钥、设备密钥、Token、证书私钥或真实个人信息提交到仓库。
- 凭据应保存在未跟踪的本地配置、环境变量或设备 NVS 中；公开日志前先脱敏。
- 若 MQTT 经过公网，正式部署应评估 TLS、证书校验、密钥轮换和最小权限；明文 MQTT 不适合承载敏感信息。
- LoRa private sync word 与 CRC 都不是加密。需要防伪造或保密时，应在应用层增加认证与加密，并设计重放保护。
- 频率、发射功率、天线和占空比必须符合部署地区的无线电法规；不要在未接天线时发射。
- 人工污染实验应使用低风险、可控气味源，保持通风，避免可燃、有毒或高浓度挥发物。

## 当前完成状态与下一步

当前版本已经具备：FreeRTOS 多任务、独立 LoRa 状态机、SX1268 warm sleep、AQ1 严格解析与 CRC、ESP32 断网不阻塞接收/显示、Wi-Fi/MQTT 自动重连、OneNET 真实遥测上传、两端 OLED 和 RTOS 栈余量告警。STM32 与 ESP32 均已通过完整构建。

尚未完成的是需要新数据支撑的“未来 30 分钟环境趋势预测”。当前 Edge Impulse 模型只对最近 30 秒窗口做 `normal` / `warning` 状态分类，不能冒充未来预测。采集工作恢复后，应按场景隔离数据、定义明确的 30 分钟未来标签，并使用独立测试场景验证。

完整硬件联调步骤见 [`docs/TEST_PLAN_CN.md`](docs/TEST_PLAN_CN.md)。
