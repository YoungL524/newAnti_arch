#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/comm/UdpEndpoint.h"
#include <QtTest>
#include <QtEndian>
#include <QUdpSocket>
#include <cstring>

namespace {
/**
 * @brief 手工字节夹具使用小端标志、120声明长度、20260915版本，避免以待测编码器自证正确。
 */
QByteArray heartbeat(bool sp = false)
{
    QByteArray bytes = QByteArray::fromHex("1a1aa1a1780000000101000033283501");
    bytes.append(QByteArray(28, '\0'));
    bytes.append(QByteArray::fromHex("01000500"));
    bytes.append(QByteArray(80, '\0'));
    bytes.append(QByteArray::fromHex("a2a1b2b1"));
    if (sp) {
        bytes[8] = 3;
        bytes[9] = 3;
    }
    return bytes;
}

/**
 * @brief 按0x0100固定偏移构造一条合法四阵面航迹报文，不调用待测解析器生成字节。
 * @return 阵面2、源序号77、目标9且packFlag为“存在”的656字节报文。
 */
QByteArray trackFrame()
{
    using Payload = core::protocol::DpToDcmpTrackPayload;
    QByteArray bytes(static_cast<int>(Payload::frameWireSize), '\0');
    qToLittleEndian<quint32>(core::protocol::DcmpFrameHeader::expectedMagic, bytes.data());
    qToLittleEndian<quint32>(Payload::frameWireSize - core::protocol::DcmpFrameHeader::lengthExcludedSize,
                             bytes.data() + core::protocol::DcmpFrameHeader::totalLengthOffset);
    qToLittleEndian<quint32>(static_cast<quint32>(core::protocol::DcmpMessageType::DpToDcmpTrack),
                             bytes.data() + core::protocol::DcmpFrameHeader::messageTypeOffset);
    qToLittleEndian<quint32>(core::protocol::DcmpFrameHeader::expectedVersion,
                             bytes.data() + core::protocol::DcmpFrameHeader::versionOffset);
    char *const payload = bytes.data() + core::protocol::DcmpFrameHeader::payloadOffset;
    qToLittleEndian<quint32>(77U, payload + Payload::fpPacketCountOffset);
    qToLittleEndian<quint32>(1U, payload + Payload::fpFaceIndexOffset);
    Payload::DpTrack track;
    track.targetId = 9;
    track.range = 1250.5F;
    track.azimuth = -12.5F;
    track.elevation = 8.0F;
    track.radialSpeed = 14.0F;
    track.amplitude = 42.25F;
    track.packFlag = 1U;
    std::memcpy(payload + Payload::trackOffset, &track, sizeof(track));
    qToLittleEndian<quint32>(core::protocol::DcmpFrameTail::expectedMagic,
                             bytes.data() + bytes.size() - core::protocol::DcmpFrameTail::wireSize);
    return bytes;
}
}

/** @brief 验收小端心跳、拒绝边界、独立强类型交付和真实UDP接入，无状态模拟旁路。 */
class DcmpProtocolAdapterTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 校验非对齐读取、原始公共字段、所有位图组合及两种心跳隔离。 */
    void validFrames();
    /** @brief 截断、错误封装、非法字段和未知类型均不得交付心跳。 */
    void rejectsInvalid();
    /** @brief 实际回环UDP必须经正式端点再调用Adapter。 */
    void udpIntegration();
    /** @brief 0x0100必须按固定长度交付阵面、源序号和单个强类型协议航迹。 */
    void validTrackFrame();
};

/**
 * @brief 穷举online和低四位，保留备份原值及接收时间，证明离线值0并非格式错误。
 */
void DcmpProtocolAdapterTest::validFrames()
{
    using namespace core::protocol;
    int dpCount = 0;
    int spCount = 0;
    quint16 expectedOnline = 0;
    quint16 expectedMask = 0;
    DcmpProtocolAdapter adapter(
        [&](const DcmpFrameHeader &header, const DpHeartbeatPayload &payload, qint64 utc, qint64 steady) {
            ++dpCount;
            QCOMPARE(header.F_UI_TotalLength, quint32(120));
            QCOMPARE(header.F_UI_GPS_Time_DMY, quint32(0x12345678));
            QCOMPARE(payload.dpOnline, expectedOnline);
            QCOMPARE(payload.fpOnlineMask, expectedMask);
            QCOMPARE(payload.bak[19], quint32(0xAABBCCDD));
            QCOMPARE(utc, qint64(100));
            QCOMPARE(steady, qint64(200));
        },
        [&](const DcmpFrameHeader &, const SpHeartbeatPayload &payload, qint64, qint64) {
            ++spCount;
            QCOMPARE(payload.spOnline, expectedOnline);
            QCOMPARE(payload.fpDataMask, expectedMask);
        });
    for (int sp = 0; sp < 2; ++sp) {
        for (expectedOnline = 0; expectedOnline <= 1; ++expectedOnline) {
            for (expectedMask = 0; expectedMask < 16; ++expectedMask) {
                QByteArray frame = heartbeat(sp != 0);
                qToLittleEndian<quint32>(0x12345678, frame.data() + 16);
                qToLittleEndian<quint32>(0xAABBCCDD, frame.data() + 124);
                qToLittleEndian<quint16>(expectedOnline, frame.data() + 44);
                qToLittleEndian<quint16>(expectedMask, frame.data() + 46);
                const QByteArray unaligned = QByteArray(1, 'x') + frame;
                QCOMPARE(adapter.onDatagram(unaligned.constData() + 1, frame.size(), 100, 200),
                         DcmpProtocolAdapter::Result::Accepted);
            }
        }
    }
    QCOMPARE(dpCount, 32);
    QCOMPARE(spCount, 32);
    DcmpProtocolAdapter noHandlers({}, {});
    const auto frame = heartbeat();
    QCOMPARE(noHandlers.onDatagram(frame.constData(), frame.size(), 100, 200), DcmpProtocolAdapter::Result::NoHandler);
}

