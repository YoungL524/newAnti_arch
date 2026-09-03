/**
 * @file tst_TrackWebSocketService.cpp
 * @brief 验证不依赖 UDP 和厂家协议的 TrackTopic 到 WebSocket 显示出口。
 * @details 测试把合法 TrackPacket 经 TrackPublisher 写入共享数据域，再由 TrackWebSocketService 使用独立
 * cursor 和固定 ScratchBuffer 主动读取。真实 QWebSocket 客户端验收初始快照、增量字段、共享数据不被
 * pop、有界目标淘汰和新客户端快照；测试不创建 ProtocolAdapter，也不通过 signal 搬运业务 Sample。
 */

#include "core/datadomain/CoreDataDomain.h"
#include "core/execution/ExecutionLane.h"
#include "core/producer/TrackPublisher.h"
#include "core/web/TrackWebSocketService.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrl>
#include <QWebSocket>
#include <QtTest>

namespace {

/**
 * @brief 构造已经完成同步识别、可通过发布门禁的确定性航迹事实。
 * @param trackId 厂家在当前采集会话内分配的航迹号。
 * @param rangeM 用于区分不同增量的斜距，单位 m。
 * @return 身份、时间、有效位和运动学字段均满足 TrackPacket 合同的 Sample。
 */
core::model::Sample<core::model::TrackPacket> makeTrackSample(quint32 trackId, double rangeM)
{
    core::model::Sample<core::model::TrackPacket> sample;
    sample.meta.endpointId = core::model::EndpointId::fromRaw(3);
    sample.meta.sourceTimeUtcMs = 1'777'500'000'000;
    sample.meta.receiveTimeUtcMs = 1'777'500'000'010 + trackId;
    sample.meta.receiveTimeSteadyNs = 10'000 + trackId;
    sample.meta.sourceSequence = trackId;
    sample.meta.traceId = 100 + trackId;
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence)
        | core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceTimeUtcMs);
    sample.value.acquisitionId = core::model::AcquisitionId::fromRaw(9);
    sample.value.deviceId = core::model::DeviceId::fromRaw(5);
    sample.value.taskId = core::model::TaskId::fromRaw(7);
    sample.value.trackId = trackId;
    sample.value.rangeM = rangeM;
    sample.value.azimuthRad = 0.25;
    sample.value.elevationRad = 0.2;
    sample.value.radialVelocityMps = 12.0;
    sample.value.amplitude = 41.5;
    sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return sample;
}

/**
 * @brief 等待并取出指定模式的 WebSocket JSON 消息。
 * @param spy 监听 QWebSocket::textMessageReceived 的信号记录器。
 * @param expectedMode 期望的 `snapshot` 或 `delta` 模式。
 * @return 超时前收到的首个匹配对象；超时、JSON 非对象或无匹配时返回空对象。
 * @note 等待期间只泵送当前 Qt 测试事件循环，不启动轮询线程。
 */
QJsonObject waitForMode(QSignalSpy &spy, const QString &expectedMode)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        if (spy.isEmpty()) {
            spy.wait(50);
        }
        while (!spy.isEmpty()) {
            const QList<QVariant> arguments = spy.takeFirst();
            const QJsonDocument document = QJsonDocument::fromJson(arguments.at(0).toString().toUtf8());
            if (document.isObject()
                && document.object().value(QStringLiteral("mode")).toString() == expectedMode) {
                return document.object();
            }
        }
    }
    return {};
}

} // namespace

/** @brief 汇总协议无关 WebReader 的共享读取和有界输出验收。 */
class TrackWebSocketServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收空初始快照、发布后增量，以及 WebReader 不会消费掉 Topic 数据。 */
    void sendsSnapshotAndDeltaWithoutProtocolAdapter();

    /** @brief 验收目标容量淘汰会发送 removal，且新客户端只取得当前有界快照。 */
    void evictsOldestTargetAndSnapshotsCurrentState();
};

/**
 * @brief 使用真实本机 WebSocket 验证 TrackPublisher 之后的显示链与厂家协议彻底解耦。
 * @details 客户端连接时先收到空 snapshot；发布一条事实后收到字段完整的 delta。随后另一个独立 cursor
 * 仍可从零读到同一 TopicRecord，防止实现退化为 Web Consumer pop 数据。
 */
