#include "core/protocol/DcmpProtocolAdapter.h"
#include <QtEndian>
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
 * @brief 读取DP/SP心跳共有的在线值、阵面位图和备份区，避免两类载荷各自维护一套偏移循环。
 * @param payloadData 已通过完整心跳长度校验的载荷起始地址。
 * @param online 接收在线值的字段。
 * @param faceMask 接收低四位阵面状态的字段。
 * @param reserved 接收二十个备份字的固定数组。
 */
void readHeartbeatFields(
    const char *payloadData, quint16 &online, quint16 &faceMask,
    quint32 (&reserved)[core::protocol::DpHeartbeatPayload::reservedWordCount])
{
    using Payload = core::protocol::DpHeartbeatPayload;
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
DcmpProtocolAdapter::DcmpProtocolAdapter(DpHandler dpHandler, SpHandler spHandler)
    : m_dpHandler(std::move(dpHandler)), m_spHandler(std::move(spHandler))
{
}

/**
 * @brief 先做有界公共校验，再按消息类型解释132字节心跳，全部成功后才调用一次owner入口。
 * @details 小端读取使用字节地址，兼容非对齐输入；长度以实际size校验，不以声明长度分配内存。
 * 时间/版本原始字段保留，不推断厂家日期编码。备份字不参与业务判定。
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
    if (type != DcmpMessageType::DpHeartbeat && type != DcmpMessageType::SpHeartbeat) {
        return Result::UnsupportedMessage;
    }
    if (size != DpHeartbeatPayload::frameWireSize
        || DpHeartbeatPayload::frameWireSize != SpHeartbeatPayload::frameWireSize) {
        return Result::InvalidLength;
    }
    const char *const payloadData = data + DcmpFrameHeader::payloadOffset;
    const quint16 online = readU16Le(payloadData + DpHeartbeatPayload::onlineOffset);
    const quint16 mask = readU16Le(payloadData + DpHeartbeatPayload::faceMaskOffset);
    if (online > 1 || (mask & 0xFFF0U) != 0) {
        return Result::InvalidHeartbeat;
    }
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
    if (type == DcmpMessageType::DpHeartbeat) {
        if (!m_dpHandler) {
            return Result::NoHandler;
        }
        DpHeartbeatPayload payload;
        readHeartbeatFields(payloadData, payload.dpOnline, payload.fpOnlineMask, payload.bak);
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
