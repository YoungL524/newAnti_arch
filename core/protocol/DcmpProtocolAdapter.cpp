#include "core/protocol/DcmpProtocolAdapter.h"

#include "core/task/RadarControlSettingsJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
/**
 * @brief 从可能未对齐的线协议地址读取一个小端16位无符号数。
 * @param data 至少包含两个可读字节的地址。
 * @return 转换为主机字节序的数值。
 */
quint16 readU16Le(const char *data)
{
    return qFromLittleEndian<quint16>(data);
}

/**
 * @brief 从可能未对齐的线协议地址读取一个小端32位无符号数。
 * @param data 至少包含四个可读字节的地址。
 * @return 转换为主机字节序的数值。
 */
quint32 readU32Le(const char *data)
{
    return qFromLittleEndian<quint32>(data);
}

/**
 * @brief 从可能未对齐的线协议地址读取一个小端32位有符号数。
 * @param data 至少包含四个可读字节的地址。
 * @return 保持二进制补码位型的主机整数。
 */
qint32 readI32Le(const char *data)
{
    return static_cast<qint32>(readU32Le(data));
}

/**
 * @brief 从线协议读取一个小端IEEE 754单精度数，不依赖结构体整体布局或主机端序。
 * @param data 至少包含四个可读字节的地址。
 * @return 按协议位型恢复的float；有限性由业务字段门禁另行校验。
 */
float readFloatLe(const char *data)
{
    const quint32 bits = readU32Le(data);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "协议float必须为32位");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/**
 * @brief 在已分配数据报的指定偏移写入一个小端32位字。
 */
void writeU32Le(QByteArray &bytes, quint32 offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

/**
 * @brief 将度值按协议比例尺100转换为有符号控制字。
 */
qint32 scaledAngle(double degrees)
{
    return static_cast<qint32>(std::llround(degrees * 100.0));
}

/**
 * @brief 按api-track补充合同逐字段解码224字节航迹，避免直接复制本机结构体形成隐式ABI合同。
 * @param data 已通过0x0100完整报文长度校验的航迹区起点。
 * @return 只包含线协议原始值的短生命周期航迹对象。
 */
core::protocol::DpToDcmpTrackPayload::DpTrack readTrack(const char *data)
{
    using Track = core::protocol::DpToDcmpTrackPayload::DpTrack;
    Track track;
    track.targetId = readI32Le(data + 0U);
    track.waveRow = readI32Le(data + 4U);
    track.waveCol = readI32Le(data + 8U);
    track.wave = readI32Le(data + 12U);
    track.rawWavePosition = readI32Le(data + 16U);
    track.range = readFloatLe(data + 20U);
    track.azimuth = readFloatLe(data + 24U);
    track.elevation = readFloatLe(data + 28U);
    track.radialSpeed = readFloatLe(data + 32U);
    track.amplitude = readFloatLe(data + 36U);
    track.altitude = readFloatLe(data + 40U);
    track.courseAngle = readFloatLe(data + 44U);
    track.pitchAngle = readFloatLe(data + 48U);
    track.targetSNR = readFloatLe(data + 52U);
    track.x = readFloatLe(data + 56U);
    track.y = readFloatLe(data + 60U);
    track.z = readFloatLe(data + 64U);
    track.vx = readFloatLe(data + 68U);
    track.vy = readFloatLe(data + 72U);
    track.vz = readFloatLe(data + 76U);
    track.clusterId = readI32Le(data + 80U);
    track.count = readI32Le(data + 84U);
    track.clutterDiagramMarkers = readI32Le(data + 88U);
    track.waveformMark = readI32Le(data + 92U);
    track.a = readFloatLe(data + 96U);
    track.b = readFloatLe(data + 100U);
    track.c = readFloatLe(data + 104U);
    track.tierLevel = readI32Le(data + 108U);
    track.targetView = readI32Le(data + 112U);
    track.packFlag = readU32Le(data + 116U);
    track.savedAzimuthCenter = readFloatLe(data + 120U);
    track.savedElevationCenter = readFloatLe(data + 124U);
    track.kinematicsMargin = readFloatLe(data + 128U);
    track.kinematicsDiff = readFloatLe(data + 132U);
    track.dataSource = readI32Le(data + 136U);
    track.assocState = readI32Le(data + 140U);
    track.pointId = readI32Le(data + 144U);
    track.multiplyRange = readI32Le(data + 148U);
    track.multiplyDoppler = readI32Le(data + 152U);
    track.accumulatedTime = readU32Le(data + 156U);
    track.trackRecognitionMark = readFloatLe(data + 160U);
    track.comingAndGoingSigns = readFloatLe(data + 164U);
    track.pointPosition = readFloatLe(data + 168U);
    track.pointType = readFloatLe(data + 172U);
    track.extrapolationCount = readI32Le(data + 176U);
    track.updateFlag = readU32Le(data + 180U);
    track.hostView = readI32Le(data + 184U);
    for (quint32 index = 0; index < 9U; ++index) {
        track.reserved[index] = readFloatLe(data + 188U + index * sizeof(quint32));
    }
    return track;
}

/**
 * @brief 读取SP心跳的在线值、阵面数据位及原有备份区；DP位姿使用独立布局。
 * @param payloadData 已通过完整心跳长度校验的载荷起始地址。
 * @param online 接收在线值的字段。
 * @param faceMask 接收低四位阵面状态的字段。
 * @param reserved 接收二十个备份字的固定数组。
 */
void readHeartbeatFields(
    const char *payloadData, quint16 &online, quint16 &faceMask,
    quint32 (&reserved)[core::protocol::SpHeartbeatPayload::reservedWordCount])
{
    using Payload = core::protocol::SpHeartbeatPayload;
    online = readU16Le(payloadData + Payload::onlineOffset);
    faceMask = readU16Le(payloadData + Payload::faceMaskOffset);
    for (quint32 i = 0; i < Payload::reservedWordCount; ++i) {
        reserved[i] = readU32Le(payloadData + Payload::reservedOffset + i * sizeof(quint32));
    }
}
} // namespace

