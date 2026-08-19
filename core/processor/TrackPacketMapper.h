/**
 * @file TrackPacketMapper.h
 * @brief 定义厂家 GeneralPackage 到系统唯一权威航迹 TrackPacket 的字段映射边界。
 * @details 映射器只做字段重命名和上下文合并，不进行坐标、角度或单位换算，也不推断识别结果。
 *          厂家协议字段来自 GeneralPackage，端点、设备、任务和链路时间由 RadarDataProcessor 通过
 *          TrackPacketSourceContext 注入；输出可直接交给 TrackRecognizer 和 SystemBus。
 */

#pragma once

#include "StructureReceive.h"
#include "business/TrackTypes.h"

/**
 * @brief 输入适配阶段附加的业务上下文
 * @details 不属于厂家协议，也不作为独立航迹模型流转。
 */
struct TrackPacketSourceContext {
    uint64_t traceId = 0;          ///< 单进程内递增的链路 ID，贯穿接收、识别、存储和显示。
    QString endpointId;            ///< 收到厂家报文的通信端点 ID。
    QString deviceId;              ///< 由 DeviceRegistry 将端点解析出的业务设备 ID。
    QString taskId;                ///< 接收时该设备所属的活动任务 ID；允许为空。
    qint64 receivedAtMs = 0;       ///< UDP 读取完成时的 Unix 时间戳，单位毫秒。
    qint64 ingressSteadyNs = 0;    ///< UDP 入口单调时间，单位纳秒，用于进程内延时。
    qint64 normalizedSteadyNs = 0; ///< 完成标准字段映射的单调时间，单位纳秒。
    uint32_t sceneNumber = 0;      ///< 厂家大包头中的景幅号，用于同一轮扫描关联。
};

/**
 * @brief 无副作用地把厂家总包和接收上下文组合成 Pending 状态的 TrackPacket。
 * @return 保留厂家原始量纲并携带完整归属、场景和时间信息的新航迹对象。
 */
TrackPacket mapGeneralPackageToTrackPacket(
    const GeneralPackage& package,
    const TrackPacketSourceContext& context);
