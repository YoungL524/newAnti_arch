"""! @file control_link_gui.py
@brief 用标准库窗口发送 DP/SP 心跳和 CSV 航迹，并观察 DCMP 发出的 0x0001 与 0xFFFF 报文。
@details 本工具位于设备侧联试边界：向 DCMP 内部 UDP 入口发送含逐面经纬高与姿态的 DP 心跳，
并发送原布局 SP 心跳；分别在 2345/1234 监听两种控制包。
不发送 ACK、不模拟设备起控、不访问数据库。模拟航迹只发给已绑定采集会话的 1 号阵面。
新人从 ControlLinkGui.start()、poll() 和 decode_control() 阅读交互及协议校验。
"""

import socket
import csv
import struct
import time
import tkinter as tk
import math
from datetime import datetime
from pathlib import Path
from tkinter import messagebox, ttk


MAGIC = 0xA1A11A1A
TAIL = 0xB1B2A1A2
FRAME_SIZE = 944
PAYLOAD_OFFSET = 44
CONTROL_PORT = 2345
WORKMODE_PORT = 1234
TRACK_FRAME_SIZE = 656
TRACK_OFFSET = PAYLOAD_OFFSET + 384
## @brief CSV 固定取脚本同目录文件，不依赖启动命令的工作目录。
CSV_PATH = Path(__file__).resolve().with_name("target_data.csv")
## @brief 固定每 200 ms 处理最多 32 条 CSV 观测，与文件历史时间完全无关，不补发积压批次。
CSV_BATCH_INTERVAL_SECONDS = 0.2

## @brief 协议结构以 core/protocol/DcmpPayloads.h 为准；每个载荷分组恰好包含 32 个小端字。
COMMON_FIELDS = (
    "F_UI_Combi_Mode F_UI_FrameWork_Mode F_UI_SysWork_Mode F_UI_SysRange_Mode "
    "F_UI_STC_Switch F_UI_STC_Mode F_UI_Exc_Switch F_UI_CFAR_Mode_Ctrl "
    "F_UI_STBY_Ctrl F_UI_CFAR_Ctrl_1 F_UI_CFAR_Ctrl_2 F_UI_Vel_Ctrl "
    "F_UI_AD_Rec_Switch F_UI_Dot_Rec_Switch F_UI_TracDot_Ctrl "
    "F_UI_DotMerge_Rec_Switch F_UI_DotCorr_Rec_Switch"
).split() + [f"uni_bak2[{index}]" for index in range(15)]
FRAME_FIELDS = (
    "F_UI_ScreenWave_Mod_Ctrl F_UI_ScreenTrack_Enable "
    "F_DP_ScreenTrack_Azim_1 F_DP_ScreenTrack_Elev_1 "
    "F_DP_ScreenTrack_Azim_2 F_DP_ScreenTrack_Elev_2 "
    "F_DP_ScreenTrack_Azim_3 F_DP_ScreenTrack_Elev_3 "
    "F_DP_ScreenTrack_Azim_4 F_DP_ScreenTrack_Elev_4 "
    "F_UIDP_Beam_Azim_Ctrl F_UIDP_Beam_Elev_Ctrl F_UIDP_Band_Switch "
    "F_UIDP_BIT_Switch_Ctrl F_UIDP_FrameWave_Ctrl F_UIDP_Modul_Ctrl "
    "F_UIDP_ModulBw_Ctrl F_UIDP_Pw_Ctrl F_UIDP_Tr_Ctrl F_UIDP_FrameWait_Ctrl "
    "F_UIDP_EffTrN_Ctrl F_UIDP_SampR0_Ctrl F_UIDP_SampRn_Ctrl "
    "F_UIDP_FrameWaveWrite_Ctrl F_UIDP_WritePos_Ctrl"
).split() + [f"uni_bak3[{index}]" for index in range(7)]
NAVIGATION_FIELDS = (
    "F_FL_Platform_GPS_Long F_FL_Platform_GPS_Lati F_FL_Platform_Height "
    "F_FL_Platform_HeadingAngl F_FL_Platform_ElAngl F_FL_Platform_RolAngl "
    "F_FL_Platform_heading_AnglVel F_FL_Platform_El_AnglVel "
    "F_FL_Platform_Rol_AnglVel F_FL_Platform_Vel_East "
    "F_FL_Platform_Vel_North F_FL_Platform_Vel_Up "
    "F_FL_AntRef_X_Vel F_FL_AntRef_Y_Vel F_FL_AntRef_Z_Vel "
    "F_FL_Platform_Vel_Ground F_FL_Platform_Vel_Acc F_FL_Platform_YawAngl"
).split() + [f"uni_bak4[{index}]" for index in range(14)]
FACE_FIELDS = (
    "F_UI_Ant_Idx F_UI_FreBand_Pick F_UI_Freq_Ctrl F_UI_Recv_Gain_Ctrl "
    "F_UI_Antenna_Mode F_UI_Power_Mode F_UI_RF_Reset "
    "F_DP_C1 F_DP_C2 F_DP_C3 F_DP_C4 F_DP_C5 F_DP_C6 "
    "F_UI_AntCourse_Angle F_UI_AntPitch_Angle F_UI_AntRoll_Angle"
).split() + [f"F_UI_Antenna_bak[{index}]" for index in range(16)]
HEADER_FIELDS = (
    "F_UI_Package_Head F_UI_TotalLength F_UI_MessageType F_UI_Protocol_Version "
    "F_UI_GPS_Time_DMY F_UI_GPS_Time_SMH F_UI_SYS_Time_DMY F_UI_SYS_Time_SMH "
    "F_UI_DCMP_Version F_UI_DCMP_Time_DMY F_UI_DCMP_Time_SMH"
).split()


