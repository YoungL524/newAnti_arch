# TrackPacket 航迹事实契约

> 状态：现行规范  
> 版本：3.0
> 生效日期：2026-09-01  
> 前置规范：[统一单位、坐标与时间契约](统一单位坐标时间契约.md)、[进程内数值标识符契约](进程内数值标识符契约.md)

## 1. 定位

`TrackPacket` 是一台完整雷达的某个阵面、某次采集会话在一个测量时刻产生的单条标准航迹观测。一个
`DeviceId` 对应设备台账中的整机，`faceIndex` 区分单面阵或四面阵内部阵面，不另建 `FaceId` 类型。

```text
RadarProtocolAdapter
  -> TrackPacket(Pending)
  -> 同步识别
  -> Sample<TrackPacket>
  -> TrackPublisher 门禁
  -> HistoryTopic<TrackPacket>
```

协议 DTO 只能是适配器内部短生命周期对象，不进入 Topic。显示状态、Web DTO、数据库记录和未来融合航迹均不是
TrackPacket 的中间版本。

## 2. SampleMeta

`SampleMeta` 是所有强类型事实共用的 64 字节以内定长元数据：

| 字段 | 类型 | 合同 |
| --- | --- | --- |
| `endpointId` | `EndpointId` | 接收完整报文的端点句柄；发布时非零且已验证存在 |
| `sourceSequence` | `quint64` | 仅 `HasSourceSequence` 置位时有效；零可以是厂家合法序号 |
| `sourceTimeUtcMs` | `qint64` | 仅 `HasSourceTimeUtcMs` 置位时有效，且必须为正 Unix epoch 毫秒 |
| `receiveTimeUtcMs` | `qint64` | socket 取出完整报文时采集的正 Unix epoch 毫秒 |
| `receiveTimeSteadyNs` | `qint64` | 同一接收点采集的正进程内单调纳秒 |
| `traceId` | `quint64` | 非零诊断关联值，不作业务主键 |
| `validity` | `SampleFieldFlags` | 只允许声明源序号和源 UTC 时间 |
| `quality` | `SampleQuality` | `Good` 或 `Suspect` |

未置位的可选字段必须为零，保证相同语义只有一种内存表示。`publishTimeSteadyNs` 不再随每条 Sample 驻留；
Publisher 在入口计算接收至发布时延并写入聚合指标。

## 3. TrackPacket 字段

| 字段 | 类型 | 合同 |
| --- | --- | --- |
| `acquisitionId` | `AcquisitionId` | 非零活动采集会话句柄；停止、断线或重连后重新分配 |
| `deviceId` | `DeviceId` | 非零完整雷达句柄；解析整机台账及公共安装参数 |
| `faceIndex` | `quint8` | 整机内阵面索引；单面阵固定为0，四面阵取0～3 |
| `taskId` | `TaskId` | 接收瞬间任务快照；零表示无活动任务，不参与 TrackKey |
| `trackId` | `quint32` | 厂家航迹号；零合法，只在采集会话内解释 |
| `scanSequence` | `quint32` | 仅 `HasScanSequence` 置位时有效；未提供时必须清零 |
| `rangeM` | `double` | 目标相对雷达的斜距，单位 m，有限且不小于零 |
| `azimuthRad` | `double` | 相对当前阵面法线的局部方位角，范围 `[-π/4, π/4]` |
| `elevationRad` | `double` | 从局部水平面向上为正，范围 `[0, π/4]` |
| `radialVelocityMps` | `double` | 单位 m/s；抵近雷达为正，远离雷达为负 |
| `amplitude` | `double` | 厂家原始幅值；不换算 dBsm，当前只要求有限 |
| `validity` | `TrackFieldFlags` | 当前只允许 `HasScanSequence` |
| `packFlag` | `quint8` | 航迹存在标志；1表示存在，0表示该TrackKey消亡，其他值非法 |
| `recognitionStatus` | `RecognitionStatus` | 发布时必须为 `Completed` |
| `recognitionType` | `RecognitionType` | 当前只冻结 `Unknown` |

