"""! @file control_link_gui.py
@brief 用标准库窗口收发 DP/SP 心跳并观察 DCMP 发出的 0x0001 控制报文。
@details 本工具位于设备侧联试边界：向 DCMP 内部 UDP 入口发送心跳，在配置的 DPH 地址监听控制包；
不发送 ACK、不模拟设备起控、不访问数据库。先在 DCMP 设备台账把 DPH 目标设为本窗口监听地址和端口。
新人从 ControlLinkGui.start()、poll() 和 decode_control() 阅读交互及协议校验。
"""

import socket
import struct
import time
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, ttk


MAGIC = 0xA1A11A1A
TAIL = 0xB1B2A1A2
FRAME_SIZE = 944
PAYLOAD_OFFSET = 44


def decode_control(packet: bytes) -> tuple[int, int]:
    """! @brief 严格校验公共封装，返回版本和采集控制字；非法报文抛出 ValueError。"""
    if len(packet) != FRAME_SIZE:
        raise ValueError(f"报文长度 {len(packet)}，期望 {FRAME_SIZE}")
    magic, length, kind, version = struct.unpack_from("<4I", packet)
    tail, = struct.unpack_from("<I", packet, FRAME_SIZE - 4)
    if magic != MAGIC or length != FRAME_SIZE - 12 or kind != 1 or tail != TAIL:
        raise ValueError("包头、声明长度、类型或包尾不符合 0x0001 协议")
    if version != 20260915:
        raise ValueError(f"协议版本 {version}，期望 20260915")
    control, = struct.unpack_from("<I", packet, PAYLOAD_OFFSET + 32)
    if control not in (0, 1):
        raise ValueError(f"F_UI_STBY_Ctrl={control}，期望 0 或 1")
    return version, control


def heartbeat(kind: int) -> bytes:
    """! @brief 生成 DP 或 SP 在线心跳，四阵面在线位图为 0x0F；不构造航迹。"""
    packet = bytearray(132)
    struct.pack_into("<4I", packet, 0, MAGIC, 120, kind, 20260915)
    struct.pack_into("<2H", packet, PAYLOAD_OFFSET, 1, 0x0F)
    struct.pack_into("<I", packet, 128, TAIL)
    return bytes(packet)


class ControlLinkGui:
    """! @brief 单线程拥有 UDP 套接字和 Tk 窗口，使用 after 定时检查报文及发送心跳。"""

    def __init__(self, root: tk.Tk) -> None:
        """! @brief 建立监听和心跳配置控件；套接字在点击开始前不创建。"""
        self.root = root
        self.root.title("DCMP 控制链路联试")
        self.root.geometry("780x480")
        self.listen_socket: socket.socket | None = None
        self.send_socket: socket.socket | None = None
        self.target: tuple[str, int] | None = None
        self.poll_job: str | None = None
        self.last_heartbeat = 0.0
        self.received = 0
        self.invalid = 0
        self.listen_ip = tk.StringVar(value="127.0.0.1")
        self.listen_port = tk.StringVar(value="7790")
        self.dcmp_ip = tk.StringVar(value="127.0.0.1")
        self.dcmp_port = tk.StringVar(value="7788")
        self.dp_enabled = tk.BooleanVar(value=True)
        self.sp_enabled = tk.BooleanVar(value=True)
        self.state = tk.StringVar(value="未监听")
        self.counts = tk.StringVar(value="有效 0  |  无效 0")

        config = ttk.LabelFrame(root, text="联试地址（DPH 目标须与监听地址一致）", padding=10)
        config.pack(fill="x", padx=12, pady=10)
        for column, (label, variable, width) in enumerate((
            ("监听 IP", self.listen_ip, 18), ("监听端口", self.listen_port, 8),
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
        """! @brief 校验 IPv4 和端口后独占绑定 DPH；失败时关闭已创建的套接字。"""
        try:
            socket.inet_aton(self.listen_ip.get())
            socket.inet_aton(self.dcmp_ip.get())
            listen_port = int(self.listen_port.get())
            dcmp_port = int(self.dcmp_port.get())
            if not (1 <= listen_port <= 65535 and 1 <= dcmp_port <= 65535):
                raise ValueError("端口必须在 1～65535 范围内")
            self.listen_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.listen_socket.bind((self.listen_ip.get(), listen_port))
            self.listen_socket.setblocking(False)
            self.send_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.target = (self.dcmp_ip.get(), dcmp_port)
        except (OSError, ValueError) as error:
            self.stop()
            messagebox.showerror("启动失败", str(error))
            return
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.state.set("监听中，等待 0x0001 控制报文")
        self.write_log(f"监听 {self.listen_ip.get()}:{listen_port}；心跳目标 {self.dcmp_ip.get()}:{dcmp_port}")
        self.last_heartbeat = 0.0
        self.poll()

    def poll(self) -> None:
        """! @brief 在 GUI 事件循环中非阻塞收包；一轮最多处理 100 包并每秒发送在线心跳。"""
        self.poll_job = None
        if self.listen_socket is None or self.send_socket is None or self.target is None:
            return
        try:
            for _ in range(100):
                try:
                    packet, sender = self.listen_socket.recvfrom(65535)
                except BlockingIOError:
                    break
                try:
                    version, control = decode_control(packet)
                except ValueError as error:
                    self.invalid += 1
                    self.write_log(f"无效报文 {sender[0]}:{sender[1]}：{error}")
                    continue
                self.received += 1
                label = "启动/保持采集" if control == 1 else "待机/停止采集"
                self.state.set(f"最近控制：{label}")
                self.write_log(f"{label}  F_UI_STBY_Ctrl={control}  协议版本={version}  来源={sender[0]}:{sender[1]}")
            self.counts.set(f"有效 {self.received}  |  无效 {self.invalid}")
            now = time.monotonic()
            if now - self.last_heartbeat >= 1.0:
                if self.dp_enabled.get():
                    self.send_socket.sendto(heartbeat(0x0101), self.target)
                if self.sp_enabled.get():
                    self.send_socket.sendto(heartbeat(0x0303), self.target)
                self.last_heartbeat = now
        except OSError as error:
            self.write_log(f"UDP 错误：{error}")
            self.stop()
            return
        self.poll_job = self.root.after(20, self.poll)

    def stop(self) -> None:
        """! @brief 关闭本工具拥有的套接字；保留屏幕上的接收证据。"""
        if self.poll_job is not None:
            self.root.after_cancel(self.poll_job)
            self.poll_job = None
        for item in (self.listen_socket, self.send_socket):
            if item is not None:
                item.close()
        self.listen_socket = None
        self.send_socket = None
        self.target = None
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
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