def control_field_rows(packet: bytes) -> list[tuple[str, str, int, str]]:
    """! @brief 从已校验报文按协议偏移提取全部字段，保留原始字并附上已知工程含义。"""
    kind, _, _ = decode_control(packet)
    rows: list[tuple[str, str, int, str]] = []

    def append_group(group: str, names: list[str], offset: int) -> None:
        """! @brief 按固定四字节步长读取一个协议分组；每个备份字也单独显示。"""
        for index, name in enumerate(names):
            absolute = offset + index * 4
            raw, = struct.unpack_from("<I", packet, absolute)
            signed, = struct.unpack_from("<i", packet, absolute)
            meaning = ""
            if name in ("F_UI_CFAR_Ctrl_1", "F_UI_CFAR_Ctrl_2"):
                meaning = f"{signed} dB"
            elif name == "F_UI_Vel_Ctrl":
                meaning = f"{signed} m/s"
            elif name in ("F_UIDP_Beam_Azim_Ctrl", "F_UIDP_Beam_Elev_Ctrl",
                          "F_UI_AntCourse_Angle", "F_UI_AntPitch_Angle", "F_UI_AntRoll_Angle"):
                meaning = f"{signed / 100:g}°"
            elif name in ("F_FL_Platform_GPS_Long", "F_FL_Platform_GPS_Lati"):
                meaning = f"{raw / 1000000:g}°"
            elif group == "导航" and 2 <= index <= 17:
                meaning = f"{signed / 100:g}"
            elif name == "F_UI_Recv_Gain_Ctrl":
                meaning = f"RF {raw & 31} dB / IF {(raw >> 5) & 31} dB"
            elif name == "F_UI_Power_Mode":
                meaning = f"风扇1 {'关' if raw & 4 else '开'} / 风扇2 {'关' if raw & 2 else '开'} / TR {'关' if raw & 1 else '开'}"
            elif name in ("F_UI_STBY_Ctrl", "workStandbyMode"):
                meaning = "工作" if raw == 1 else "待机"
            rows.append((group, name, absolute, f"{raw} (0x{raw:08X})" + (f"  →  {meaning}" if meaning else "")))

    append_group("公共头", HEADER_FIELDS, 0)
    if kind == 1:
        append_group("通用控制", COMMON_FIELDS, PAYLOAD_OFFSET)
        append_group("帧控制", FRAME_FIELDS, PAYLOAD_OFFSET + 128)
        append_group("导航", NAVIGATION_FIELDS, PAYLOAD_OFFSET + 256)
        for face in range(4):
            append_group(f"{face + 1}# 阵面", FACE_FIELDS, PAYLOAD_OFFSET + 384 + face * 128)
    else:
        append_group("工作/待机", ["workStandbyMode"] + [f"reserved[{index}]" for index in range(4)], PAYLOAD_OFFSET)
    append_group("公共尾", ["packetTail"], len(packet) - 4)
    return rows


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


