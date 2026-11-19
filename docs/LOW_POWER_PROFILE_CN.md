# 节点低功耗

正式 Keil 目标采用 84 MHz、`APP_POWER_PROFILE=1`、`APP_OLED_AUTO=1`、`APP_STOP_ENABLE=1`。ESP32 网关不进入此省电模式。

- **OLED**：开机、首个有效 AI 判定或新 AI 事件可点亮约 30 秒；熄屏后停止刷新，采样、推理和 LoRa 不停止。
- **CPU**：传感器转换时让当前任务等待，空闲且外设无事务、LoRa 已睡眠时尝试短时 STOP。由 LSE（32.768 kHz 晶振）驱动的 RTC 定时唤醒，恢复工作时钟并补偿系统时基。不改变每秒采样或约五秒上报。

## 怎么确认

退出 Keil 调试并在断电状态下拔掉下载器，用电池单独上电。接串口收日志时不并接适配器电源；纯电池端测量应移除所有可能反向供电的连接。约每 10 秒出现一条：

```text
LPSTOP,1,uptime_ms,state,entries,rtc_wakes,early_wakes,bypasses,elapsed_ms,deadline_overruns,fault_reason*CRC
```

- `state=2` 只是 LSE/RTC 唤醒中断自检通过；还要看 `entries` 和 `rtc_wakes` 都在增加，才说明真的进入 STOP 并唤醒。
- `state=3` 为浅睡回退，`fault_reason=3` 表示 LSE 未就绪；`state=4` 为故障锁定。正常验证记录的 `deadline_overruns`、`fault_reason` 均为 0。
- 检查采样序号连续、网关持续收到数据。正式功能验收还要等到 AI 出分，通常开机约 65 分半。

若要做同条件 A/B 对照，在 Keil 中只改变 `APP_STOP_ENABLE`（1 开、0 关），每次完整重编译。保持时钟、外设、OLED 状态、采样和发送频率不变。操作前保存当前固件并核对要烧录的版本。

## 实板记录

2026-11-18 电池供电启动日志：约 62 秒时累计进入 STOP 362 次、RTC 唤醒 362 次，累计休眠约 53.55 秒；记录的 70 次每秒采样连续，无截止时间超限。2026-11-19 使用数字万用表在电池端观察：

| 节点状态 | 最低 | 常见 | 最高表显 |
|---|---:|---:|---:|
| 启动后熄屏 | 6.82 mA | 约 6.87 mA | 35.52 mA |
| AI 就绪、熄屏，网关仍更新 | 6.77 mA | 约 6.80 mA | 45.68 mA |

万用表读数不是纯休眠电流或周期平均电流，不足以确认真实射频发射峰值，也不能用休眠时间占比推算整板节能比例。测法见[功耗测量](POWER_MEASUREMENT_CN.md)。