/**
 * @brief 每次仅破坏一个条件；循环检查所有截断长度，确保无越界读取及错误状态更新。
 */
void DcmpProtocolAdapterTest::rejectsInvalid()
{
    using namespace core::protocol;
    using Result = DcmpProtocolAdapter::Result;
    int deliveries = 0;
    DcmpProtocolAdapter adapter(
        [&](const DcmpFrameHeader &, const DpHeartbeatPayload &, qint64, qint64) { ++deliveries; },
        [&](const DcmpFrameHeader &, const SpHeartbeatPayload &, qint64, qint64) { ++deliveries; });
    const auto valid = heartbeat();
    QCOMPARE(adapter.onDatagram(nullptr, 132, 0, 0), Result::InvalidLength);
    for (int size = 0; size < valid.size(); ++size) {
        QCOMPARE(adapter.onDatagram(valid.constData(), size, 0, 0), Result::InvalidLength);
    }
    QByteArray frame = valid;
    frame[0] = 0;
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidMagic);
    frame = valid;
    frame[4] = char(132);
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidLength);
    frame = valid;
    frame[12] = 0;
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::UnsupportedVersion);
    frame = valid;
    frame[131] = 0;
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidTail);
    frame = valid;
    frame[44] = 2;
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidHeartbeat);
    frame = heartbeat(true);
    frame[47] = 1;
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidHeartbeat);
    frame = valid;
    qToLittleEndian<quint32>(0x9999U, frame.data() + DcmpFrameHeader::messageTypeOffset);
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::UnsupportedMessage);
    frame = valid + QByteArray(4, '\0');
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidLength);
    // 声明长度和包尾均正确，但心跳多出四字节仍须拒绝。
    qToLittleEndian<quint32>(124, frame.data() + 4);
    qToLittleEndian<quint32>(0xB1B2A1A2, frame.data() + 132);
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 0, 0), Result::InvalidLength);
    QCOMPARE(deliveries, 0);
}

/**
 * @brief 外部socket只发送协议字节，接收时间由UdpEndpoint采集，Adapter只向对应入口交付。
 */
void DcmpProtocolAdapterTest::udpIntegration()
{
    using namespace core::protocol;
    int dpCount = 0;
    int spCount = 0;
    DcmpProtocolAdapter adapter(
        [&](const DcmpFrameHeader &, const DpHeartbeatPayload &p, qint64 utc, qint64 steady) {
            QCOMPARE(p.fpOnlineMask, quint16(5));
            QVERIFY(utc > 0 && steady > 0);
            ++dpCount;
        },
        [&](const DcmpFrameHeader &, const SpHeartbeatPayload &p, qint64, qint64) {
            QCOMPARE(p.fpDataMask, quint16(5));
            ++spCount;
        });
    core::comm::UdpEndpoint endpoint([&](const char *data, qint64 size, const QHostAddress &, quint16,
                                       qint64 utc, qint64 steady) {
        QCOMPARE(adapter.onDatagram(data, size, utc, steady), DcmpProtocolAdapter::Result::Accepted);
    });
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));
    QUdpSocket peer;
    QCOMPARE(peer.writeDatagram(heartbeat(), QHostAddress::LocalHost, endpoint.localPort()), qint64(132));
    QCOMPARE(peer.writeDatagram(heartbeat(true), QHostAddress::LocalHost, endpoint.localPort()), qint64(132));
    QTRY_COMPARE(dpCount, 1);
    QTRY_COMPARE(spCount, 1);
}

/**
 * @brief 验证航迹解码不进入Topic，也不复用心跳handler承担数据路由。
 * @details Adapter只同步交付短生命周期协议值；运行时身份绑定和弧度映射属于后续Producer门禁。
 */
void DcmpProtocolAdapterTest::validTrackFrame()
{
    using namespace core::protocol;
    int deliveries = 0;
    DcmpProtocolAdapter adapter({}, {},
        [&](const DcmpFrameHeader &header, quint8 faceIndex, quint32 sourceSequence,
            const DpToDcmpTrackPayload::DpTrack &track, qint64 utc, qint64 steady) {
            ++deliveries;
            QCOMPARE(header.F_UI_MessageType, DcmpMessageType::DpToDcmpTrack);
            QCOMPARE(faceIndex, quint8(1));
            QCOMPARE(sourceSequence, quint32(77));
            QCOMPARE(track.targetId, qint32(9));
            QCOMPARE(track.range, 1250.5F);
            QCOMPARE(track.azimuth, -12.5F);
            QCOMPARE(track.packFlag, quint32(1));
            QCOMPARE(utc, qint64(300));
            QCOMPARE(steady, qint64(400));
        });
    QByteArray frame = trackFrame();
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 300, 400),
             DcmpProtocolAdapter::Result::Accepted);
    QCOMPARE(deliveries, 1);

    qToLittleEndian<quint32>(4U,
        frame.data() + DcmpFrameHeader::payloadOffset + DpToDcmpTrackPayload::fpFaceIndexOffset);
    QCOMPARE(adapter.onDatagram(frame.constData(), frame.size(), 300, 400),
             DcmpProtocolAdapter::Result::InvalidTrack);
    QCOMPARE(deliveries, 1);
}

/**
 * @brief 提供真实UDP所需事件循环，不装配伪造State或Web出口。
 */
QTEST_GUILESS_MAIN(DcmpProtocolAdapterTest)
#include "tst_DcmpProtocolAdapter.moc"