def heartbeat(kind: int, face_mask: int = 1,
              face_poses: tuple[tuple[float, ...], ...] = ((118.65303, 31.93494, 63.9288, 0, 0, 0),) * 4) -> bytes:
    """! @brief 生成 DP 152 字节 POS2 心跳或 SP 132 字节心跳；逐面位置只由在线位决定有效性。"""
    if face_mask < 0 or face_mask > 15 or len(face_poses) != 4:
        raise ValueError("阵面位图或逐面位姿数量无效")
    frame_size = 152 if kind == 0x0101 else 132
    packet = bytearray(frame_size)
    struct.pack_into("<4I", packet, 0, MAGIC, frame_size - 12, kind, 20260915)
    struct.pack_into("<2H", packet, PAYLOAD_OFFSET, 1, face_mask)
    if kind == 0x0101:
        for face, values in enumerate(face_poses):
            if len(values) != 6:
                raise ValueError(f"{face + 1}# 阵面位姿字段数量无效")
            if (face_mask & (1 << face)) == 0:
                continue
            if not all(map(math.isfinite, values)):
                raise ValueError(f"{face + 1}# 阵面位姿无效")
            longitude, latitude, altitude, heading, pitch, roll = values
            if not -180 <= longitude <= 180 or not -90 <= latitude <= 90 \
                    or not -1000 <= altitude <= 10000:
                raise ValueError(f"{face + 1}# 阵面经纬高超范围")
            if not -180 <= heading <= 360:
                raise ValueError(f"{face + 1}# 阵面航向超范围")
            normalized_heading = (heading + 180) % 360 - 180
            if not -90 <= pitch <= 90 or not -90 <= roll <= 90:
                raise ValueError(f"{face + 1}# 阵面俯仰或横滚超范围")
            struct.pack_into("<6i", packet, PAYLOAD_OFFSET + 4 + face * 24,
                             round(longitude * 1000000), round(latitude * 1000000), round(altitude * 100),
                             round(normalized_heading * 100), round(pitch * 100), round(roll * 100))
        struct.pack_into("<I", packet, PAYLOAD_OFFSET + 100, 0x32534F50)
    struct.pack_into("<I", packet, frame_size - 4, TAIL)
    return bytes(packet)


def track_packet(sequence: int, row: dict[str, str]) -> bytes:
    """! @brief 将 CSV 的 ID、极坐标、径向 speed、原始 RCS 和 packFlag 映射到 1# 阵面 656 字节包。
    @param sequence 本工具逐包递增的 uint32 计数，与 CSV Time 无关。
    @param row CSV 原始行；角度为度、距离为米、speed 为抵近正的 m/s，RCS 仅作为未标定幅值原值。
    @return 现行 0x0100 报文；未建立语义的字段及备份区保持零，不用 Lon/Lat/Hgt 替换雷达极坐标。
    @throws ValueError 必填字段缺失、数值非法或观测超出 Core 的方位 ±45°、俯仰 0～45° 时拒绝。
    """
    try:
        target_id = int(row["ID"])
        pack_flag = int(row["packFlag"])
        values = tuple(float(row[field]) for field in ("Range", "Azimuth", "Elevation", "speed", "RCS"))
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("CSV 航迹字段缺失或不是有效数值") from error
    if not 0 <= target_id <= 0x7FFFFFFF or pack_flag not in (0, 1):
        raise ValueError("CSV ID 或 packFlag 超范围")
    if not all(math.isfinite(value) and abs(value) <= 3.402823466e38 for value in values):
        raise ValueError("CSV 航迹包含非有限数或超出 float32 范围")
    if values[0] < 0 or not -45 <= values[1] <= 45 or not 0 <= values[2] <= 45:
        raise ValueError("CSV 观测超出现行接收扇区")
    packet = bytearray(TRACK_FRAME_SIZE)
    struct.pack_into("<4I", packet, 0, MAGIC, TRACK_FRAME_SIZE - 12, 0x0100, 20260915)
    struct.pack_into("<3I", packet, PAYLOAD_OFFSET + 4, sequence, 0, sequence)
    struct.pack_into("<i", packet, TRACK_OFFSET, target_id)
    struct.pack_into("<5f", packet, TRACK_OFFSET + 20, *values)
    struct.pack_into("<I", packet, TRACK_OFFSET + 116, pack_flag)
    struct.pack_into("<I", packet, TRACK_FRAME_SIZE - 4, TAIL)
    return bytes(packet)


