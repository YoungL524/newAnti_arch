# DCMP 界面控制参数映射契约

> 状态：V1 逐字段映射已冻结；字段含义和控制字以 [2026-09-23 应用版 Excel](../protocol/★数据协议(四面阵雷达)_应用版（1.0）_20260923.xlsx) 为准，表内协议版本仍为 `20260915`。表格只给出原始控制字、
> 未给出物理值清单的字段，UI 按本合同提供有界控制字输入，不自行发明物理枚举。

## 1. 边界与唯一数据方向

界面不直接构造线协议对象。参数只有一条路径：

```text
React 参数表单
  -> HTTP 参数更新
  -> ApplicationController
  -> DeviceController 校验并替换最新 RadarControlSettings
  -> 50 ms 定时读取最新值
  -> DcmpProtocolAdapter 映射并编码临时 DcmpToDpControlPayload
  -> m_sock2DpSp / 127.0.0.1:2345
```

`RadarControlSettings` 是 DeviceController 内唯一长期运行参数快照；它使用业务语义和值域，不复制包头、备份字或
线协议布局。`DcmpToDpControlPayload` 只在 Adapter 发送边界临时构造，不进入 Topic，也不成为第二份长期状态。
HTTP 请求体同样只是边界内短生命周期 DTO；设备台账仍只保存设备身份、部署参数和端点，不保存正在运行的控制快照。

新增 `RadarControlSettings` 的理由是隔离 UI 业务值与厂家线协议整数布局；它的上游是参数 HTTP 请求，下游只有
DeviceController 和 DcmpProtocolAdapter，生命周期为当前活动任务，任务结束即销毁。若最终没有任何运行参数需要
用户编辑，则删除该类型并由 Adapter 使用固定默认值。

## 2. 参数归属

| 参数类别                   | 真源                                  | 生命周期          | 是否按阵面           |
| ---------------------- | ----------------------------------- | ------------- | --------------- |
| 设备经纬高                  | SQLite 设备 revision                  | 随设备配置持久化      | 否               |
| 阵面安装航向、俯仰、横滚           | SQLite 设备 revision 的阵面项             | 随设备配置持久化      | 是               |
| 本次任务启用阵面               | Task 的 `deviceFaceMasks`            | 当前任务          | 是               |
| STC、CFAR、波形、增益、频点等运行参数 | 任务参数快照；运行时由 DeviceController 持有唯一副本 | 模板可复用，任务快照可追溯 | 按协议字段决定         |
| 采集工作控制                | 开始/结束采集动作                           | 一次性 `0xFFFF` 命令 | `workStandbyMode` 随按钮取 1/0；`0x0001` 字段联动另议 |

整包发送条件已独立：`0x0001` 在任务激活且 DP/SP 双心跳在线时周期发送，不受开始/停止采集按钮门控；本轮不定义字段间联动。

任务切换不得修改设备台账中的 `faces[].active`。`deviceFaceMasks` 只决定本次任务哪些 `FaceControl` 生效。

任务模板是可复用的参数来源，不是运行状态。创建任务时必须把所选模板复制成该任务自己的独立参数快照；
用户在任务中调整参数只更新该任务快照和运行期 Owner，不得静默改写共享模板。“另存为模板”是显式操作。
这样既能复用同一套任务参数，又能保证模板以后修改时不篡改历史任务的实际配置。

## 3. 已能从现有界面确定的映射

数值转换统一使用四舍五入，并在转换前校验有限值和目标整数范围；越界返回 422，不截断、不环绕。

| UI/台账字段            | 0x0001 字段                           | 转换与约束                                                 |
| ------------------ | ----------------------------------- | ----------------------------------------------------- |
| `device.lng`       | `navigation.F_FL_Platform_GPS_Long` | `round(经度° × 1,000,000)`；当前协议为 `uint32`，V1 只接受非负可表示经度 |
| `device.lat`       | `navigation.F_FL_Platform_GPS_Lati` | `round(纬度° × 1,000,000)`；当前协议为 `uint32`，V1 只接受非负可表示纬度 |
| `device.altitude`  | `navigation.F_FL_Platform_Height`   | `round(海拔m × 100)`                                    |
| 阵面数组下标 `i`         | `faces[i].F_UI_Ant_Idx`             | 固定为 `i`，合法值 0～3，禁止由用户编辑                               |
| `faces[i].heading` | `faces[i].F_UI_AntCourse_Angle`     | 阵面绝对安装航向规范到 `[-180,180]`，再乘100                        |
| `faces[i].pitch`   | `faces[i].F_UI_AntPitch_Angle`      | 阵面安装俯仰乘100，范围 `[-90,90]`                              |
| `faces[i].roll`    | `faces[i].F_UI_AntRoll_Angle`       | 阵面安装横滚乘100，范围 `[-90,90]`                              |
| `device.maxRange`  | `control.F_UI_SysRange_Mode`        | 2 km→0，5 km→1；其他档位编码待协议确认，未确认前 UI 只允许2/5 km           |

