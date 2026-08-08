# STM32 环境数据采集

此目录用于保存新的、带有精确污染事件时间点的数据。固件以 1 Hz 输出传感器数据，
并通过串口单字符命令记录污染源何时放入、移除和恢复洁净。

## 固件采集模式

`APP/data_logger.h` 中的 `DATA_LOGGER_COLLECTION_MODE` 当前为 `1`。在该模式下：

- 运行 BME680、SGP40、OLED 和 Data Logger；
- 暂停 LoRa 测试任务和旧 TinyML 推理任务；
- `main.c` 保持不变，仍然只负责启动和运行 `scheduler`。

完成新模型部署后，将该宏改为 `0` 即可恢复 LoRa 与 TinyML 的正常调度。

## 明日采集步骤

1. 烧录当前工程，连接 USART1（115200、8N1）。
2. 上电后保持环境洁净，至少等待 60 秒预热。
3. 使用串口工具发送 `S`，开始一个新 session。
4. 洁净基线建议持续 5 分钟。
5. 放入污染源的同一时刻发送 `P`。
6. 保持污染源一段时间；取走的同一时刻发送 `R`。
7. 通风恢复后可发送 `C`，标记重新进入洁净阶段。
8. 完成后发送 `E`，看到 `session_end` 后再停止串口保存。

命令可以重复使用：每次新的污染源放入都发送一次 `P`；固件会自动增加
`event_id`。发送 `H` 或 `?` 可再次显示帮助。

## 为最终模型保留独立 session

不要再把一次很长的洁净记录和一次很长的污染记录随机切窗后，同时放入训练集与
测试集。相邻窗口过于相似，会让验证分数虚高，而换一次采集就明显下降。

- 每次按 `S` 到 `E` 视为一个独立 session；
- 明天先完成 4～6 个 session，后续最好在不同日期再补充；
- 每个 session 都要包含独立的洁净基线、放入、移除和恢复过程；
- 若最终仍要预测未来 30 分钟，最后一次状态变化后至少继续记录 35 分钟；
- 以后按完整 session 划分训练/验证/测试，保留至少 20% 的 session 只做测试。

这样得到的测试结果才接近设备面对新环境时的真实效果。

## 推荐的电脑端捕获方式

先安装一次依赖：

```powershell
python -m pip install -r ML/collection/requirements.txt
```

然后将 `COM5` 替换为实际串口：

```powershell
python ML/collection/capture_serial.py --port COM5
```

脚本会实时显示串口数据，并把不可替代的原始记录保存到
`ML/collection/raw/`。在脚本窗口直接按 `S/P/R/C/E` 即可向 STM32
发送事件命令。

## 采集后清洗

假设原始文件名为 `capture_20260731_090000.txt`：

```powershell
python ML/collection/prepare_serial_capture.py `
  ML/collection/raw/capture_20260731_090000.txt
```

脚本会在 `ML/collection/processed/<原始文件名>/` 下输出：

- `session_XXX_sensor_all.csv`：保留所有行，便于审计；
- `session_XXX_sensor_valid.csv`：只保留两个传感器均有效的行；
- `session_XXX_events.csv`：精确的放入、移除、恢复与结束事件。

原始串口文件是不可再生成的数据，完成采集后不要删除；先复制备份，再进行模型
标签生成和 Edge Impulse 上传。
