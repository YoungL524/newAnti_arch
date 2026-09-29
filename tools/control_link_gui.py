"""! @file control_link_gui.py
@brief 用标准库窗口发送 DP/SP 心跳和单目标航迹，并观察 DCMP 发出的 0x0001 与 0xFFFF 报文。
@details 本工具位于设备侧联试边界：向 DCMP 内部 UDP 入口发送心跳，分别在 2345/1234 监听两种控制包；
不发送 ACK、不模拟设备起控、不访问数据库。模拟航迹只发给已绑定采集会话的 1 号阵面。
新人从 ControlLinkGui.start()、poll() 和 decode_control() 阅读交互及协议校验。
"""

import socket
import struct
import time
import tkinter as tk
import math
from datetime import datetime
from tkinter import messagebox, ttk


MAGIC = 0xA1A11A1A
TAIL = 0xB1B2A1A2
FRAME_SIZE = 944
PAYLOAD_OFFSET = 44
CONTROL_PORT = 2345
WORKMODE_PORT = 1234
TRACK_FRAME_SIZE = 656
TRACK_OFFSET = PAYLOAD_OFFSET + 384


def decode_control(packet: bytes) -> tuple[int, int, int]:
    """! @brief 校验两种控制报文，返回类型、版本和控制值；非法报文抛出 ValueError。"""
    if len(packet) < 48:
        raise ValueError("报文短于统一公共头尾")
    magic, length, kind, version = struct.unpack_from("<4I", packet)
    expected_size = FRAME_SIZE if kind == 1 else 68 if kind == 0xFFFF else 0
    if len(packet) != expected_size:
        raise ValueError(f"0x{kind:04X} 报文长度 {len(packet)}，期望 {expected_size}")
    tail, = struct.unpack_from("<I", packet, expected_size - 4)
    if magic != MAGIC or length != expected_size - 12 or tail != TAIL:
        raise ValueError("包头、声明长度或包尾不符合统一协议")
    if version != 20260915:
        raise ValueError(f"协议版本 {version}，期望 20260915")
    control_offset = PAYLOAD_OFFSET + 32 if kind == 1 else PAYLOAD_OFFSET
    control, = struct.unpack_from("<I", packet, control_offset)
    if control not in (0, 1):
        raise ValueError(f"控制值={control}，期望 0 或 1")
    if kind == 0xFFFF and any(packet[PAYLOAD_OFFSET + 4:PAYLOAD_OFFSET + 20]):
        raise ValueError("0xFFFF 备份区必须全零")
    return kind, version, control


def heartbeat(kind: int) -> bytes:
    """! @brief 生成 DP 或 SP 在线心跳；只声明模拟的 1 号阵面在线和有数据。"""
    packet = bytearray(132)
    struct.pack_into("<4I", packet, 0, MAGIC, 120, kind, 20260915)
    struct.pack_into("<2H", packet, PAYLOAD_OFFSET, 1, 0x01)
    struct.pack_into("<I", packet, 128, TAIL)
    return bytes(packet)


def track_packet(sequence: int, pack_flag: int = 1) -> bytes:
    """! @brief 构造 1 号阵面、目标 1 的 0x0100 完整包；角度为度，包计数逐包递增。"""
    packet = bytearray(TRACK_FRAME_SIZE)
    struct.pack_into("<4I", packet, 0, MAGIC, TRACK_FRAME_SIZE - 12, 0x0100, 20260915)
    struct.pack_into("<3I", packet, PAYLOAD_OFFSET + 4, sequence, 0, sequence)
    struct.pack_into("<i", packet, TRACK_OFFSET, 1)
    range_m = 2000.0 + 250.0 * math.sin(sequence * 0.04)
    azimuth_deg = 18.0 * math.sin(sequence * 0.025)
    struct.pack_into("<5f", packet, TRACK_OFFSET + 20,
                     range_m, azimuth_deg, 8.0, 12.0, 35.0)
    struct.pack_into("<I", packet, TRACK_OFFSET + 116, pack_flag)
    struct.pack_into("<I", packet, TRACK_FRAME_SIZE - 4, TAIL)
    return bytes(packet)