固定部署第一版只填写 `Navigation` 的经度、纬度和高度。平台航向、俯仰、横滚、角速度、东/北/天速度、
天线系速度、地速、加速度和偏航角全部发送零；将来只有接入真实车载/机载惯导 Producer 后才能更新，
不得把阵面安装姿态写入平台惯导，也不得从地图动画或航迹反推。

设备台账不保存整机 `absoluteHeading/pitch/roll`，三个姿态字段属于每个阵面；SQLite schema、内置台账、
HTTP DTO、React 类型、编辑表单、地图姿态计算和协议映射必须使用同一模型。迁移完成前不得
实现 50 ms 控制发送，以免把错误姿态写入协议。

## 4. 任务阵面映射

1. 设备台账编辑时，用户以实测绝对航向指定物理1#阵面；四面阵的2#、3#、4#按顺时针依次增加90°、180°、
   270°自动确定。UI必须同时显示编号与航向，不能只用数组顺序暗示物理编号。
2. 单面阵只能使用bit0；四面阵允许任务通过bit0～bit3选择任意非空子集，因此可以调试一个或多个指定物理阵面。
3. 每个50 ms报文仍包含四个`FaceControl`，`F_UI_Ant_Idx`始终分别为`0x00～0x03`；任务faceMask只决定
   哪些物理阵面参与本次实验，不改变设备台账编号和安装姿态。
4. 未启用阵面的运行控制填充值仍需与DP冻结；在此之前Owner保存四面完整参数，但不得把未确认的零值解释为
   “关闭阵面”。启用阵面共享同一`CommonControl`、`FrameControl`和`Navigation`。

## 5. CommonControl 最终映射

合同中的英文驼峰键是HTTP/TypeScript稳定标识，不直接展示给用户。本次“雷达参数”面板只提供中文界面；
全局中文/English 词典与语言切换单列为独立 i18n 任务，切换语言不得改变参数值、模板或协议控制字。

除Excel明确描述为bit位图或二进制位的字段外，协议字面量默认按十六进制解释并显示。例如STC四个方案为
`0x00、0x01、0x10、0x11`，不能错误映射成连续十进制`0、1、2、3`。带物理单位的角度、距离、时间、计数
仍在UI中按十进制工程量输入，由Adapter转换为协议整数。

