"""Live STM32 VOC results from the ESP32 gateway's USB serial port."""

import argparse
import csv
import os
import re
import sys
import time
from binascii import crc_hqx
from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


STATES = ('预热', '收集窗口', '正常', '疑似事件', '事件', '恢复中', '无效')
COLORS = dict(voc='#0e7490', mean='#2458a6', ref='#667085', ai='#6941c6', rule='#b45309')
LIMIT = 0xffffffff
FIELDS = re.compile(rb'AQ2(?:,-?[0-9]{1,10}){16}\*[0-9A-Fa-f]{4}')
CSV_FIELDS = ('pc_utc', 'segment', 'radio_seq', 'seq_gap', 'node_ms', 'flags',
              'temp_c', 'rh_pct', 'pressure_hpa', 'sraw_voc', 'voc_index', 'rule_state',
              'window_end_ms', 'window_voc_mean', 'rule_episode_id', 'ai_end_ms',
              'ai_state', 'ai_score', 'ai_delta_from_prev60s', 'raw_aq2')


def default_csv_dir():
    if getattr(sys, 'frozen', False):
        # An EXE may be run from a read-only folder or use an unpacked temp directory.
        base = Path(os.environ.get('LOCALAPPDATA') or Path.home())
        return base / 'VOC Live' / 'CSV'
    return Path(__file__).resolve().parents[1] / 'outputs'


@dataclass(frozen=True)
class Frame:
    seq: int
    ms: int
    flags: int
    t: int
    rh: int
    p: int
    sraw: int
    voc: int
    rule: int
    end: int
    mean: int
    episode: int
    ai_end: int
    ai: int
    score: int
    delta: int


