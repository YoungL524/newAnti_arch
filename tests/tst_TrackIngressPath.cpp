/**
 * @file tst_TrackIngressPath.cpp
 * @brief 验收真实UDP航迹经唯一数据域到统一WebSocket出口的完整主链。
 * @details 链路固定为UdpEndpoint到DcmpProtocolAdapter、TrackIngressProducer、TrackPublisher、TrackTopic，
 * 再由TrackWebSocketService使用独立cursor读取；测试同时验证packFlag消亡不产生旁路或Topic弹出语义。
 */

#include "core/comm/UdpEndpoint.h"
#include "core/datadomain/CoreDataDomain.h"
#include "core/execution/ExecutionLane.h"
#include "core/producer/TrackIngressProducer.h"
#include "core/model/StableIdRegistry.h"
#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/web/TrackWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QUrl>
#include <QWebSocket>
#include <QtEndian>
#include <QtTest>

#include <cstring>

namespace {

/**
 * @brief 构造由真实公共封装承载的单个0x0100航迹点。
 * @param packFlag 1表示存在，0表示同一TrackKey消亡。
 * @param faceIndex 0～3阵面索引。
 * @return 指定阵面、源序号88、目标17的固定656字节小端报文。
 */
QByteArray makeTrackFrame(quint32 packFlag, quint32 faceIndex = 0U)
{
    using namespace core::protocol;
    QByteArray bytes(static_cast<int>(DpToDcmpTrackPayload::frameWireSize), '\0');
    qToLittleEndian<quint32>(DcmpFrameHeader::expectedMagic, bytes.data());
    qToLittleEndian<quint32>(DpToDcmpTrackPayload::frameWireSize - DcmpFrameHeader::lengthExcludedSize,
                             bytes.data() + DcmpFrameHeader::totalLengthOffset);
    qToLittleEndian<quint32>(static_cast<quint32>(DcmpMessageType::DpToDcmpTrack),
                             bytes.data() + DcmpFrameHeader::messageTypeOffset);
    qToLittleEndian<quint32>(DcmpFrameHeader::expectedVersion,
                             bytes.data() + DcmpFrameHeader::versionOffset);
    char *const payload = bytes.data() + DcmpFrameHeader::payloadOffset;
    qToLittleEndian<quint32>(88U, payload + DpToDcmpTrackPayload::fpPacketCountOffset);
    qToLittleEndian<quint32>(faceIndex, payload + DpToDcmpTrackPayload::fpFaceIndexOffset);
    DpToDcmpTrackPayload::DpTrack track;
    track.targetId = 17;
    track.range = 1500.0F;
    track.azimuth = 10.0F;
    track.elevation = 5.0F;
    track.radialSpeed = -7.5F;
    track.amplitude = 123.25F;
    track.packFlag = packFlag;
    std::memcpy(payload + DpToDcmpTrackPayload::trackOffset, &track, sizeof(track));
    qToLittleEndian<quint32>(DcmpFrameTail::expectedMagic,
                             bytes.data() + bytes.size() - DcmpFrameTail::wireSize);
    return bytes;
}

/**
 * @brief 从信号记录中等待指定V1消息类型。
 * @param spy WebSocket文本消息记录器。
 * @param type trackSnapshot或trackDelta。
 * @return 三秒内收到的首个匹配JSON对象，超时返回空对象。
 */
QJsonObject waitForType(QSignalSpy &spy, const QString &type)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        if (spy.isEmpty()) spy.wait(50);
        while (!spy.isEmpty()) {
            const auto arguments = spy.takeFirst();
            const auto document = QJsonDocument::fromJson(arguments.at(0).toString().toUtf8());
            if (document.isObject() && document.object().value(QStringLiteral("type")).toString() == type) {
                return document.object();
            }
        }
    }
    return {};
}

} // namespace

/** @brief 汇总0x0100正式航迹显示主链的网络级验收。 */
class TrackIngressPathTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验证四面阵任务可只启用任意阵面子集，且未启用阵面不会误发布。 */
    void acceptsPartialFaceBinding();
    /** @brief 验证存在与消亡事实均经过同一TrackTopic并形成连续Web增量。 */
    void publishesAndRemovesThroughSharedTopic();
};

/**
 * @brief 只绑定2号和4号阵面，防止入口重新收紧为完整四面阵并破坏单阵面调试业务。
 */
void TrackIngressPathTest::acceptsPartialFaceBinding()
{
    core::domain::CoreDataDomain domain(8, 4);
    core::producer::TrackIngressProducer ingress(domain.trackTopic());
    const std::array<core::model::AcquisitionId, 4> acquisitions {{
        {}, core::model::AcquisitionId::fromRaw(7),
        {}, core::model::AcquisitionId::fromRaw(10)}};
    QVERIFY(ingress.bind(core::model::EndpointId::fromRaw(2),
                         core::model::DeviceId::fromRaw(4),
                         core::model::TaskId::fromRaw(8), acquisitions));
    core::protocol::DpToDcmpTrackPayload::DpTrack track;
    track.targetId = 1;
    track.range = 100.0F;
    track.packFlag = 1U;
    QCOMPARE(ingress.accept(0, 1, track, 1, 1),
             core::producer::TrackIngressProducer::Result::UnboundFace);
    QCOMPARE(ingress.accept(1, 2, track, 2, 2),
             core::producer::TrackIngressProducer::Result::Published);
}

