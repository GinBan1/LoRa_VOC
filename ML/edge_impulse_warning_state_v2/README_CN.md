# Edge Impulse `warning_state` v2 数据集

本目录由 `ML/prepare_edge_impulse_sessions_v2.py` 从 `ML/raw_sessions/` 中的本地原始记录重复生成。原始记录不会被脚本修改。

## Git 数据策略

原始环境记录可能包含时间、场景和环境特征，且 CSV 体积会持续增长，因此原始 CSV 与脚本生成的 CSV 均不纳入 Git。仓库只保留：

- 数据准备脚本；
- 本说明和采集约定；
- STM32 固件源码；
- 当前部署使用的 `EdgeImpulse/` 模型代码与参数。

克隆仓库后，`training/`、`testing/`、`processed_sources/` 可能为空，这是正常现象。请在本地放入自己的原始数据后重新生成。

## 本地生成

原始文件放在：

```text
ML/raw_sessions/session1.csv ... session8.csv
ML/raw_sessions/normal1.csv  ... normal3.csv
```

在仓库根目录执行：

```powershell
python -m pip install numpy pandas
python ML/prepare_edge_impulse_sessions_v2.py
```

脚本输出约定：

```text
ML/edge_impulse_warning_state_v2/
├─ training/           # 上传到 Edge Impulse Training
├─ testing/            # 上传到 Edge Impulse Test
├─ processed_sources/  # 无表头等旧记录的标准化副本
├─ manifest.csv        # 样本来源、时间范围、标签与划分
└─ source_summary.csv  # 各源记录质量和生成样本数
```

这些输出均可再生成，不应提交到 Git。实际样本数取决于本地数据质量；此前本地数据的参考划分为 Training 117 个（normal 57、warning 60），Test 61 个（normal 21、warning 40）。同一 `source_session` 不应同时出现在 Training 和 Test 中。

## 模型语义

- `normal`：污染源不在场、事件状态为 `clean`，并通过严格稳定性检查。
- `warning`：完整 30 秒输入窗口位于 `source_placed` 到 `source_removed` 之间。
- 恢复、漂移、跨事件边界以及存在采样缺口的窗口全部排除。

当前模型判断的是“当前 30 秒窗口是否呈现污染预警状态”，不是预测未来是否会有人放入污染源。若要研究真正的未来趋势预测，需要补充跨日期、跨地点、多污染源和自然发生事件的数据，并重新设计标签。

## `normal` 稳定窗口条件

连续 5 分钟（300 个 1 Hz 采样点）同时满足：

```text
max(sraw_voc) - min(sraw_voc) < 80
abs(voc_index[end] - voc_index[start]) < 5
event_state == clean
```

不满足条件的正常记录仍可留作原始证据，但不会进入严格模型样本。例如，某次本地 `normal3.csv` 因没有合格的连续 5 分钟窗口而未生成训练样本；重新生成后的 `source_summary.csv` 会给出本地数据的实际结果。

## Edge Impulse 上传设置

分别将 `training/` 上传为 **Training**，将 `testing/` 上传为 **Test**。推荐使用文件夹上传，避免平台再次随机拆分已经按 session 隔离的数据。

CSV Wizard：

```text
Time series: Yes
Format: Each row contains a reading, sensor values are columns
Time column: timestamp_ms
Timestamp type: Time elapsed in milliseconds
Label column: warning_state
Value columns: temperature, humidity, pressure, sraw_voc
Metadata column: source_session
Sample length: Unlimited（每个 CSV 已经是一个完整 30 秒样本）
Multiple labels: Use the last value（同一生成样本内标签应一致）
```

Create Impulse：

```text
Frequency: 1 Hz
Window size: 30000 ms
Window increase: 30000 ms
Zero-pad data: Off
Processing block: Raw Data
Learning block: Classification
Normalize features: scikit-learn StandardScaler
```

建议训练参数：

```text
Training cycles: 30
Learning rate: 0.0005
Validation set size: 20%
Profile int8 model: On
Split train/validation set on metadata key: source_session
```

训练完成后同时检查 float32 与 int8 的独立测试集结果。不要只看总准确率；应重点记录 `warning` recall、`normal` 误报率、混淆矩阵，并在新的真实场景中进行盲测。