class ControlLinkGui:
    """! @brief 单线程拥有 UDP 套接字和 Tk 窗口，使用 after 定时接收控制包并发送心跳、航迹。"""

    def __init__(self, root: tk.Tk) -> None:
        """! @brief 建立监听和心跳配置控件；套接字在点击开始前不创建。"""
        self.root = root
        self.root.title("DCMP 控制链路联试")
        self.root.geometry("780x480")
        self.control_socket: socket.socket | None = None
        self.workmode_socket: socket.socket | None = None
        self.send_socket: socket.socket | None = None
        self.target: tuple[str, int] | None = None
        self.poll_job: str | None = None
        self.last_heartbeat = 0.0
        self.received = 0
        self.invalid = 0
        self.heartbeats_sent = 0
        self.tracks_sent = 0
        self.track_sequence = 0
        self.track_enabled = False
        self.last_track = 0.0
        self.dcmp_ip = tk.StringVar(value="127.0.0.1")
        self.dcmp_port = tk.StringVar(value="7788")
        self.dp_enabled = tk.BooleanVar(value=True)
        self.sp_enabled = tk.BooleanVar(value=True)
        self.state = tk.StringVar(value="未监听")
        self.counts = tk.StringVar(value="控制有效 0  |  无效 0  |  心跳已发送 0  |  航迹已发送 0")

        config = ttk.LabelFrame(root, text="0x0001 监听 127.0.0.1:2345；0xFFFF 监听 127.0.0.1:1234", padding=10)
        config.pack(fill="x", padx=12, pady=10)
        for column, (label, variable, width) in enumerate((
            ("DCMP IP", self.dcmp_ip, 18), ("DCMP 端口", self.dcmp_port, 8),
        )):
            ttk.Label(config, text=label).grid(row=0, column=column * 2, padx=3)
            ttk.Entry(config, textvariable=variable, width=width).grid(row=0, column=column * 2 + 1, padx=3)
        ttk.Checkbutton(config, text="发送 DP 心跳", variable=self.dp_enabled).grid(row=1, column=0, columnspan=2, sticky="w", pady=8)
        ttk.Checkbutton(config, text="发送 SP 心跳", variable=self.sp_enabled).grid(row=1, column=2, columnspan=2, sticky="w", pady=8)
        self.start_button = ttk.Button(config, text="开始监听", command=self.start)
        self.start_button.grid(row=1, column=5)
        self.stop_button = ttk.Button(config, text="停止", command=self.stop, state="disabled")
        self.stop_button.grid(row=1, column=7)
        self.track_button = ttk.Button(config, text="开始模拟航迹", command=self.toggle_track, state="disabled")
        self.track_button.grid(row=2, column=0, columnspan=2, sticky="w", pady=4)

        ttk.Label(root, textvariable=self.state, font=("Microsoft YaHei", 15, "bold")).pack(anchor="w", padx=16)
        ttk.Label(root, textvariable=self.counts).pack(anchor="w", padx=16, pady=4)
        self.log = tk.Text(root, state="disabled", height=17)
        self.log.pack(fill="both", expand=True, padx=12, pady=(4, 12))
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def write_log(self, message: str) -> None:
        """! @brief 追加有上限的接收记录，避免 50 ms 周期报文无限占用窗口内存。"""
        self.log.configure(state="normal")
        self.log.insert("end", f"{datetime.now():%H:%M:%S.%f}"[:-3] + f"  {message}\n")
        if int(self.log.index("end-1c").split(".")[0]) > 300:
            self.log.delete("1.0", "101.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def start(self) -> None:
        """! @brief 校验心跳目标后绑定两个固定控制端口；任一绑定失败时关闭全部已创建套接字。"""
        try:
            socket.inet_aton(self.dcmp_ip.get())
            dcmp_port = int(self.dcmp_port.get())
            if not 1 <= dcmp_port <= 65535:
                raise ValueError("端口必须在 1～65535 范围内")
            self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.control_socket.bind(("127.0.0.1", CONTROL_PORT))
            self.control_socket.setblocking(False)
            self.workmode_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.workmode_socket.bind(("127.0.0.1", WORKMODE_PORT))
            self.workmode_socket.setblocking(False)
            self.send_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.target = (self.dcmp_ip.get(), dcmp_port)
        except (OSError, ValueError) as error:
            self.stop()
            messagebox.showerror("启动失败", str(error))
            return
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.track_button.configure(state="normal")
        self.state.set("监听中，等待 0x0001 或 0xFFFF 报文")
        self.write_log(f"监听 127.0.0.1:{CONTROL_PORT}/{WORKMODE_PORT}；心跳目标 {self.dcmp_ip.get()}:{dcmp_port}")
        self.last_heartbeat = 0.0
        self.poll()

    def poll(self) -> None:
        """! @brief 在 GUI 事件循环中分别读取两个控制端口；每个端口每轮最多处理 100 包并每秒发送心跳。"""
        self.poll_job = None
        if self.control_socket is None or self.workmode_socket is None or self.send_socket is None or self.target is None:
            return
        try:
            for receiver in (self.control_socket, self.workmode_socket):
                for _ in range(100):
                    try:
                        packet, sender = receiver.recvfrom(65535)
                    except BlockingIOError:
                        break
                    try:
                        kind, version, control = decode_control(packet)
                    except ValueError as error:
                        self.invalid += 1
                        self.write_log(f"无效报文 {sender[0]}:{sender[1]}：{error}")
                        continue
                    self.received += 1
                    if kind == 0xFFFF:
                        label = "开始工作" if control == 1 else "待机"
                    else:
                        label = "周期控制"
                    self.state.set(f"最近控制：{label}")
                    field = "workStandbyMode" if kind == 0xFFFF else "F_UI_STBY_Ctrl"
                    self.write_log(f"0x{kind:04X} {label}  {field}={control}  协议版本={version}  来源={sender[0]}:{sender[1]}")
            now = time.monotonic()
            if now - self.last_heartbeat >= 1.0:
                if self.dp_enabled.get():
                    self.send_socket.sendto(heartbeat(0x0101), self.target)
                    self.heartbeats_sent += 1
                if self.sp_enabled.get():
                    self.send_socket.sendto(heartbeat(0x0303), self.target)
                    self.heartbeats_sent += 1
                self.last_heartbeat = now
            if self.track_enabled and now - self.last_track >= 0.2:
                self.track_sequence += 1
                self.send_socket.sendto(track_packet(self.track_sequence), self.target)
                self.tracks_sent += 1
                self.last_track = now
            self.counts.set(f"控制有效 {self.received}  |  无效 {self.invalid}  |  心跳已发送 {self.heartbeats_sent}  |  航迹已发送 {self.tracks_sent}")
        except OSError as error:
            self.write_log(f"UDP 错误：{error}")
            self.stop()
            return
        self.poll_job = self.root.after(20, self.poll)

    def toggle_track(self) -> None:
        """! @brief 显式启停单目标模拟流；停止时提交一包 packFlag=0 供显示端移除航迹。"""
        if self.send_socket is None or self.target is None:
            return
        self.track_enabled = not self.track_enabled
        if self.track_enabled:
            self.last_track = 0.0
            self.track_button.configure(text="停止模拟航迹")
            self.write_log("开始发送 1 号阵面、目标 ID 1 的模拟航迹；请先在 DCMP 开始采集")
        else:
            self.track_button.configure(text="开始模拟航迹")
            try:
                self.track_sequence += 1
                self.send_socket.sendto(track_packet(self.track_sequence, 0), self.target)
            except OSError as error:
                self.write_log(f"航迹消亡包发送失败：{error}")
            self.write_log("停止模拟航迹并提交目标消亡包")

    def stop(self) -> None:
        """! @brief 停止模拟流并释放本工具套接字；已发过的目标先提交一次消亡包。"""
        if self.poll_job is not None:
            self.root.after_cancel(self.poll_job)
            self.poll_job = None
        if self.track_enabled and self.send_socket is not None and self.target is not None:
            try:
                self.track_sequence += 1
                self.send_socket.sendto(track_packet(self.track_sequence, 0), self.target)
            except OSError as error:
                self.write_log(f"航迹消亡包发送失败：{error}")
        for item in (self.control_socket, self.workmode_socket, self.send_socket):
            if item is not None:
                item.close()
        self.control_socket = None
        self.workmode_socket = None
        self.send_socket = None
        self.target = None
        self.track_enabled = False
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        self.track_button.configure(state="disabled", text="开始模拟航迹")
        self.state.set("已停止监听")

    def close(self) -> None:
        """! @brief 退出窗口前释放 UDP 端口。"""
        self.stop()
        self.root.destroy()


def main() -> None:
    """! @brief 启动控制链路联试窗口；无需第三方 Python 包。"""
    root = tk.Tk()
    ControlLinkGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
