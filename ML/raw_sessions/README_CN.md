# 原始采集数据目录

本目录用于在本地保存不可替代的串口原始记录。出于体积和隐私考虑，CSV 文件不纳入 Git；仓库只保留本说明。请自行备份原始记录，数据准备脚本不会修改它们。

## 文件命名约定

```text
session1.csv ... session8.csv   # 含污染源放入、移除和恢复过程
normal1.csv  ... normal3.csv    # 无污染源的连续正常环境记录
```

需要增加数据时可以继续使用 `session9.csv`、`normal4.csv` 等名称，但必须同时更新 `ML/prepare_edge_impulse_sessions_v2.py` 的源文件清单，或按脚本当前约定调整文件名。

旧的回归实验数据仅用于历史对照，约定放在 `ML/archive/legacy_regression_data/`，不参与当前 `normal` / `warning` 分类模型。

## 推荐串口字段

CSV 表头应为：

```text
record_type,session_id,timestamp_ms,uptime_ms,event_id,event_state,event_name,temperature,humidity,pressure,sraw_voc,voc_index,bme680_valid,sgp40_valid
```

- 采样频率：1 Hz；时间列单位为毫秒。
- 每次上电先完成传感器预热，当前采集程序参考预热时间为 60 秒。
- `timestamp_ms` 应在单个文件中单调递增；少量按键手速误差可由事件标记反映，但不要手工伪造传感器数值。
- 只保留 `bme680_valid=1`、`sgp40_valid=1` 且字段完整的有效传感器记录用于建模。
- 推荐保留原始表头。脚本可以为已知的无表头旧记录生成 canonical 副本，但新的采集不应依赖该兼容行为。

采集器命令约定：

```text
S = start
P = source_placed
R = source_removed
C = clean
E = end
H = help
```

一次污染 session 应包含稳定基线、放入污染源、保持、移除和恢复过程。正常 session 应来自无污染源的连续环境，并尽量覆盖空调房、客厅、不同温湿度和不同时段。不要让同一次 session 的相邻窗口跨入训练集和测试集两边。

## 本地再生成 Edge Impulse 数据

在仓库根目录执行：

```powershell
python -m pip install numpy pandas
python ML/prepare_edge_impulse_sessions_v2.py
```

可上传文件会生成到：

```text
ML/edge_impulse_warning_state_v2/training/
ML/edge_impulse_warning_state_v2/testing/
```

质量摘要和样本来源会生成到：

```text
ML/edge_impulse_warning_state_v2/source_summary.csv
ML/edge_impulse_warning_state_v2/manifest.csv
```

以上生成 CSV 同样不纳入 Git。详细标签规则、上传参数和训练参数见 `ML/edge_impulse_warning_state_v2/README_CN.md`。

## 隐私与安全

- 原始数据只需记录建模所需的环境与事件字段，不要加入姓名、住址、Wi-Fi 信息或 OneNET 凭据。
- 对外分享前移除绝对时间、精确位置、设备序列号和可能识别个人活动的备注。
- 原始文件至少保留一份离线备份；清洗和生成操作只针对副本或脚本输出目录。
- 人工污染实验应使用低风险气味源、保持通风，并避免有毒、可燃或高浓度挥发物。
