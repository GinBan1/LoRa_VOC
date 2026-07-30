# STM32F407 + E22 LoRa + ESP32 网关项目路线

## 1. 最终架构

```text
BME680 / SGP40
       │ I2C
       ▼
STM32F407VET6 ── E22-400M22S ))))  私有 LoRa  (((( E22-400M22S ── ESP32
  采集 + TinyML                  点对点/小型星型                      │
                                                                      Wi-Fi
                                                                       │
                                                              MQTT / ThingsBoard
```

这里使用的是 LoRa 物理层加自定义协议，不是 LoRaWAN。它适合单个或少量固定节点的课程设计，成本低；地址、ACK、重传、去重、信道避让和安全需要自己实现。

## 2. 当前工程基线与阻断项

- MCU：STM32F407VET6，CubeMX 6.15.0，STM32CubeF4 1.28.3，Keil/ArmClang。
- 已有：I2C1、USART1、BME680、SGP40、OLED、Edge Impulse TinyML。
- 未有：SPI1、SX1268/E22 驱动、RTC、FreeRTOS。
- 最近 Keil 构建成功。工程当前不是 Git 仓库。
- `.ioc` 与生成的时钟代码不一致：`.ioc` 选择 HSE/LSE，`main.c` 实际从 HSI 运行。
- CubeMX 重新生成工程可能覆盖 Keil 中手工加入的 Edge Impulse 文件组、IncludePath 和编译宏。操作前必须完整备份或初始化 Git，生成后立即全量编译。

TinyML 当前不能视为已经验证：

- 模型元数据要求 10 个采样点 × 4 轴，共 40 个输入。
- `APP/ei_wrapper.cpp` 只提供 4 个 float，`signal.total_length` 也是 4。
- 模型轴为 `humidity + pressure + sraw_voc + future_sraw_voc_30m`，错误地包含未来值；代码实际传入 `temperature + humidity + pressure + sraw_voc`。
- 必须重新确认 Edge Impulse 的特征列，去掉未来值，以 1 Hz 连续 10 点窗口重新导出并用已知样本对拍。

## 3. 原理图检查

| E22 信号 | 模块脚 | STM32 引脚 | 配置 |
|---|---:|---|---|
| DIO1 | 13 | PA0 | GPIO_EXTI0，上升沿 |
| BUSY | 14 | PA1 | 输入，无上下拉 |
| NRST | 15 | PA2 | 开漏输出，默认释放为高 |
| NSS | 19 | PA4 | 推挽输出，默认高 |
| SCK | 18 | PA5 | SPI1_SCK |
| MISO | 16 | PA6 | SPI1_MISO |
| MOSI | 17 | PA7 | SPI1_MOSI |
| TXEN | 7 | PC4 | 推挽输出，默认低 |
| RXEN | 6 | PB0 | 推挽输出，默认低 |
| DIO2 | 8 | NC | MCU 手动控制射频开关时可不接 |

射频开关必须互斥：

| 状态 | TXEN | RXEN |
|---|---:|---:|
| Sleep / Standby | 0 | 0 |
| TX | 1 | 0 |
| RX | 0 | 1 |

绝不能让 TXEN 和 RXEN 同时为高。

电源与射频：

- E22 官方给出的 22 dBm 最大发射电流约 105 mA。3.3 V 稳压器至少按 200 mA 瞬态能力验证。
- VCC9 附近放 100 nF + 10–47 µF 低 ESR 电容，电源和地短而粗；电解电容应使用 `Cxx` 标号并写明容值/耐压。
- 所有 GND 焊盘就近多过孔接完整地平面。
- 使用模块 IPEX/U.FL 座时，ANT21 可以悬空；使用邮票孔 ANT21 时，必须走短直 50 Ω 馈线。两种出口只选一个。
- 没有连接匹配天线时禁止发射。

模块官方资料：

- https://www.ebyte.com/product/434.html
- https://www.ebyte.com/Uploadfiles/Files/2024-12-31/202412311627388791.pdf

## 4. CubeMX 第一轮：只加入 LoRa 硬件接口

第一轮不要同时加入 FreeRTOS。先用裸机把射频链路跑通。

### 4.1 备份

1. 复制整个工程目录，或初始化 Git 并提交当前可编译版本。
2. 保存当前 Keil Build Log。
3. 记录 Keil 工程中的 EdgeImpulse 文件组、IncludePath、`EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=0` 等编译宏。

### 4.2 Pinout

1. 打开 `LoRaWAN.ioc`。
2. 在 `Connectivity > SPI1` 选择 `Full-Duplex Master`。
3. 确认 PA5/PA6/PA7 分别成为 SPI1_SCK/MISO/MOSI。
4. PA4 设为 GPIO_Output，标签 `LoRa_CS`。
5. PA2 设为 GPIO_Output，标签 `LoRa_RST`。
6. PA1 设为 GPIO_Input，标签 `LoRa_BUSY`。
7. 取消 PA0 当前的 `SYS_WKUP`，改为 `GPIO_EXTI0`，标签 `LoRa_DIO1`，触发选 Rising Edge。
8. PC4 设为 GPIO_Output，标签 `LoRa_TXEN`。
9. PB0 设为 GPIO_Output，标签 `LoRa_RXEN`。
10. 保留 I2C1 PB6/PB7、USART1 PA9/PA10 和 SWD。