| 界面名称（中文 / English）与内部键 | 协议字段                       | 控件和值域                                         | 初始值 | 交互与发送规则                                 |
| --------------------------- | -------------------------- | --------------------------------------------- | --- | --------------------------------------- |
| 阵面组合模式 / Combination Mode<br>`combinationMode` | `F_UI_Combi_Mode` | 下拉：`0x00`常规、`0x01`合成 | `0x00` | 高级区保留且允许配置；标注当前DP可能不响应。 |
| 帧工作模式 / Frame Mode<br>`frameWorkMode` | `F_UI_FrameWork_Mode` | 分段选择：`0x00`景幅连续、`0x02`单帧测试 | `0x00` | `0x01`为DP自主调用，UI不得选择。决定FrameControl控件状态。 |
| 系统主模式 / System Mode<br>`systemWorkMode` | `F_UI_SysWork_Mode` | 下拉：`0x00` Air、`0x01` Ground、`0x02` Sea、`0x03` Weather、`0x04` MultiFunc | `0x00` | 高级区保留且允许配置；标注当前DP可能不响应。 |
| 探测量程 / Detection Range<br>`rangeMode` | `F_UI_SysRange_Mode` | 下拉：`0x00`为2 km、`0x01`为5 km | `0x01` | 未取得新增编码前只提供这两档。 |
| STC开关 / STC Enable<br>`stcEnabled` | `F_UI_STC_Switch` | 开关：`0x00`关、`0x01`开 | `0x00` | 关闭时`stcMode`灰显但保留其值。 |
| STC方案 / STC Profile<br>`stcMode` | `F_UI_STC_Mode` | 下拉：`0x00、0x01、0x10、0x11`对应方案一～四 | `0x00` | 仅STC开启时设备响应。 |
| 激励信号关闭 / Excitation Disable<br>`excitationDisabled` | `F_UI_Exc_Switch` | 开关：`0x00`激励开、`0x01`激励关 | `0x00` | 使用反向语义文案，避免误操作。 |
| CFAR方案 / CFAR Profile<br>`cfarMode` | `F_UI_CFAR_Mode_Ctrl` | 下拉：`0x00`方案一、`0x01`方案二 | `0x00` | 使用有符号32位协议字段，但UI只接受两个合同值。 |
| 采集工作控制 / Acquisition Control<br>`acquisitionControl` | `F_UI_STBY_Ctrl` | 暂无独立控件 | `0x00` | 当前周期编码暂固定为`0x01`；与采集按钮的字段联动留待后续合同，按钮只触发一次性 `0xFFFF`。 |
| CFAR门限1 / CFAR Threshold 1<br>`cfarThreshold1` | `F_UI_CFAR_Ctrl_1` | 十进制整数，`int32` | `0` | 界面单位 dB；按整数原样发送，未引入缩放。 |
| CFAR门限2 / CFAR Threshold 2<br>`cfarThreshold2` | `F_UI_CFAR_Ctrl_2` | 十进制整数，`int32` | `0` | 界面单位 dB；按整数原样发送，未引入缩放。 |
| 速度门限 / Velocity Threshold<br>`velocityThreshold` | `F_UI_Vel_Ctrl` | 十进制整数，`int32` | `0` | 界面单位 m/s；按整数原样发送，未引入缩放。 |
| AD记录 / AD Recording<br>`adRecordEnabled` | `F_UI_AD_Rec_Switch` | 开关：`0x00`关、`0x01`开 | `0x00` | 稳态参数。 |
| 点迹记录 / Plot Recording<br>`pointRecordEnabled` | `F_UI_Dot_Rec_Switch` | 开关：`0x00`关、`0x01`开 | `0x00` | “点迹”只是DP内部记录名称，不新增PointTopic。 |
| 起批点数 / Initiation Count<br>`trackInitiationPointCount` | `F_UI_TracDot_Ctrl` | 十进制计数，`uint32` | `0` | 带计数语义，按十进制输入。 |
| 凝聚记录 / Clustering Recording<br>`clusterRecordEnabled` | `F_UI_DotMerge_Rec_Switch` | 开关：`0x00`关、`0x01`开 | `0x00` | 稳态参数。 |
| 关联记录 / Association Recording<br>`associationRecordEnabled` | `F_UI_DotCorr_Rec_Switch` | 开关：`0x00`关、`0x01`开 | `0x00` | 稳态参数。 |

`uni_bak2[15]` 永远填零，不进入模板、HTTP或UI。

## 6. FrameControl 最终映射

