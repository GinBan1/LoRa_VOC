# VOC 现场监测 · Windows 版

适用于 Windows 10/11 64 位。此工具连接 **ESP32 网关的 USB 串口**，显示 STM32 上报的 VOC 指数、板端 AI 分数和两路判定状态。

## 使用方法

1. 将下载的 ZIP **全部解压**到普通文件夹。不要在压缩包的预览窗口内直接启动，也不要把 `VOC-Monitor.exe` 单独移出；同级 `_internal` 文件夹是它的运行组件。
2. 双击 `VOC-Monitor.exe`，无须先安装 Python 或打开终端。不接设备时，点窗口中的「演示」查看模拟曲线；演示不会占用串口，也不会写入实测 CSV。
3. 连接 ESP32 网关后，关闭占用同一串口的串口助手或监视器，再点「刷新串口」，选对应 COM 口，点「连接 / 断开」。默认 115200。列表里找不到板子时，检查是否使用数据线、系统能否识别 USB 串口及其驱动。
4. 有真实有效帧时，程序保存 CSV；可用窗口中的「打开记录文件夹」查看。Windows 版默认目录为 `%LOCALAPPDATA%\VOC Live\CSV`。预热未完成时没有 AI 分数，不代表串口故障。

声明：本工具面向单网关、单节点。VOC 指数曲线不是浓度曲线；AI 分数不是精度或安全等级。

## 从源码重建（仅开发者）

在装有 Tk 支持的 Windows x64 Python 3.12 环境中，从工程根目录运行：

```powershell
python -m pip install 'pyinstaller==6.20.0' 'pyserial==3.5'
python -m PyInstaller --windowed --onedir --name VOC-Monitor tools/voc_live.py
```