/**
 * @brief 装配正式组件并显式绑定测试会话，不使用模拟Publisher或第二条Web发送路径。
 * @details WebReader消费后TrackTopic仍保留两条事实，证明Consumer只维护cursor且没有pop共享数据。
 */
void TrackIngressPathTest::publishesAndRemovesThroughSharedTopic()
{
    core::domain::CoreDataDomain domain(8, 4);
    core::producer::TrackIngressProducer ingress(domain.trackTopic());
    const std::array<core::model::AcquisitionId, 4> acquisitions {
        core::model::AcquisitionId::fromRaw(6), core::model::AcquisitionId::fromRaw(7),
        core::model::AcquisitionId::fromRaw(9), core::model::AcquisitionId::fromRaw(10)};
    const auto deviceId = core::model::DeviceId::fromRaw(4);
    QVERIFY(ingress.bind(core::model::EndpointId::fromRaw(2), deviceId,
                         core::model::TaskId::fromRaw(8), acquisitions));

    core::protocol::DcmpProtocolAdapter adapter({}, {},
        [&](const auto &, quint8 face, quint32 sequence, const auto &track, qint64 utc, qint64 steady) {
            QCOMPARE(ingress.accept(face, sequence, track, utc, steady),
                     core::producer::TrackIngressProducer::Result::Published);
        });
    core::comm::UdpEndpoint endpoint(
        [&](const char *data, qint64 size, const QHostAddress &, quint16, qint64 utc, qint64 steady) {
            QCOMPARE(adapter.onDatagram(data, size, utc, steady),
                     core::protocol::DcmpProtocolAdapter::Result::Accepted);
        });
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));

    core::execution::ExecutionLane lane;
    core::model::StableIdRegistry stableIds;
    QVERIFY(stableIds.registerDevice(deviceId, QUuid(QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"))));
    QVERIFY(stableIds.registerAcquisition(acquisitions[0], QUuid(QStringLiteral("{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}"))));
    QVERIFY(stableIds.registerAcquisition(acquisitions[1], QUuid(QStringLiteral("{dddddddd-dddd-dddd-dddd-dddddddddddd}"))));
    QVERIFY(stableIds.registerAcquisition(acquisitions[2], QUuid(QStringLiteral("{eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee}"))));
    QVERIFY(stableIds.registerAcquisition(acquisitions[3], QUuid(QStringLiteral("{ffffffff-ffff-ffff-ffff-ffffffffffff}"))));
    core::web::WebSocketGateway gateway;
    core::web::TrackWebSocketService reader(domain.trackTopic(), lane, gateway, stableIds, 4, 8);
    QVERIFY(gateway.start(0));
    QWebSocket client;
    QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws/v1").arg(gateway.port())));
    QVERIFY(!waitForType(messages, QStringLiteral("trackSnapshot")).isEmpty());

    QUdpSocket sender;
    const QByteArray present = makeTrackFrame(1U);
    QCOMPARE(sender.writeDatagram(present, QHostAddress::LocalHost, endpoint.localPort()), qint64(present.size()));
    const QJsonObject presentPayload = waitForType(messages, QStringLiteral("trackDelta"))
        .value(QStringLiteral("payload")).toObject();
    QCOMPARE(presentPayload.value(QStringLiteral("upserts")).toArray().size(), 1);
    QCOMPARE(presentPayload.value(QStringLiteral("removals")).toArray().size(), 0);

    const QByteArray secondFace = makeTrackFrame(1U, 1U);
    QCOMPARE(sender.writeDatagram(secondFace, QHostAddress::LocalHost, endpoint.localPort()),
             qint64(secondFace.size()));
    const QJsonArray secondFaceUpserts = waitForType(messages, QStringLiteral("trackDelta"))
        .value(QStringLiteral("payload")).toObject()
        .value(QStringLiteral("upserts")).toArray();
    QCOMPARE(secondFaceUpserts.size(), 1);
    QCOMPARE(secondFaceUpserts.at(0).toObject().value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));
    QCOMPARE(secondFaceUpserts.at(0).toObject().value(QStringLiteral("faceIndex")).toInt(), 1);

    const QByteArray ended = makeTrackFrame(0U);
    QCOMPARE(sender.writeDatagram(ended, QHostAddress::LocalHost, endpoint.localPort()), qint64(ended.size()));
    const QJsonObject endedPayload = waitForType(messages, QStringLiteral("trackDelta"))
        .value(QStringLiteral("payload")).toObject();
    QCOMPARE(endedPayload.value(QStringLiteral("upserts")).toArray().size(), 0);
    QCOMPARE(endedPayload.value(QStringLiteral("removals")).toArray().size(), 1);
    QCOMPARE(domain.trackTopic().range().newest, core::domain::TopicSequence(3));
}

/**
 * @brief 提供UDP、WebSocket和ExecutionLane queued唤醒所需的无界面Qt事件循环。
 */
QTEST_GUILESS_MAIN(TrackIngressPathTest)

#include "tst_TrackIngressPath.moc"