void TrackWebSocketServiceTest::sendsSnapshotAndDeltaWithoutProtocolAdapter()
{
    core::domain::CoreDataDomain domain(8, 4);
    core::producer::TrackPublisher publisher(domain.trackTopic());
    core::execution::ExecutionLane lane;
    core::web::TrackWebSocketService service(domain.trackTopic(), lane, 4, 4);
    QVERIFY(service.start(0));

    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    QSignalSpy messageSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(service.serverPort())));
    QVERIFY(connectedSpy.wait(3000));

    const QJsonObject snapshot = waitForMode(messageSpy, QStringLiteral("snapshot"));
    QCOMPARE(snapshot.value(QStringLiteral("type")).toString(), QStringLiteral("tracks"));
    QCOMPARE(snapshot.value(QStringLiteral("tracks")).toArray().size(), 0);

    const auto result = publisher.publish(makeTrackSample(12, 900.0));
    QVERIFY(result.accepted());
    const QJsonObject delta = waitForMode(messageSpy, QStringLiteral("delta"));
    const QJsonArray upserts = delta.value(QStringLiteral("upserts")).toArray();
    QCOMPARE(upserts.size(), 1);
    const QJsonObject point = upserts.at(0).toObject();
    QCOMPARE(point.value(QStringLiteral("acquisitionId")).toString(), QStringLiteral("9"));
    QCOMPARE(point.value(QStringLiteral("deviceId")).toString(), QStringLiteral("radar-5"));
    QCOMPARE(point.value(QStringLiteral("taskId")).toString(), QStringLiteral("task-7"));
    QCOMPARE(point.value(QStringLiteral("trackId")).toInt(), 12);
    QCOMPARE(point.value(QStringLiteral("range")).toDouble(), 900.0);
    QCOMPARE(point.value(QStringLiteral("radialSpeed")).toDouble(), 12.0);
    QCOMPARE(point.value(QStringLiteral("amplitude")).toDouble(), 41.5);
    QCOMPARE(service.cursor(), core::domain::TopicSequence(1));

    core::domain::TopicScratchBuffer<core::model::TrackPacket> independentScratch(2);
    const auto independentRead = domain.trackTopic().readAfter(0, independentScratch);
    QCOMPARE(independentRead.count, qsizetype(1));
    QCOMPARE(independentScratch[0].sample.value.trackId, quint32(12));
    client.close();
}

/**
 * @brief 以容量一的 Web 最终状态验证淘汰语义而不改变 TrackTopic retention。
 * @details 第二个 TrackKey 到达后，同一 delta 必须删除旧键并加入新键；随后连接的客户端只能在 snapshot
 * 看到新目标。该容量只约束 Web 派生状态，Topic 中两条权威事实仍由独立 cursor 顺序可读。
 */
void TrackWebSocketServiceTest::evictsOldestTargetAndSnapshotsCurrentState()
{
    core::domain::CoreDataDomain domain(8, 4);
    core::producer::TrackPublisher publisher(domain.trackTopic());
    core::execution::ExecutionLane lane;
    core::web::TrackWebSocketService service(domain.trackTopic(), lane, 4, 1);
    QVERIFY(service.start(0));

    QWebSocket firstClient;
    QSignalSpy firstConnected(&firstClient, &QWebSocket::connected);
    QSignalSpy firstMessages(&firstClient, &QWebSocket::textMessageReceived);
    firstClient.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(service.serverPort())));
    QVERIFY(firstConnected.wait(3000));
    QVERIFY(!waitForMode(firstMessages, QStringLiteral("snapshot")).isEmpty());

    QVERIFY(publisher.publish(makeTrackSample(1, 100.0)).accepted());
    QVERIFY(!waitForMode(firstMessages, QStringLiteral("delta")).isEmpty());
    QVERIFY(publisher.publish(makeTrackSample(2, 200.0)).accepted());
    const QJsonObject evictionDelta = waitForMode(firstMessages, QStringLiteral("delta"));
    const QJsonArray removals = evictionDelta.value(QStringLiteral("removals")).toArray();
    const QJsonArray upserts = evictionDelta.value(QStringLiteral("upserts")).toArray();
    QCOMPARE(removals.size(), 1);
    QCOMPARE(removals.at(0).toObject().value(QStringLiteral("trackId")).toInt(), 1);
    QCOMPARE(upserts.size(), 1);
    QCOMPARE(upserts.at(0).toObject().value(QStringLiteral("trackId")).toInt(), 2);

    QWebSocket secondClient;
    QSignalSpy secondConnected(&secondClient, &QWebSocket::connected);
    QSignalSpy secondMessages(&secondClient, &QWebSocket::textMessageReceived);
    secondClient.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(service.serverPort())));
    QVERIFY(secondConnected.wait(3000));
    const QJsonObject currentSnapshot = waitForMode(secondMessages, QStringLiteral("snapshot"));
    const QJsonArray currentTracks = currentSnapshot.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(currentTracks.size(), 1);
    QCOMPARE(currentTracks.at(0).toObject().value(QStringLiteral("trackId")).toInt(), 2);

    core::domain::TopicScratchBuffer<core::model::TrackPacket> independentScratch(4);
    QCOMPARE(domain.trackTopic().readAfter(0, independentScratch).count, qsizetype(2));
    firstClient.close();
    secondClient.close();
}

/**
 * @brief 创建带 Qt 事件循环的无界面测试入口。
 * @details QWebSocket 握手、Doorbell 和 ExecutionLane queued 唤醒均依赖该事件循环；不创建真实 GUI。
 */
QTEST_GUILESS_MAIN(TrackWebSocketServiceTest)

#include "tst_TrackWebSocketService.moc"
