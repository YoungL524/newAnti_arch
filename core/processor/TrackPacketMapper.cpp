#include "TrackPacketMapper.h"

/**
 * @brief 逐字段复制厂家航迹量和上游归属上下文，不执行任何隐式单位换算。
 * @details Range/Altitude/xyz 保持米，速度保持米每秒，Azimuth/Elevation 保持弧度。识别状态初始化为
 *          Pending，确保只有 TrackRecognizer 能把对象推进到可发布的 Completed 状态。
 */
TrackPacket mapGeneralPackageToTrackPacket(
    const GeneralPackage& package,
    const TrackPacketSourceContext& context)
{
    const TargetType& source = package.h_c.TrackInfo;

    TrackPacket packet;
    packet.traceId = context.traceId;
    packet.endpointId = context.endpointId;
    packet.deviceId = context.deviceId;
    packet.taskId = context.taskId;
    packet.receivedAtMs = context.receivedAtMs;
    packet.ingressSteadyNs = context.ingressSteadyNs;
    packet.normalizedSteadyNs = context.normalizedSteadyNs;
    packet.sceneNumber = context.sceneNumber;

    packet.trackId = source.TargetID;
    packet.rangeM = source.Range;
    packet.azimuthRad = source.Azimuth;
    packet.elevationRad = source.Elevation;
    packet.radialSpeedMps = source.RadialSpeed;
    packet.rcs = source.RCS;
    packet.altitudeM = source.Altitude;
    packet.xM = source.x;
    packet.yM = source.y;
    packet.zM = source.z;
    packet.vxMps = source.vx;
    packet.vyMps = source.vy;
    packet.vzMps = source.vz;
    packet.recognitionStatus = RecognitionStatus::Pending;
    packet.recognitionType = RecognitionType::Unknown;
    return packet;
}
