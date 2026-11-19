# 首次安装与烧录

## 1. 准备硬件

采集端使用 STM32F407VET6、BME680、SGP40、I²C OLED、E22-400M22S（SX1268）；网关使用 ESP32、同频 LoRa 模块和 I²C OLED。节点 STOP 使用板载 32.768 kHz LSE 晶振；没有晶振时，固件检测失败会留在浅睡路径。

| 节点接口 | STM32 引脚 |
|---|---|
| LoRa SPI1 SCK / MISO / MOSI | PA5 / PA6 / PA7 |
| LoRa NSS / NRST / BUSY / DIO1 | PA4 / PA2 / PA1 / PA0 |
| LoRa TXEN / RXEN | PC4 / PB0 |
| I²C1 SCL / SDA | PB6 / PB7 |
| USART1 TX / RX | PA9 / PA10 |

接线前按自己板卡的原理图核对供电和引脚，信号连接需共地。天空星板载 KEY 与 LoRa DIO1 共用 PA0，当前接法下不把它当省电按键。两端须使用匹配的射频参数和天线，并遵守当地无线电规定。网关接线见[ESP32 网关说明](../ESP32_Gateway/README.md)。

## 2. 构建 STM32

- 使用 Keil MDK、Arm Compiler 6.23、`Keil.STM32F4xx_DFP.3.0.0` 和 STM32CubeF4 固件包 V1.28.3。
- 将 STM32CubeF4 SDK 放在源码根目录，目录名为 `STM32Cube_FW_F4_V1.28.3/`，与 `MDK-ARM/` 同级。
- 打开 `MDK-ARM/LoRaWAN.uvprojx`，选择 `LoRaWAN` 目标。按实物配置下载器，执行 **Rebuild**，确认无错误后 **Download**。需要独立下载工具时，使用本次构建生成的 `MDK-ARM/LoRaWAN/LoRaWAN_Adapt_Keil.hex`，不要用目录中的旧实验镜像。
- 退出调试，复位或重新上电运行。以提供的 Keil 工程为准，不需要用 CubeMX 重生成代码。

## 3. 构建 ESP32

使用 ESP-IDF 5.5.5（[Espressif Windows 安装与烧录说明](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/get-started/windows-setup.html)），在 ESP-IDF 终端从源码根目录执行：

```powershell
cd ESP32_Gateway
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

全新工程若未选目标，先执行 `idf.py set-target esp32`，再配置；已有配置不必重复设置。把 `COMx` 换成 ESP32 实际串口。烧录前核对目标芯片和分区；只更新既有且分区一致的网关应用时，可以用 `app-flash`。

仅使用 LoRa 接收、本地显示和串口监视时，无需填写 Wi-Fi、OneNET 或邮箱。需要联网时，在 `menuconfig` 的「空气环境 LoRa 网关配置」填写 Wi-Fi；需要 OneNET 时，再填写产品 ID、设备名、Access Key 和有效的 Token 到期时间。邮件默认关闭，另见[邮件配置与测试](ALARM_MAIL_CN.md)。


## 4. 验收

1. 给两端上电，确认网关持续收到 LoRa 数据，接收序号递增。
2. 用 [Windows 监视器](VOC_MONITOR_WINDOWS_CN.md)连接网关串口，确认 VOC 读数持续更新。节点通常开机约 65 分半后出现首个有效 AI 分数，缺测时可能更晚。ESP-IDF monitor 与 Windows 监视器不能同时占用同一串口。
3. 如启用了邮件，按[邮件说明](ALARM_MAIL_CN.md)发送测试邮件；如需验证 STOP 唤醒，按[低功耗说明](LOW_POWER_PROFILE_CN.md)检查 `LPSTOP` 日志。