### 4.3 SPI1 参数

| 参数 | 值 |
|---|---|
| Mode | Full-Duplex Master |
| Frame Format | Motorola |
| Data Size | 8 bit |
| First Bit | MSB First |
| Clock Polarity | Low |
| Clock Phase | 1 Edge |
| NSS | Software |
| Baud Prescaler | 16 |

当前 APB2 为 84 MHz，分频 16 得到 5.25 MHz，适合首次联调。SX1268 SPI 上限为 16 MHz，不要使用分频 4；分频 8 的 10.5 MHz虽可用，但首版没有必要。

### 4.4 GPIO 参数

- `LoRa_CS`：Output Push Pull、No pull、初始 High。
- `LoRa_RST`：推荐 Output Open Drain、Pull-up 或外部上拉、初始释放 High。
- `LoRa_BUSY`：Input、No pull。
- `LoRa_DIO1`：External Interrupt Rising、No pull。
- `LoRa_TXEN`、`LoRa_RXEN`：Output Push Pull、No pull、初始 Low。
- EXTI0 中断首版可用优先级 6。

### 4.5 生成与回归检查

1. Generate Code。
2. 打开 Keil，确认 APP/EdgeImpulse 文件组和 IncludePath 未丢失。
3. 确认生成了 `spi.c/.h`，`stm32f4xx_hal_conf.h` 启用了 SPI。
4. 全量 Rebuild，要求 0 error。
5. 不接天线、不发射，只验证原有传感器、OLED、TinyML 基线是否仍能运行。

## 5. LoRa 裸机 Bring-up

优先移植 Semtech 官方 C 驱动，不从零手写 opcode：

- https://github.com/Lora-net/sx126x_driver

只需实现平台层：

- `sx126x_hal_reset`
- `sx126x_hal_wakeup`
- `sx126x_hal_write`
- `sx126x_hal_read`

硬件访问规则：

1. 每条 SPI 命令前等待 BUSY 变低，并设置超时。
2. NSS 拉低后连续发送完整 opcode、参数和数据，再拉高。
3. 从 Sleep 唤醒后，NSS 拉低到首个 SCK 之间留足唤醒时间，再等待 BUSY 低。
4. NRST 拉低约 100 µs后释放，然后等待 BUSY 低。
5. DIO1 ISR 只置标志；不要在中断中做 SPI、打印或等待。

首轮初始化建议：

1. Hardware reset。
2. 等 BUSY 低。
3. `GetStatus`，连续调用 1000 次零超时作为 SPI 验收。
4. `SetStandby(RC)`。
5. 选择 DC-DC regulator。
6. 禁用 DIO2 自动 RF switch，因为当前 DIO2 未接。
7. 设置合法测试频率。
8. 设置 LoRa packet type、buffer base、modulation parameters 和 packet parameters。
9. 把 TxDone、RxDone、Timeout、CRC Error 映射到 DIO1。

近距离起步参数：

- 发射功率：+10 dBm
- BW：125 kHz
- SF：7
- CR：4/5
- Preamble：8
- Header：Explicit
- CRC：On
- IQ：Normal
- 私有网络 Sync Word：两端完全一致

不要一开始使用 +22 dBm。

验收：

- P1：无射频发射，GetStatus 1000 次无错误。
- P2：一端固定发包、一端固定接收，近距离 1000 包成功率不低于 99%，日志包含 seq、RSSI、SNR。
- P3：加入 ACK、最多 3 次重传、重复包去重和随机退避。

## 6. 私有协议建议

基础帧：

```text
magic | version | type | flags | node_id | session | seq |
uptime | payload_len | payload | auth_tag
```

遥测 payload 使用定点整数，避免发送 JSON：

```text
temperature_x100, humidity_x100, pressure_pa, sraw_voc,
voc_index, predicted_delta_x100, trend, battery_mv, status
```

- DATA 发出后进入短 RX 窗口等待 ACK。
- ACK 必须匹配 node_id、session 和 seq。
- 网关按 `(node_id, session, seq)` 去重。
- 多节点加入随机抖动、指数退避和 CAD/LBT。
- SX1268 硬件 CRC 只能检测误码，不能认证发送者。部署版建议 AES-CCM-128，并保证 nonce 不复用。

## 7. ESP32 Wi-Fi/MQTT 网关

第二块 E22 接 ESP32 的 SPI。两端频率、BW、SF、CR、Sync Word、Preamble、Header、CRC、IQ 必须完全相同。

建议任务：

- RadioRxTask：常态 RX，验包、去重，立即发送无线 ACK。
- MqttTask：维护 Wi-Fi/MQTT，把二进制遥测转换为 JSON 后发布。
- StorageTask：Wi-Fi 断开时用 NVS/Flash 环形队列缓存，恢复后补发。

网关建议市电/USB 常供电，不进入睡眠，否则会漏收节点上行。