| UI键                       | 协议字段                         | 控件和值域                        | 初始值    | 生效条件与规则                                             |
| ------------------------- | ---------------------------- | ---------------------------- | ------ | --------------------------------------------------- |
| `screenWaveMode`          | `F_UI_ScreenWave_Mod_Ctrl`   | 十六进制`uint32`方案编号             | `0x00` | 景幅连续模式可编辑；Excel未给方案数量，使用有界原始控制字输入。                  |
| `screenTrackEnabled`      | `F_UI_ScreenTrack_Enable`    | 开关：`0x00`关、`0x01`开             | `0x00` | 景幅连续模式可编辑。跟踪坐标仍由DP自主产生。                             |
| `beamAzimuthDeg`          | `F_UIDP_Beam_Azim_Ctrl`      | 数值，`[-45,45]°`               | 0      | 仅单帧测试可编辑，发送`round(°×100)`。                          |
| `beamElevationDeg`        | `F_UIDP_Beam_Elev_Ctrl`      | 数值，`[-45,45]°`               | 0      | 与方位采用相同协议约束；若实机俯仰范围不同，必须先修Excel再改合同。                |
| `bandwidthMode`           | `F_UIDP_Band_Switch`         | 下拉：`0x00`带宽1、`0x01`带宽2、`0x02`带宽3 | `0x00` | 单帧测试可编辑；备注暂定对应2、30、200 MHz。                            |
| `bitEnabled`              | `F_UIDP_BIT_Switch_Ctrl`     | 开关：`0x00`关、`0x01`开             | `0x00` | 仅单帧测试可编辑；开启后FPGA强制只接收，UI不改写阵面天线模式快照。                |
| `waveformCode`            | `F_UIDP_FrameWave_Ctrl`      | 十六进制控制字                      | `0x11` | 单帧测试可编辑；已分配模式使用`0x11～0x99`，`0xaa`表示自定义。未分配编号仍可能不响应。 |
| `modulationMode`          | `F_UIDP_Modul_Ctrl`          | 十六进制；`0x00`线性调频、`0x01`普通脉冲 | `0x00` | 仅`waveformCode=0xaa`时可编辑；其余值按厂家扩展。                         |
| `modulationBandwidthWord` | `F_UIDP_ModulBw_Ctrl`        | 十六进制`uint32`原始字，标注MHz       | `0x00` | 仅自定义波形生效；FPGA格式未展开，不做浮点换算。                          |
| `pulseWidthWord`          | `F_UIDP_Pw_Ctrl`             | 十六进制`uint32`原始字，标注μs        | `0x00` | 仅自定义波形生效；FPGA格式未展开。                                 |
| `repetitionPeriodWord`    | `F_UIDP_Tr_Ctrl`             | 十六进制`uint32`原始字，标注μs        | `0x00` | 仅自定义波形生效；FPGA格式未展开。                                 |
| `frameWaitPulseCount`     | `F_UIDP_FrameWait_Ctrl`      | `uint32`脉冲数                  | 0      | 仅自定义波形生效。                                           |
| `effectivePulseCount`     | `F_UIDP_EffTrN_Ctrl`         | `uint32`脉冲数                  | 0      | 仅自定义波形生效。                                           |
| `sampleStartWord`         | `F_UIDP_SampR0_Ctrl`         | 十六进制`uint32`原始字             | `0x00` | 仅自定义波形生效；采用开窗采样时由FPGA格式解释。                          |
| `sampleEndOrLengthWord`   | `F_UIDP_SampRn_Ctrl`         | 十六进制`uint32`原始字             | `0x00` | 仅自定义波形生效；协议允许表示截止位置或采样长度，DCMP不自行转换。                 |
| `waveformWriteEnabled`    | `F_UIDP_FrameWaveWrite_Ctrl` | 开关：`0x00`不写入、`0x01`写入        | `0x00` | 仅自定义波形时可编辑；按用户决定作为稳态参数持续发送。                         |
| `waveformWritePosition`   | `F_UIDP_WritePos_Ctrl`       | 十六进制，`0x11～0x99`             | `0x11` | `waveformWriteEnabled=1`时必填，表示固化目标模式字。              |

`F_DP_ScreenTrack_Azim/Elev_1..4` 由DP自主控制，UI不显示输入控件，发送时保持零；`uni_bak3[7]`永远填零。
被当前模式禁用或折叠的控件仍保留任务快照中的值，切换模式不得擅自清零。

## 7. Navigation 最终映射

| 来源     | 协议字段                     | V1规则                                            |
| ------ | ------------------------ | ----------------------------------------------- |
| 设备台账经度 | `F_FL_Platform_GPS_Long` | `round(经度°×1,000,000)`；协议为`uint32`，V1只接受非负可表示值。 |
| 设备台账纬度 | `F_FL_Platform_GPS_Lati` | `round(纬度°×1,000,000)`；协议为`uint32`，V1只接受非负可表示值。 |
| 设备台账海拔 | `F_FL_Platform_Height`   | `round(海拔m×100)`。                               |

平台航向、俯仰、横滚、三轴角速度、东/北/天速度、天线系三轴速度、地速、加速度和偏航角在固定部署V1
全部填零，`uni_bak4[14]`也填零。这些字段不进入参数模板和UI。

## 8. FaceControl 最终映射

以下映射对每个阵面分别保存；单面阵只显示1号页签，四面阵显示1～4号页签，并提供“复制参数到全部阵面”。