def parse_frame(line):
    if FIELDS.fullmatch(line) is None:
        raise ValueError('帧格式错误')
    payload, crc = line.split(b'*')
    if crc_hqx(payload, 0xffff) != int(crc, 16):
        raise ValueError('帧校验失败')
    f = Frame(*map(int, payload.split(b',')[1:]))
    if not (all(0 <= x <= LIMIT for x in (f.seq, f.ms, f.end, f.episode, f.ai_end))
            and 0 <= f.flags <= 7 and 0 <= f.voc <= 500 and 0 <= f.sraw <= 65535
            and -400 <= f.t <= 850 and 0 <= f.rh <= 1000
            and (3000 if f.flags & 1 else 0) <= f.p <= 11000
            and 0 <= f.mean <= 5000 and 0 <= f.rule < len(STATES) and 0 <= f.ai < len(STATES)):
        raise ValueError('字段越界')
    age = (f.ms - f.end) & LIMIT
    ready = bool(f.flags & 4)
    if (ready != (2 <= f.rule <= 5) or (f.end and age > LIMIT // 2)
            or (ready and (age > 15000 or not (f.mean and f.voc and f.sraw) or (f.flags & 3) != 3))
            or (f.rule in (4, 5) and not f.episode)):
        raise ValueError('规则窗口无效')
    if f.ai_end:
        if not (2 <= f.ai <= 5 and f.ai_end == f.end and (f.flags & 3) == 3
                and f.sraw and f.voc and age <= 15000
                and 0 <= f.score <= 1000 and -49900 <= f.delta <= 49900):
            raise ValueError('AI 窗口无效')
    elif f.ai not in (0, 1, 6) or f.score or f.delta:
        raise ValueError('AI 无效帧不应包含分数')
    return f


class LineBuffer:
    """Keep UART fragments across reads and discard entire oversized lines."""

    def __init__(self):
        self.buf = bytearray()
        self.overflow = False

    def feed(self, chunk):
        for byte in chunk:
            if byte == 10:
                if not self.overflow:
                    yield bytes(self.buf).rstrip(b'\r')
                self.buf.clear()
                self.overflow = False
            elif not self.overflow:
                if len(self.buf) == 512:
                    self.buf.clear()
                    self.overflow = True
                else:
                    self.buf.append(byte)


class Stream:
    def __init__(self):
        self.segment = 1
        self.last = None

    def accept(self, f):
        previous = self.last
        if previous is None:
            self.last = f
            return 0, False
        seq_step = (f.seq - previous.seq) & LIMIT
        time_step = (f.ms - previous.ms) & LIMIT
        if 0 < seq_step <= LIMIT // 2 and 0 < time_step <= LIMIT // 2:
            # Preserve sequence continuity over a counter wrap, but start a new
            # plot segment if the raw uptime (the CSV/plot time base) wrapped.
            new_segment = f.ms < previous.ms
            if new_segment:
                self.segment += 1
            self.last = f
            return seq_step - 1, new_segment
        if (f.seq <= 10 and f.ms <= 60000 and f.ms < previous.ms
                and f.seq < previous.seq):
            self.segment += 1
            self.last = f
            return 0, True
        return None


def csv_row(f, segment, gap, raw):
    env = bool(f.flags & 1)
    gas = bool(f.flags & 2) and bool(f.voc)
    return (datetime.now(timezone.utc).isoformat(timespec='milliseconds'), segment, f.seq,
            gap, f.ms, f.flags, f.t / 10 if env else None, f.rh / 10 if env else None,
            f.p / 10 if env else None, f.sraw if gas else None, f.voc if gas else None,
            f.rule, f.end or None, f.mean / 10 if f.end and f.flags & 4 else None, f.episode, f.ai_end or None,
            f.ai, f.score / 1000 if f.ai_end else None, f.delta / 100 if f.ai_end else None,
            raw.decode('ascii'))


def demo_line(i):
    """Synthetic UI preview only; never saved as experimental data."""
    ms = 4201000 + i * 5000
    end = ms // 10000 * 10000
    block = i // 2
    cycle = block % 20
    voc = 90 + max(0, 130 - abs(cycle - 10) * 23)
    rule = 4 if 10 <= cycle <= 12 else (5 if cycle == 13 else 2)
    ai = 4 if 8 <= cycle <= 12 else (5 if cycle == 13 else 2)
    score = min(970, max(60, (voc - 90) * 11))
    values = (i + 1, ms, 7, 253, 456, 10058, 32000, voc, rule, end,
              voc * 10, block // 20 + 1 if rule in (4, 5) else 0,
              end, ai, score, (voc - 90) * 100)
    body = ('AQ2,' + ','.join(map(str, values))).encode('ascii')
    return body + b'*%04X' % crc_hqx(body, 0xffff)


def main():
    import tkinter as tk
    from tkinter import ttk

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', help='ESP32 gateway USB port, e.g. COM8')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--csv-dir', type=Path, default=default_csv_dir())
    ap.add_argument('--demo', action='store_true', help='synthetic UI preview, no COM/CSV')
    args = ap.parse_args()
    if args.demo and args.port:
        ap.error('--demo cannot open a COM port')
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        serial = list_ports = None

    root = tk.Tk()
    root.title('VOC 现场监测 · STM32 端侧 AI')
    root.geometry('1020x730')
    root.minsize(740, 510)
    root.configure(bg='#f1f5f9')
    csv_dir = args.csv_dir.expanduser()
    box = ttk.Frame(root, padding=12)
    box.pack(fill='both', expand=True)
    bar = ttk.Frame(box)
    bar.pack(fill='x')
    ttk.Label(bar, text='VOC 现场监测', font=('Microsoft YaHei UI', 17, 'bold')).pack(side='left')
    port = tk.StringVar(value=args.port or '')
    ports = ttk.Combobox(bar, textvariable=port, width=12)
    ports.pack(side='right', padx=6)
    ttk.Label(bar, text='ESP32 网关串口').pack(side='right')
    connect_hint = '选择 ESP32 网关串口；不要选 STM32 采集端。'
    blank_readings = 'VOC —   |   STM32 AI —   |   固定规则 —'
    status = tk.StringVar(value=connect_hint)
    readings = tk.StringVar(value=blank_readings)
    path = tk.StringVar(value='CSV：尚未开始')
    ttk.Label(box, textvariable=status, foreground='#1d4ed8').pack(anchor='w', pady=(8, 0))
    ttk.Label(box, textvariable=readings, font=('Microsoft YaHei UI', 11, 'bold')).pack(anchor='w', pady=6)
    graph = tk.Canvas(box, bg='white', highlightthickness=1, highlightbackground='#d1d5db')
    graph.pack(fill='both', expand=True)
    footer = ttk.Frame(box)
    footer.pack(fill='x', pady=(6, 0))
    ttk.Label(footer, textvariable=path, wraplength=560, foreground='#45556b').pack(side='left')
    ttk.Label(box, text='横轴：节点上电秒数。AI 分数不是浓度或安全等级；前 60 秒均值仅作参考。',
              foreground='#45556b').pack(anchor='w')

    source = writer = file = None
    buf, stream = LineBuffer(), Stream()
    traces, windows = deque(maxlen=2200), deque(maxlen=1100)
    seen = good = errors = lost = 0
    last_rx, opened = 0.0, 0.0
    initial = time.monotonic()
    demo = False

    def refresh():
        try:
            ports['values'] = () if list_ports is None else tuple(x.device for x in list_ports.comports())
        except OSError as exc:
            status.set(f'无法读取串口列表：{exc}')

    def open_csv_folder():
        try:
            csv_dir.mkdir(parents=True, exist_ok=True)
            os.startfile(str(csv_dir.resolve()))
        except OSError as exc:
            status.set(f'无法打开记录文件夹：{exc}')

    def clear_view():
        nonlocal buf, stream, seen, errors, lost, good, last_rx
        buf, stream = LineBuffer(), Stream()
        traces.clear()
        windows.clear()
        seen = errors = lost = good = 0
        last_rx = 0.0
        readings.set(blank_readings)

    def stop(message='已断开；已写入的 CSV 保留。'):
        nonlocal source, file, writer
        if source is not None:
            try:
                source.close()
            except OSError:
                pass
            finally:
                source = None
        if file is not None:
            try:
                file.close()
            except OSError as exc:
                message = f'{message} CSV 关闭失败：{exc}'
            finally:
                file = writer = None
        status.set(message)

    def connect():
        nonlocal source, file, writer, opened
        if demo:
            return
        if source is not None:
            stop()
            return
        if serial is None:
            status.set('缺少串口组件 pySerial，请使用 Windows 发行包。')
            return
        if not port.get().strip():
            status.set('请先选择或输入 ESP32 的 COM 口。')
            return
        try:
            source = serial.Serial(baudrate=args.baud, timeout=0)
            source.dtr = source.rts = False
            source.port = port.get().strip()
            source.open()
        except (OSError, ValueError) as exc:
            stop(f'无法打开串口：{exc}（若已被占用，先关闭串口助手）')
            return
        try:
            csv_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
            target = csv_dir / f'voc_live_{stamp}.csv'
            file = target.open('x', newline='', encoding='utf-8-sig')
            writer = csv.writer(file)
            writer.writerow(CSV_FIELDS)
            file.flush()
            path.set(f'CSV：{target.resolve()}')
            clear_view()
            opened = time.monotonic()
            status.set('串口已打开，等待带 AI 字段的 AQ2 帧…')
        except (OSError, ValueError) as exc:
            stop(f'无法创建或写入 CSV：{exc}')

    def toggle_demo():
        nonlocal demo, initial
        if source is not None:
            status.set('请先断开串口再进入演示，已记录的 CSV 会保留。')
            return
        demo = not demo
        clear_view()
        initial = time.monotonic()
        state = 'disabled' if demo else 'normal'
        for widget in (ports, connect_button, refresh_button):
            widget.configure(state=state)
        demo_button.configure(text='退出演示' if demo else '演示')
        if demo:
            path.set('CSV：演示不写文件')
            status.set('演示模式 · 合成数据 · 未连接设备 · 不保存 CSV')
        else:
            path.set('CSV：未连接；已有记录保留在记录文件夹')
            status.set(connect_hint)

    def record(line):
        nonlocal last_rx, good, errors, lost
        if not line.startswith(b'AQ2,'):
            return
        try:
            f = parse_frame(line)
        except ValueError:
            errors += 1
            return
        order = stream.accept(f)
        if order is None:
            errors += 1
            return
        gap, new_segment = order
        if new_segment:
            traces.clear()
            windows.clear()
        lost += gap
        good += 1
        last_rx = time.monotonic()
        if writer is not None:
            writer.writerow(csv_row(f, stream.segment, gap, line))
            file.flush()
        v = f.voc if (f.flags & 2) and f.voc else None
        traces.append((f.ms, v, bool(gap)))
        if f.end and (f.flags & 4):
            mean = f.mean / 10
            entry = (f.end, mean, mean - f.delta / 100 if f.ai_end else None,
                     f.score / 1000 if f.ai_end else None, f.rule, f.ai)
            if not windows or f.end > windows[-1][0]:
                windows.append(entry)
            elif f.end == windows[-1][0] and f.ai_end and windows[-1][3] is None:
                windows[-1] = entry
        voc = v if v is not None else '无效'
        sc = f' {f.score / 1000:.3f}' if f.ai_end else ''
        readings.set(f'VOC {voc}   |   STM32 AI：{STATES[f.ai]}{sc}   |   固定规则：{STATES[f.rule]}')

    def draw():
        graph.delete('all')
        w, h = graph.winfo_width(), graph.winfo_height()
        if w < 100 or h < 150:
            return
        left, right = 44, w - 24
        span = 300000
        end = max(span, traces[-1][0] if traces else 0)
        start = end - span
        if traces and traces[-1][0] - traces[0][0] < span:
            start = max(start, traces[0][0] - 8000)
            end = start + span

        def x(ms):
            return left + (right - left) * (ms - start) / span

        def chart(top, bottom, ceiling, ticks, title):
            graph.create_text(left, top - 18, text=title, anchor='w', fill='#203049')
            for tick in ticks:
                y = bottom - tick / ceiling * (bottom - top)
                graph.create_line(left, y, right, y, fill='#e5eaf0')
                graph.create_text(left - 7, y, text=str(tick), anchor='e', fill='#526377')
            for i in range(6):
                grid = left + (right - left) * i / 5
                graph.create_line(grid, top, grid, bottom, fill='#edf0f5')
            return lambda v: bottom - max(0, min(ceiling, v)) / ceiling * (bottom - top)

        def series(points, y, color, max_step):
            prev = None
            for ms, value, gap in points:
                if ms < start or ms > end or value is None:
                    prev = None
                    continue
                xx, yy = x(ms), y(value)
                if prev and not gap and ms - prev[0] <= max_step:
                    graph.create_line(prev[1], prev[2], xx, yy, fill=color, width=2)
                graph.create_oval(xx-2, yy-2, xx+2, yy+2, fill=color, outline='')
                prev = (ms, xx, yy)

        split = h // 2
        y1 = chart(37, split-13, 500, (0, 100, 200, 300, 400, 500), 'VOC Index · 最近 5 分钟')
        y2 = chart(split+40, h-32, 1, (0, 0.25, 0.5, 0.75, 1), '板端 AI 分数 · ◀ AI事件   ▼ 规则事件')
        series(traces, y1, COLORS['voc'], 8500)
        series(((p[0], p[1], False) for p in windows), y1, COLORS['mean'], 11000)
        series(((p[0], p[2], False) for p in windows), y1, COLORS['ref'], 11000)
        series(((p[0], p[3], False) for p in windows), y2, COLORS['ai'], 11000)
        graph.create_line(left, y2(0.5), right, y2(0.5), fill='#c4b5fd', dash=(3, 4))
        prior = None
        for ms, _, _, score, rule, ai in windows:
            consecutive = prior and ms - prior[0] == 10000
            if start <= ms <= end and consecutive:
                xx = x(ms)
                if rule == 4 and prior[1] in (2, 3):
                    graph.create_text(xx, y2(0.91), text='▼', fill=COLORS['rule'])
                if score is not None and ai == 4 and prior[2] in (2, 3):
                    graph.create_text(xx, y2(0.80), text='◀', fill=COLORS['ai'])
                if rule == 2 and prior[1] == 5:
                    graph.create_text(xx, y2(0.91), text='○', fill=COLORS['rule'])
                if score is not None and ai == 2 and prior[2] == 5:
                    graph.create_text(xx, y2(0.80), text='○', fill=COLORS['ai'])
            prior = (ms, rule, ai if score is not None else None)
        for i, (name, key) in enumerate((('VOC 读数', 'voc'), ('10 秒均值', 'mean'), ('前 60 秒参考', 'ref'))):
            graph.create_text(right - i * 114, 18, text=name, anchor='e', fill=COLORS[key])
        for i in range(6):
            graph.create_text(left + (right-left)*i/5, h-16, fill='#526377',
                              text=f'{(start + span*i/5)/1000:.0f}s')

    def tick():
        nonlocal seen
        now = time.monotonic()
        if demo:
            goal = int((now - initial) / 0.55)
            # Do not redraw or backfill thousands of synthetic frames after a PC sleep.
            seen = max(seen, goal - 15)
            while seen <= goal:
                record(demo_line(seen))
                seen += 1
            status.set('演示模式 · 合成数据 · 未连接设备 · 不保存 CSV')
        elif source is not None:
            try:
                incoming = source.read(min(4096, source.in_waiting))
                for line in buf.feed(incoming):
                    record(line)
                if good:
                    state = '收数中' if now - last_rx < 16 else '已超时，显示历史值'
                    status.set(f'{state}   |   会话 {stream.segment}   |   接收 {good}   |   缺帧 {lost}   |   拒收 {errors}')
                elif now - opened > 16:
                    status.set(f'未收到有效实时帧（拒收 {errors}）：检查网关 COM 口和两端固件版本。')
            except (OSError, ValueError) as exc:
                stop(f'记录已停止：{exc}')
        draw()
        root.after(300, tick)

    connect_button = ttk.Button(bar, text='连接 / 断开', command=connect)
    connect_button.pack(side='right', padx=4)
    refresh_button = ttk.Button(bar, text='刷新串口', command=refresh)
    refresh_button.pack(side='right')
    demo_button = ttk.Button(bar, text='演示', command=toggle_demo)
    demo_button.pack(side='right', padx=4)
    if hasattr(os, 'startfile'):
        ttk.Button(footer, text='打开记录文件夹', command=open_csv_folder).pack(side='right')
    refresh()
    if args.port:
        root.after(100, connect)
    if args.demo:
        toggle_demo()

    def close():
        stop()
        root.destroy()

    root.protocol('WM_DELETE_WINDOW', close)
    root.after(30, tick)
    root.mainloop()


if __name__ == '__main__':
    main()