MQTT：

- Topic：`aq/<node_id>/telemetry`
- 使用 MQTT over TLS。
- 无线 ACK 表示网关收到；云端成功需要单独的 cloud-ack 状态，不能混为一谈。

ESP-MQTT 官方文档：

- https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/protocols/mqtt.html

## 8. FreeRTOS 迁移

LoRa 裸机通信稳定后再启用 FreeRTOS/CMSIS-RTOS v2。

建议结构：

- SensorTask：唯一 I2C 所有者，固定 1 Hz 采集并将不可变 Sample 投递到队列。
- MlTask：消费 10 点窗口，运行 Edge Impulse，生成 Prediction。
- RadioTask：唯一 SPI/E22 所有者，处理 TX、ACK、RX、超时和退避。
- LoggerTask：最低优先级，仅开发期启用。
- OLED：仅开发期开启，功耗测试时关闭。

DIO1 EXTI ISR 只调用 task notification/semaphore 的 FromISR 版本。若 FreeRTOS 的 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 为 5，则调用 FreeRTOS API 的中断数字优先级必须大于等于 5，例如 6。

迁移后：

- 删除 SysTick_Handler 中手工累加 `sample_tick` 的调度。
- 用 `osDelayUntil`/`vTaskDelayUntil` 保持 1 Hz 节拍。
- HAL timebase 改为 TIM6/TIM7，SysTick 留给 FreeRTOS。
- 所有队列、任务栈和超时都要统计；进行 24–72 小时稳定性测试。

## 9. 低功耗的现实边界

RTOS 本身不会自动降低功耗。正确顺序是：

1. 所有任务阻塞。
2. 启用 FreeRTOS tickless idle。
3. 没有 I2C heater、Radio、UART、OLED 等 power lock 时进入 STOP。
4. 由 RTC 或 DIO1 唤醒。
5. STOP 唤醒后恢复 PLL、系统时钟和相关外设。

但当前硬件无法达到原设想的平均 `<200 µA`：

- STM32F407 STOP 低功耗调节器典型值约 0.28–0.31 mA，已高于 200 µA。
- SGP40 在 3.3 V 连续工作平均约 2.6 mA。
- VOC Index 算法典型要求每 1 秒采样，当前 TinyML 也要求 1 Hz、10 点窗口。
- OLED、开发板稳压器、LED、ST-Link 还会继续增加功耗。

SGP40 官方资料：

- https://sensirion.com/products/catalog/SGP40
- https://sensirion.com/media/documents/296373BB/6203C5DF/Sensirion_Gas_Sensors_Datasheet_SGP40.pdf

可选路线：

- 保持连续 VOC/预测：接受整机平均电流为数 mA，先以 `<5 mA` 为工程目标并实测。
- 每 60 秒唤醒后连续采集 10 秒：可以降低平均功耗，但 SGP40 预热/基线和模型数据分布要重新验证。
- 真正追求 `<200 µA`：改用 STM32L/U/WL，关断 OLED/LED/ST-Link，使用低静态电流电源与传感器电源门控，并按新的采样节奏重新训练模型。
- F407 Standby + RTC 可做到更低，但唤醒近似复位，不能保持 FreeRTOS 上下文，需要保存状态并重新启动。

## 10. 中国大陆频段合规

模块能调到某个频率，不代表可以用 +22 dBm 任意发射。

- 433.05–434.79 MHz 的相关微功率条目主要面向通用无线遥控，功率和发射时长受限，环境遥测不应自行套用。
- 470–510 MHz 的 50 mW e.r.p 条目限民用计量场景，并要求发射前搜寻、单信道、单次发射不超过 1 秒等条件。
- E22 的 +22 dBm 约为 158 mW conducted，天线增益还要计入 e.r.p，明显不能当默认部署功率。

实验室先使用低功率、短距离和合适的屏蔽/假负载条件。现场部署前，应按实际用途向当地无线电管理机构或检测机构确认频率、功率、带宽、天线和占空限制。

工信部 2019 年第 52 号公告：

- https://www.miit.gov.cn/zwgk/zcwj/wjfb/gg/art/2020/art_4f2c890530cc4837b2113cf977e249c4.html

## 11. 总体验收顺序

1. **P0 基线**：修正 TinyML 特征与 40 点窗口，连续运行 24 小时，记录推理时延和正确性。
2. **P1 SPI**：GetStatus 1000 次零错误，无射频发射。
3. **P2 P2P**：近距离 1000 包成功率 ≥99%，记录 RSSI/SNR/seq。
4. **P3 协议**：注入丢 ACK、重复包和节点重启；最多 3 次重传，网关不重复上报。
5. **P4 网关**：Wi-Fi/MQTT 断开 10 分钟不丢数据，恢复后自动补发。
6. **P5 RTOS**：连续 24–72 小时无死锁/HardFault，任务栈有余量，队列不溢出。
7. **P6 功耗**：分别测 RUN、STOP、LoRa RX/TX、SGP40、OLED；断开调试器和 LED 后积分测量。
8. **P7 系统**：测试断网、断电、brownout、网关重启、节点重启和长期运行。