def retirement_packet(sequence: int, last_packet: bytes) -> bytes:
    """! @brief 复用目标最近有效坐标生成消亡包，避免 CSV 消亡点越界导致 Core 拒绝删除。
    @param sequence 本工具新分配的 uint32 包计数。
    @param last_packet 同一目标先前成功提交的 656 字节 0x0100 报文。
    @return 仅更新包计数、景幅计数和 packFlag=0 的临时报文，不修改保存的原包。
    """
    packet = bytearray(last_packet)
    struct.pack_into("<I", packet, PAYLOAD_OFFSET + 4, sequence)
    struct.pack_into("<I", packet, PAYLOAD_OFFSET + 12, sequence)
    struct.pack_into("<I", packet, TRACK_OFFSET + 116, 0)
    return bytes(packet)


class ControlLinkGui:
    """! @brief 单线程拥有 UDP 套接字和 Tk 窗口，使用 after 定时接收控制包并发送心跳、航迹。"""

    def __init__(self, root: tk.Tk) -> None:
        """! @brief 建立监听和心跳配置控件；套接字在点击开始前不创建。"""
        self.root = root
        self.root.title("DCMP 控制链路联试")
        self.root.geometry("1060x860")
        self.control_socket: socket.socket | None = None
        self.workmode_socket: socket.socket | None = None
        self.send_socket: socket.socket | None = None
        self.target: tuple[str, int] | None = None
        self.poll_job: str | None = None
        self.last_heartbeat = 0.0
        self.received = 0
        self.invalid = 0
        self.heartbeats_sent = 0
        self.heartbeat_last_sent = {0x0101: "尚未提交", 0x0303: "尚未提交"}  ##< 分别记录本地 sendto 成功时间，不代表 Core 收到。
        self.tracks_sent = 0
        self.track_sequence = 0
        self.track_enabled = False
        self.track_file = None  ##< 绕过全文件预载，仅在 CSV 发送期间拥有打开的文本文件。
        self.track_reader = None  ##< 当前 CSV 迭代器，与文件同时释放。
        self.last_csv_batch = 0.0  ##< 最近一轮 CSV 处理的 monotonic 秒；与文件时间无关。
        self.csv_skipped = 0  ##< 本轮非法或超扇区记录数，不伪造修改后的观测。
        self.active_tracks: dict[int, bytes] = {}  ##< 每个当前目标只留最近有效包，供消亡时复用。
        self.dcmp_ip = tk.StringVar(value="127.0.0.1")
        self.dcmp_port = tk.StringVar(value="7788")
        self.dp_enabled = tk.BooleanVar(value=True)
        self.sp_enabled = tk.BooleanVar(value=True)
        self.face_enabled = [tk.BooleanVar(value=True) for _ in range(4)]
        self.face_pose_values = [
            [tk.StringVar(value=value) for value in face]
            for face in (("118.65303", "31.93494", "63.9288", "0", "0", "0"),
                         ("118.65303", "31.93494", "63.9288", "90", "0", "0"),
                         ("118.65303", "31.93494", "63.9288", "180", "0", "0"),
                         ("118.65303", "31.93494", "63.9288", "270", "0", "0"))
        ]
        self.state = tk.StringVar(value="未监听")
        self.counts = tk.StringVar(value="控制有效 0  |  无效 0  |  心跳已发送 0  |  航迹已发送 0")
        self.event = tk.StringVar(value="等待监听")
        self.csv_progress = tk.StringVar(value="CSV 未开始")
        self.heartbeat_submission = tk.StringVar(value="DP/SP 心跳尚未提交")
        self.packet_state = {1: tk.StringVar(value="尚未接收 0x0001"),
                             0xFFFF: tk.StringVar(value="尚未接收 0xFFFF")}
        self.trees: dict[int, ttk.Treeview] = {}
        self.tree_rows: dict[int, dict[tuple[str, str], str]] = {1: {}, 0xFFFF: {}}

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
        self.track_button = ttk.Button(config, text="发送 CSV 航迹", command=self.toggle_track, state="disabled")
        self.track_button.grid(row=2, column=0, columnspan=2, sticky="w", pady=4)

        pose_frame = ttk.LabelFrame(root, text="DP 心跳实时位姿（逐面发送；DCMP 平均在线阵面的经纬高）", padding=8)
        pose_frame.pack(fill="x", padx=12, pady=(0, 5))
        for column, label in enumerate(("阵面在线", "经度 °", "纬度 °", "海拔 m", "航向 °", "俯仰 °", "横滚 °")):
            ttk.Label(pose_frame, text=label).grid(row=0, column=column, padx=4, pady=(7, 2))
        for face in range(4):
            ttk.Checkbutton(pose_frame, text=f"{face + 1}#", variable=self.face_enabled[face]).grid(
                row=face + 1, column=0, sticky="w")
            for field in range(6):
                ttk.Entry(pose_frame, textvariable=self.face_pose_values[face][field], width=13).grid(
                    row=face + 1, column=field + 1, padx=4, pady=1)

        ttk.Label(root, textvariable=self.state, font=("Microsoft YaHei", 15, "bold")).pack(anchor="w", padx=16)
        ttk.Label(root, textvariable=self.counts).pack(anchor="w", padx=16, pady=4)
        ttk.Label(root, textvariable=self.event).pack(anchor="w", padx=16)
        ttk.Label(root, textvariable=self.csv_progress).pack(anchor="w", padx=16)
        ttk.Label(root, textvariable=self.heartbeat_submission).pack(anchor="w", padx=16)
        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True, padx=12, pady=(4, 12))
        for kind, title in ((1, "0x0001 控制参数"), (0xFFFF, "0xFFFF 工作/待机")):
            page = ttk.Frame(notebook)
            notebook.add(page, text=title)
            ttk.Label(page, textvariable=self.packet_state[kind]).pack(anchor="w", padx=5, pady=5)
            table_frame = ttk.Frame(page)
            table_frame.pack(fill="both", expand=True)
            tree = ttk.Treeview(table_frame, columns=("offset", "value"), show="tree headings")
            tree.heading("#0", text="协议字段")
            tree.heading("offset", text="报文偏移")
            tree.heading("value", text="实际值 / 工程含义")
            tree.column("#0", width=330, stretch=False)
            tree.column("offset", width=90, stretch=False, anchor="center")
            tree.column("value", width=520, stretch=True)
            vertical = ttk.Scrollbar(table_frame, orient="vertical", command=tree.yview)
            horizontal = ttk.Scrollbar(table_frame, orient="horizontal", command=tree.xview)
            tree.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
            tree.grid(row=0, column=0, sticky="nsew")
            vertical.grid(row=0, column=1, sticky="ns")
            horizontal.grid(row=1, column=0, sticky="ew")
            table_frame.rowconfigure(0, weight=1)
            table_frame.columnconfigure(0, weight=1)
            self.trees[kind] = tree
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def write_log(self, message: str) -> None:
        """! @brief 只保留最近一条联试事件，周期控制值由字段表持续覆盖显示。"""
        self.event.set(f"{datetime.now():%H:%M:%S}  {message}")

    def show_packet(self, kind: int, packet: bytes, sender: tuple[str, int]) -> None:
        """! @brief 按协议分组更新字段表；重复字段只改值，保留用户展开和滚动位置。"""
        tree = self.trees[kind]
        known = self.tree_rows[kind]
        groups: dict[str, str] = {}
        for group, name, offset, value in control_field_rows(packet):
            group_id = groups.get(group)
            if group_id is None:
                group_id = f"{kind}:{group}"
                groups[group] = group_id
                if not tree.exists(group_id):
                    tree.insert("", "end", iid=group_id, text=group, open=group != "公共头")
            key = (group, name)
            row_id = known.get(key)
            if row_id is None:
                row_id = f"{group_id}:{name}"
                tree.insert(group_id, "end", iid=row_id, text=name,
                            values=(offset, value))
                known[key] = row_id
            elif tree.set(row_id, "value") != value:
                tree.set(row_id, "value", value)
        self.packet_state[kind].set(
            f"最近接收 {datetime.now():%H:%M:%S.%f}"[:-3] +
            f"  来源 {sender[0]}:{sender[1]}  长度 {len(packet)} 字节")

    def heartbeat_pose(self) -> tuple[int, tuple[tuple[float, ...], ...]]:
        """! @brief 从 GUI 读取逐面位姿；非法文本由发送轮次提示，不让 Tk 回调崩溃。"""
        try:
            face_poses = tuple(tuple(float(value.get()) for value in face) for face in self.face_pose_values)
        except ValueError as error:
            raise ValueError("心跳位姿请输入数值") from error
        face_mask = sum(1 << face for face, enabled in enumerate(self.face_enabled) if enabled.get())
        return face_mask, face_poses

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
            self.send_socket.setblocking(False)
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

    def send_heartbeats(self, now: float) -> None:
        """! @brief 优先发送到期心跳，DP 位姿或单路发送失败不会跳过 SP，错误不关闭整个工具。
        @param now 本轮 monotonic 秒；每秒最多尝试一轮，不追补 GUI 停顿期间的历史心跳。
        @note 仍由 Tk 单线程调度，操作系统暂停进程或 Tk 模态阻塞时不承诺持续发送。
        """
        if now - self.last_heartbeat < 1.0:
            return
        if self.last_heartbeat and now - self.last_heartbeat >= 2.0:
            self.write_log(f"工具心跳调度间隔 {now - self.last_heartbeat:.2f}s，检查窗口阻塞或进程暂停")
        self.last_heartbeat = now
        face_mask = sum(1 << face for face, enabled in enumerate(self.face_enabled) if enabled.get())
        for kind, enabled, label in ((0x0101, self.dp_enabled, "DP"), (0x0303, self.sp_enabled, "SP")):
            if not enabled.get():
                continue
            try:
                packet = heartbeat(kind, face_mask, self.heartbeat_pose()[1]) if kind == 0x0101 else heartbeat(kind, face_mask)
                self.send_socket.sendto(packet, self.target)
                self.heartbeats_sent += 1
                self.heartbeat_last_sent[kind] = f"{datetime.now():%H:%M:%S.%f}"[:-3]
            except (ValueError, OSError) as error:
                self.write_log(f"{label} 心跳未发送：{error}")
        self.heartbeat_submission.set(
            f"心跳最近本地提交：DP {self.heartbeat_last_sent[0x0101]} | SP {self.heartbeat_last_sent[0x0303]} | 目标 {self.target[0]}:{self.target[1]}")

    def send_csv_tracks(self, now: float) -> None:
        """! @brief 每 200 ms 按文件顺序处理最多 32 条观测，完全忽略 CSV 时间，不补发历史批次。
        @param now 本轮 monotonic 秒，仅用于工具自身的固定发送节奏。
        @note 无效行跳过并计数，消亡优先复用同目标最近有效包；文件结束自动收口本工具剩余目标。
        """
        if not self.track_enabled:
            return
        if now - self.last_csv_batch < CSV_BATCH_INTERVAL_SECONDS:
            return
        self.last_csv_batch = now
        try:
            for _ in range(32):
                row = next(self.track_reader, None)
                if row is None:
                    self.stop_tracks("CSV 发送完成")
                    return
                try:
                    target_id = int(row["ID"])
                    pack_flag = int(row["packFlag"])
                    sequence = (self.track_sequence + 1) & 0xFFFFFFFF
                    previous = self.active_tracks.get(target_id)
                    packet = retirement_packet(sequence, previous) if pack_flag == 0 and previous else track_packet(sequence, row)
                except (KeyError, TypeError, ValueError) as error:
                    self.csv_skipped += 1
                    self.write_log(f"CSV 第 {self.track_reader.line_num} 行跳过：{error}")
                    continue
                self.send_socket.sendto(packet, self.target)
                self.track_sequence = sequence
                self.tracks_sent += 1
                if pack_flag == 0:
                    self.active_tracks.pop(target_id, None)
                else:
                    self.active_tracks[target_id] = packet
            self.csv_progress.set(f"CSV 运行中：已读取第 {self.track_reader.line_num} 行，下一轮继续")
        except (OSError, csv.Error, UnicodeError) as error:
            self.stop_tracks(f"CSV 发送中止：{error}")

    def poll(self) -> None:
        """! @brief 心跳先于接收和 CSV 执行；每端口限 100 包，仅刷新本轮每种控制的最后一包。
        @note finally 保证非关停回调仍重新调度；报文校验或字段显示异常不会永久断掉 after 链。
        """
        self.poll_job = None
        if self.control_socket is None or self.workmode_socket is None or self.send_socket is None or self.target is None:
            return
        try:
            self.send_heartbeats(time.monotonic())
            for receiver in (self.control_socket, self.workmode_socket):
                latest = {}
                for _ in range(100):
                    try:
                        packet, sender = receiver.recvfrom(65535)
                    except BlockingIOError:
                        break
                    try:
                        kind, _, control = decode_control(packet)
                    except ValueError as error:
                        self.invalid += 1
                        self.write_log(f"无效报文 {sender[0]}:{sender[1]}：{error}")
                        continue
                    self.received += 1
                    latest[kind] = (packet, sender, control)
                for kind, (packet, sender, control) in latest.items():
                    if kind == 0xFFFF:
                        label = "开始工作" if control == 1 else "待机"
                    else:
                        label = "周期控制"
                    self.state.set(f"最近控制：{label}")
                    self.show_packet(kind, packet, sender)
            self.send_csv_tracks(time.monotonic())
            self.counts.set(f"控制有效 {self.received}  |  无效 {self.invalid}  |  心跳已发送 {self.heartbeats_sent}  |  航迹已发送 {self.tracks_sent}  |  CSV跳过 {self.csv_skipped}")
        except (ValueError, OSError, tk.TclError) as error:
            self.write_log(f"本轮联试异常：{error}")
        finally:
            if self.send_socket is not None:
                self.poll_job = self.root.after(20, self.poll)

    def toggle_track(self) -> None:
        """! @brief 从脚本同目录的 target_data.csv 开始发送；手动停止只结束航迹，不停止 DP/SP 心跳。"""
        if self.send_socket is None or self.target is None:
            return
        if self.track_enabled:
            self.stop_tracks("手动停止 CSV 航迹")
            return
        if not self.face_enabled[0].get():
            self.write_log("模拟航迹固定来自 1# 阵面，请先勾选 1# 在线")
            return
        try:
            self.track_file = CSV_PATH.open(encoding="utf-8-sig", newline="")
            self.track_reader = csv.DictReader(self.track_file)
            required = {"ID", "Range", "Azimuth", "Elevation", "speed", "RCS", "packFlag"}
            if not required.issubset(self.track_reader.fieldnames or ()):
                raise ValueError("CSV 缺少必需列：" + ", ".join(sorted(required - set(self.track_reader.fieldnames or ()))))
        except (OSError, ValueError, csv.Error, UnicodeError) as error:
            self.stop_tracks(f"无法读取 {CSV_PATH}：{error}")
            return
        self.csv_skipped = 0
        self.last_csv_batch = 0.0
        self.track_enabled = True
        self.csv_progress.set("CSV 运行中：每 200 ms 最多处理 32 条，忽略文件时间")
        self.track_button.configure(text="停止 CSV 航迹")
        self.write_log("开始按文件顺序发送 target_data.csv，固定 1# 阵面；请先在 DCMP 开始采集")

    def stop_tracks(self, reason: str) -> None:
        """! @brief 关闭 CSV 文件并对仍活动的各目标提交一次消亡包；逐包失败计数，不关闭心跳套接字。
        @param reason 手动停止、EOF 或 CSV 失败原因；事件明确区分本地提交成功和失败，不声明设备已收到。
        """
        self.track_enabled = False
        if self.track_file is not None:
            self.track_file.close()
        self.track_file = None
        self.track_reader = None
        failed = 0
        for packet in self.active_tracks.values():
            try:
                sequence = (self.track_sequence + 1) & 0xFFFFFFFF
                self.send_socket.sendto(retirement_packet(sequence, packet), self.target)
                self.track_sequence = sequence
                self.tracks_sent += 1
            except OSError:
                failed += 1
        self.active_tracks.clear()
        self.track_button.configure(text="发送 CSV 航迹")
        self.csv_progress.set(f"CSV 已停止：{reason}")
        self.write_log(f"{reason}；跳过 {self.csv_skipped} 行，剩余目标消亡提交失败 {failed} 包")

    def stop(self) -> None:
        """! @brief 停止模拟流并释放本工具套接字；已发过的目标先提交一次消亡包。"""
        if self.poll_job is not None:
            self.root.after_cancel(self.poll_job)
            self.poll_job = None
        self.stop_tracks("停止监听，心跳与 CSV 航迹均停止")
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
        self.track_button.configure(state="disabled", text="发送 CSV 航迹")
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