| UI键               | 协议字段                          | 控件和值域                   | 初始值       | 规则                                     |
| ----------------- | ----------------------------- | ----------------------- | --------- | -------------------------------------- |
| 无                 | `F_UI_Ant_Idx`                | 固定`0x00～0x03`          | 阵面下标      | 不进入模板编辑，Adapter按数组下标填写。                |
| `frequencyBand`   | `F_UI_FreBand_Pick`           | 下拉：`0x00` X、`0x01` Ku | `0x01`    | 每阵面独立。                                 |
| `frequencyIndex`  | `F_UI_Freq_Ctrl`              | 整数0～39                  | 0         | 表示频点编号，不换算物理频率；频段不支持的编号可能不响应。          |
| `rfAttenuationDb` | `F_UI_Recv_Gain_Ctrl` bits0～4 | 整数0～31 dB               | 0         | 按16/8/4/2/1 dB二进制组合编码。仅STC关闭时设备响应。     |
| `ifAttenuationDb` | `F_UI_Recv_Gain_Ctrl` bits5～9 | 整数0～31 dB               | 0         | 按16/8/4/2/1 dB二进制组合后左移5位。高于bit9的位必须为零。 |
| `antennaMode`     | `F_UI_Antenna_Mode`           | 下拉：`0x00`待机、`0x01`只接收、`0x10`只发射、`0x11`收发均加电 | `0x00` | 每阵面必须有独立控件；BIT开启时FPGA可临时强制只接收，但不修改模板值。 |
| `fan1Powered`     | `F_UI_Power_Mode` bit2        | 开关                      | 开         | 协议位为反逻辑：开电写0，断电写1。                     |
| `fan2Powered`     | `F_UI_Power_Mode` bit1        | 开关                      | 开         | 协议位为反逻辑：开电写0，断电写1。                     |
| `trModulePowered` | `F_UI_Power_Mode` bit0        | 开关                      | 开         | 协议位为反逻辑：开电写0，断电写1；bit3～31必须为零。         |
| `rfReset`         | `F_UI_RF_Reset`               | 独立按钮                    | 正常值`0xaa` | 点击动作发送`0x55`；不属于模板稳态参数，恢复时序见第10节。      |
| 设备台账阵面航向          | `F_UI_AntCourse_Angle`        | 只读，`[-180,180]°`        | 台账值       | 发送`round(°×100)`。                      |
| 设备台账阵面俯仰          | `F_UI_AntPitch_Angle`         | 只读，`[-90,90]°`          | 台账值       | 发送`round(°×100)`。                      |
| 设备台账阵面横滚          | `F_UI_AntRoll_Angle`          | 只读，`[-90,90]°`          | 台账值       | 发送`round(°×100)`。                      |

`F_DP_C1..C6` 是DP对SP的BIT反馈预留，不属于DCMP输入，发送时保持零；`F_UI_Antenna_bak[16]`永远填零。

## 9. 参数面板与提交合同

创建任务时先选择模板；任务建立后，关联雷达的“雷达配置”界面通过左侧导航切换设备配置与运行参数。设备配置由“保存修改”提交到设备台账；通用、帧、阵面运行参数由“应用参数”提交到当前任务快照和 DeviceController 运行副本，不写入设备台账：

1. 通用参数；
2. 帧参数，根据`frameWorkMode`渐进展示；
3. 1～4号阵面页签；
4. 只读部署信息；
5. RF复位动作区。

参数编辑采用“本地草稿 + 一次应用”：输入过程不发包；点击应用后提交完整参数集合，后端全部校验通过才更新
任务快照和Owner运行副本。失败保留上一份有效配置并返回逐字段错误。暂不响应字段仍保留可用控件，同时显示
“当前DP版本可能不响应”；这不代表DCMP可以伪造执行结果。

当前后端校验只返回首个失败字段的错误，不是一次返回全部字段错误；若后续要求错误数组协议，须单独评审后再实现。

## 10. RF复位待冻结项

用户点击指定阵面的复位按钮后，UI立即显示黄色“复位待观察”，Owner发送`0x55`。当前DP心跳的
`fpOnlineMask`只能表示DP是否能联系对应FP，不能作为复位ACK。以下三项属于RF动作状态机合同，不影响本次
逐字段映射冻结，但在实现按钮前必须确认：

1. `0x55`保持多少个50 ms周期以及何时恢复`0xaa`；
2. 黄色状态是等待下一次心跳、等待离线到在线的边沿，还是等待0x0100中的明确FP状态；
3. 超时后显示“未观察到恢复”还是只恢复为普通在线状态。