namespace core::protocol {
/**
 * @brief 仅保存同步强类型入口，不隐式创建网络、线程或状态owner。
 */
DcmpProtocolAdapter::DcmpProtocolAdapter(
    DpHandler dpHandler, SpHandler spHandler, TrackHandler trackHandler)
    : m_dpHandler(std::move(dpHandler))
    , m_spHandler(std::move(spHandler))
    , m_trackHandler(std::move(trackHandler))
{
}

/**
 * @brief 逐字段写入0x0001线协议，备份区依靠零初始化保持确定，不直接发送本机结构体内存。
 * @details 当前静态平台只写经纬高；未启用阵面的天线模式强制为待机，其余模板值仍完整保留。
 */
QByteArray DcmpProtocolAdapter::encodeControl(
    const core::task::RadarControlSettings &settings,
    const core::config::DeviceConfiguration &configuration,
    quint8 faceMask,
    bool acquisitionControl)
{
    QString validationError;
    const QJsonArray faces = QJsonDocument::fromJson(configuration.facesJson.toUtf8()).array();
    if (!core::task::validateRadarControlSettings(settings, validationError)
        || faceMask == 0U || (faceMask & 0xF0U) != 0U || faces.size() < 1
        || !std::isfinite(configuration.longitudeDeg)
        || !std::isfinite(configuration.latitudeDeg)
        || !std::isfinite(configuration.altitudeM)) {
        return {};
    }
    constexpr quint32 payloadBase = DcmpFrameHeader::payloadOffset;
    QByteArray bytes(
        DcmpFrameHeader::wireSize + DcmpToDpControlPayload::wireSize + DcmpFrameTail::wireSize,
        '\0');
    writeU32Le(bytes, 0U, DcmpFrameHeader::expectedMagic);
    writeU32Le(bytes, 4U, DcmpFrameHeader::declaredFixedSize + DcmpToDpControlPayload::wireSize);
    writeU32Le(bytes, 8U, static_cast<quint32>(DcmpMessageType::DcmpToDpControl));
    writeU32Le(bytes, 12U, DcmpFrameHeader::expectedVersion);
    auto word = [&bytes](quint32 offset, quint32 value) {
        writeU32Le(bytes, payloadBase + offset, value);
    };
    const auto &common = settings.common;
    word(0U, common.combinationMode);
    word(4U, common.frameWorkMode);
    word(8U, common.systemWorkMode);
    word(12U, common.rangeMode);
    word(16U, common.stcEnabled ? 1U : 0U);
    word(20U, common.stcMode);
    word(24U, common.excitationDisabled ? 1U : 0U);
    word(28U, static_cast<quint32>(common.cfarMode));
    word(32U, acquisitionControl ? 1U : 0U);
    word(36U, static_cast<quint32>(common.cfarThreshold1));
    word(40U, static_cast<quint32>(common.cfarThreshold2));
    word(44U, static_cast<quint32>(common.velocityThreshold));
    word(48U, common.adRecordEnabled ? 1U : 0U);
    word(52U, common.pointRecordEnabled ? 1U : 0U);
    word(56U, common.trackInitiationPointCount);
    word(60U, common.clusterRecordEnabled ? 1U : 0U);
    word(64U, common.associationRecordEnabled ? 1U : 0U);

    constexpr quint32 frameBase = 128U;
    const auto &frame = settings.frame;
    word(frameBase + 0U, frame.screenWaveMode);
    word(frameBase + 4U, frame.screenTrackEnabled ? 1U : 0U);
    word(frameBase + 40U, static_cast<quint32>(scaledAngle(frame.beamAzimuthDeg)));
    word(frameBase + 44U, static_cast<quint32>(scaledAngle(frame.beamElevationDeg)));
    word(frameBase + 48U, frame.bandwidthMode);
    word(frameBase + 52U, frame.bitEnabled ? 1U : 0U);
    word(frameBase + 56U, frame.waveformCode);
    word(frameBase + 60U, frame.modulationMode);
    word(frameBase + 64U, frame.modulationBandwidthWord);
    word(frameBase + 68U, frame.pulseWidthWord);
    word(frameBase + 72U, frame.repetitionPeriodWord);
    word(frameBase + 76U, frame.frameWaitPulseCount);
    word(frameBase + 80U, frame.effectivePulseCount);
    word(frameBase + 84U, frame.sampleStartWord);
    word(frameBase + 88U, frame.sampleEndOrLengthWord);
    word(frameBase + 92U, frame.waveformWriteEnabled ? 1U : 0U);
    word(frameBase + 96U, frame.waveformWritePosition);

    constexpr quint32 navigationBase = 256U;
    word(navigationBase + 0U, static_cast<quint32>(std::llround(configuration.longitudeDeg * 1'000'000.0)));
    word(navigationBase + 4U, static_cast<quint32>(std::llround(configuration.latitudeDeg * 1'000'000.0)));
    word(navigationBase + 8U, static_cast<quint32>(std::llround(configuration.altitudeM * 100.0)));

    constexpr quint32 facesBase = 384U;
    for (quint32 index = 0; index < 4U; ++index) {
        const quint32 base = facesBase + index * 128U;
        const auto &faceSettings = settings.faces[index];
        word(base + 0U, index);
        word(base + 4U, faceSettings.frequencyBand);
        word(base + 8U, faceSettings.frequencyIndex);
        word(base + 12U, (faceSettings.rfAttenuationDb & 0x1FU)
                           | ((faceSettings.ifAttenuationDb & 0x1FU) << 5U));
        word(base + 16U, (faceMask & (quint8(1U) << index)) != 0U
                           ? faceSettings.antennaMode : 0U);
        word(base + 20U, (faceSettings.fan1Powered ? 0U : 4U)
                           | (faceSettings.fan2Powered ? 0U : 2U)
                           | (faceSettings.trModulePowered ? 0U : 1U));
        word(base + 24U, 0xAAU);
        if (index < static_cast<quint32>(faces.size()) && faces[int(index)].isObject()) {
            const QJsonObject pose = faces[int(index)].toObject();
            double heading = pose.value(QStringLiteral("headingDeg")).toDouble();
            if (heading > 180.0) heading -= 360.0;
            word(base + 52U, static_cast<quint32>(scaledAngle(heading)));
            word(base + 56U, static_cast<quint32>(scaledAngle(
                pose.value(QStringLiteral("pitchDeg")).toDouble())));
            word(base + 60U, static_cast<quint32>(scaledAngle(
                pose.value(QStringLiteral("rollDeg")).toDouble())));
        }
    }
    writeU32Le(bytes, payloadBase + DcmpToDpControlPayload::wireSize,
               DcmpFrameTail::expectedMagic);
    return bytes;
}

/**
 * @brief 用与 0x0001 相同的公共封装逐字段写入 0xFFFF；零初始化保证四个备份字为零。
 */
QByteArray DcmpProtocolAdapter::encodeWorkMode(bool work)
{
    DcmpToDpWorkModePayload payload;
    payload.workStandbyMode = work ? 1U : 0U;
    QByteArray bytes(
        DcmpFrameHeader::wireSize + DcmpToDpWorkModePayload::wireSize + DcmpFrameTail::wireSize,
        '\0');
    writeU32Le(bytes, DcmpFrameHeader::magicOffset, DcmpFrameHeader::expectedMagic);
    writeU32Le(bytes, DcmpFrameHeader::totalLengthOffset,
               DcmpFrameHeader::declaredFixedSize + DcmpToDpWorkModePayload::wireSize);
    writeU32Le(bytes, DcmpFrameHeader::messageTypeOffset,
               static_cast<quint32>(DcmpToDpWorkModePayload::messageType));
    writeU32Le(bytes, DcmpFrameHeader::versionOffset, DcmpFrameHeader::expectedVersion);
    writeU32Le(bytes, DcmpFrameHeader::payloadOffset, payload.workStandbyMode);
    writeU32Le(bytes, DcmpFrameHeader::payloadOffset + DcmpToDpWorkModePayload::wireSize,
               DcmpFrameTail::expectedMagic);
    return bytes;
}

/**
 * @brief 先做有界公共校验，再按消息类型和固定长度解码，全部成功后才调用一次owner入口。
 * @details 小端读取使用字节地址，兼容非对齐输入；长度以实际size校验，不以声明长度分配内存。
 * 时间/版本原始字段保留，不推断厂家日期编码；0x0100只校验当前显示链真正消费的航迹字段。
 */
DcmpProtocolAdapter::Result DcmpProtocolAdapter::onDatagram(
    const char *data, qint64 size, qint64 receiveUtcMs, qint64 receiveSteadyNs) const
{
    constexpr qint64 minimumFrameSize = DcmpFrameHeader::wireSize + DcmpFrameTail::wireSize;
    if (!data || size < minimumFrameSize || size > 65507 || (size - minimumFrameSize) % sizeof(quint32) != 0) {
        return Result::InvalidLength;
    }
    if (readU32Le(data + DcmpFrameHeader::magicOffset) != DcmpFrameHeader::expectedMagic) {
        return Result::InvalidMagic;
    }
    if (readU32Le(data + DcmpFrameHeader::totalLengthOffset)
        != static_cast<quint32>(size - DcmpFrameHeader::lengthExcludedSize)) {
        return Result::InvalidLength;
    }
    if (readU32Le(data + DcmpFrameHeader::versionOffset) != DcmpFrameHeader::expectedVersion) {
        return Result::UnsupportedVersion;
    }
    if (readU32Le(data + size - DcmpFrameTail::wireSize) != DcmpFrameTail::expectedMagic) {
        return Result::InvalidTail;
    }
    const auto type = static_cast<DcmpMessageType>(readU32Le(data + DcmpFrameHeader::messageTypeOffset));
    if (type != DcmpMessageType::DpHeartbeat && type != DcmpMessageType::SpHeartbeat
        && type != DcmpMessageType::DpToDcmpTrack) {
        return Result::UnsupportedMessage;
    }
    if ((type == DcmpMessageType::DpHeartbeat
         && size != DpHeartbeatPayload::frameWireSize
         && size != SpHeartbeatPayload::frameWireSize)
        || (type == DcmpMessageType::SpHeartbeat
            && size != SpHeartbeatPayload::frameWireSize)) {
        return Result::InvalidLength;
    }
    const char *const payloadData = data + DcmpFrameHeader::payloadOffset;
    DcmpFrameHeader header;
    header.F_UI_TotalLength = readU32Le(data + DcmpFrameHeader::totalLengthOffset);
    header.F_UI_MessageType = type;
    const char *const systemInfo = data + DcmpFrameHeader::systemInfoOffset;
    header.F_UI_GPS_Time_DMY = readU32Le(systemInfo);
    header.F_UI_GPS_Time_SMH = readU32Le(systemInfo + sizeof(quint32));
    header.F_UI_SYS_Time_DMY = readU32Le(systemInfo + 2U * sizeof(quint32));
    header.F_UI_SYS_Time_SMH = readU32Le(systemInfo + 3U * sizeof(quint32));
    header.F_UI_DCMP_Version = readU32Le(systemInfo + 4U * sizeof(quint32));
    header.F_UI_DCMP_Time_DMY = readU32Le(systemInfo + 5U * sizeof(quint32));
    header.F_UI_DCMP_Time_SMH = readU32Le(systemInfo + 6U * sizeof(quint32));
    if (type == DcmpMessageType::DpToDcmpTrack) {
        if (size != DpToDcmpTrackPayload::frameWireSize) return Result::InvalidLength;
        const DpToDcmpTrackPayload::DpTrack track = readTrack(
            payloadData + DpToDcmpTrackPayload::trackOffset);
        const quint32 faceIndex = readU32Le(payloadData + DpToDcmpTrackPayload::fpFaceIndexOffset);
        const bool finiteDisplayFields = std::isfinite(track.range) && std::isfinite(track.azimuth)
            && std::isfinite(track.elevation) && std::isfinite(track.radialSpeed)
            && std::isfinite(track.amplitude);
        if (faceIndex > 3U || track.targetId < 0 || track.packFlag > 1U
            || !finiteDisplayFields || track.range < 0.0F
            || track.azimuth < -45.0F || track.azimuth > 45.0F
            || track.elevation < 0.0F || track.elevation > 45.0F) {
            return Result::InvalidTrack;
        }
        if (!m_trackHandler) return Result::NoHandler;
        m_trackHandler(header, static_cast<quint8>(faceIndex),
                       readU32Le(payloadData + DpToDcmpTrackPayload::fpPacketCountOffset),
                       track, receiveUtcMs, receiveSteadyNs);
        return Result::Accepted;
    }
    const quint16 online = readU16Le(payloadData + DpHeartbeatPayload::onlineOffset);
    const quint16 mask = readU16Le(payloadData + DpHeartbeatPayload::faceMaskOffset);
    if (online > 1 || (mask & 0xFFF0U) != 0) return Result::InvalidHeartbeat;
    if (type == DcmpMessageType::DpHeartbeat) {
        if (!m_dpHandler) {
            return Result::NoHandler;
        }
        DpHeartbeatPayload payload;
        payload.dpOnline = online;
        payload.fpOnlineMask = mask;
        if (size == DpHeartbeatPayload::frameWireSize) {
            for (quint32 face = 0; face < 4U; ++face) {
                for (quint32 field = 0; field < 6U; ++field) {
                    payload.facePose[face][field] = readI32Le(
                        payloadData + DpHeartbeatPayload::facePoseOffset + face * 24U + field * 4U);
                }
            }
            payload.poseFormatTag = readU32Le(payloadData + DpHeartbeatPayload::poseFormatTagOffset);
        }
        if (online == 1U && mask != 0U
            && payload.poseFormatTag == DpHeartbeatPayload::expectedPoseFormatTag) {
            for (quint32 face = 0; face < 4U; ++face) {
                if ((mask & (1U << face)) == 0U) continue;
                const auto &pose = payload.facePose[face];
                if (pose[0] < -180'000'000 || pose[0] > 180'000'000
                    || pose[1] < -90'000'000 || pose[1] > 90'000'000
                    || pose[2] < -100'000 || pose[2] > 1'000'000
                    || pose[3] < -18'000 || pose[3] > 18'000
                    || pose[4] < -9'000 || pose[4] > 9'000
                    || pose[5] < -9'000 || pose[5] > 9'000) {
                    return Result::InvalidHeartbeat;
                }
            }
        }
        m_dpHandler(header, payload, receiveUtcMs, receiveSteadyNs);
    } else {
        if (!m_spHandler) {
            return Result::NoHandler;
        }
        SpHeartbeatPayload payload;
        readHeartbeatFields(payloadData, payload.spOnline, payload.fpDataMask, payload.bak);
        m_spHandler(header, payload, receiveUtcMs, receiveSteadyNs);
    }
    return Result::Accepted;
}
} // namespace core::protocol