所有 double 字段必须有限。幅值的厂家单位和标度尚未冻结，因此字段名不带物理单位后缀，不对正负或范围做
未经协议支撑的推断。

## 4. 整机与阵面

主链设备台账只登记完整的单面阵或四面阵雷达。固定规则：

- 删除 `FaceId` 类型及 TrackPacket.faceId；
- `DeviceId` 映射一台完整雷达及其公共经纬高、heading、pitch、roll；
- 单面阵和四面阵均只注册一个 DeviceId；
- 单面阵只允许 `faceIndex=0`，四面阵允许0～3并分别建立 AcquisitionId；
- 原始 TrackPacket 始终保留所属阵面的局部角度，不提前做跨阵面融合；
- 其他厂家、异构组网和多地点联合部署不进入当前主链，由插件边界承担。

## 5. TrackKey

运行时航迹身份固定为：

```text
TrackKey = (AcquisitionId, DeviceId, faceIndex, trackId)
```

该结构在当前编译器下预算不超过 24 字节。`taskId`、扫描号、时间、traceId、endpointId 和 Topic sequence 均不
参与身份。相同 trackId 在不同整机、阵面或 AcquisitionId 下必须得到不同 TrackKey。

## 6. 坐标与符号

局部坐标仍使用 RFU：

```text
+X：阵面右方
+Y：阵面法线正前方
+Z：阵面上方
```

```text
horizontalM = rangeM * cos(elevationRad)
xM = horizontalM * sin(azimuthRad)
yM = horizontalM * cos(azimuthRad)
zM = rangeM * sin(elevationRad)
```

`azimuthRad` 是相对阵面法线的有符号角，不归一化为 `[0,2π)`。`radialVelocityMps` 采用雷达业务约定而不是
数学上的 `d(range)/dt` 符号：抵近为正、远离为负。

## 7. 发布门禁

TrackPublisher 只能检查 Sample 本身可观察的条件：

1. EndpointId、DeviceId、AcquisitionId 非零；
2. 接收 UTC 和 steady 时间为正，traceId 非零；
3. validity 不含未知位，未置位的可选字段已经清零；
4. 已声明有效的 source UTC 时间为正；
5. `rangeM >= 0`；
6. `azimuthRad ∈ [-π/4, π/4]`；
7. `elevationRad ∈ [0, π/4]`；
8. 径向速度与幅值有限；
9. `packFlag` 只能为0或1；
10. `faceIndex` 只能为0～3；
11. recognitionStatus 为 Completed。

注册表是否存在、DeviceId 是否属于当前 endpoint、AcquisitionId 是否仍活动，必须由 Producer 上游控制上下文
验证，Topic 不反向查询控制平面。

## 8. 生命周期与 Consumer

- 同一 TrackKey 的后续 Sample 是新观测，WebReader 按 sequence 折叠为浏览器显示态；
- `packFlag == 1` 表示存在观测；`packFlag == 0` 表示该 TrackKey 消亡，WebReader 删除其完整尾迹；
- 不得使用特殊 trackId、零距离或 Failed 状态替代消亡语义；
- gap 由 Consumer 明确处理，不能要求 Producer 补历史；
- 显示使用 DeviceId 对应安装姿态完成 RFU→ENU→WGS-84 投影；
- 存储和 Web 在序列化边界把数值句柄解析为外部稳定标识；
- Consumer 不修改已发布 TrackPacket，也不根据厂家类型再次调整单位和符号。

## 9. 资源验收

- 当前 Qt 5.14.2/MinGW 7.3 x64 实测 `sizeof(SampleMeta) == 48`；
- 当前 Qt 5.14.2/MinGW 7.3 x64 实测 `sizeof(TrackPacket) == 72`；
- 当前 Qt 5.14.2/MinGW 7.3 x64 实测 `sizeof(TrackKey) == 24`；
- 三者保持平凡复制；
- 不含 QString、QUuid、std::optional、动态容器或所有权指针；
- trackId、sourceSequence、scanSequence 的零值语义必须由 validity/合同正确区分。
