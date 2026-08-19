#include <QtTest>

#include "SystemBus.h"
#include "TrackDisplayWebBridge.h"
#include "TrackTypes.h"
#include "WebService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QUrl>
#include <QWebSocket>

class TrackDisplayWebDeltaTest : public QObject
{
    Q_OBJECT

    static TrackDisplayPoint point(uint32_t index)
    {
        TrackDisplayPoint value;
        value.deviceId = QStringLiteral("radar-1");
        value.taskId = QStringLiteral("task-1");
        value.trackId = 77;
        value.pointIndex = index;
        value.timestamp = 1000 + index;
        value.range = 1000.0 + index;
        value.azimuth = 0.1;
        value.elevation = 0.02;
        value.radialSpeed = 3.0;
        value.altitude = 50.0;
        return value;
    }

    static bool publishUpdate(TrackDisplayUpdate update)
    {
        auto shared = std::make_shared<const TrackDisplayUpdate>(std::move(update));
        DataPacket packet;
        packet.sourceType = SOURCE_TRACK_DISPLAY;
        packet.dispatchTarget = UI_OUTPUTWIDGET;
        packet.command = DataCommand::TrackDisplayUpdate;
        packet.payload = shared;
        return SystemBus::getInstance().publish(std::move(packet));
    }

    static QJsonObject findMessage(const QSignalSpy& spy,
                                   const QString& mode, qint64 revision)
    {
        for (const auto& arguments : spy) {
            const auto doc = QJsonDocument::fromJson(arguments.at(0).toString().toUtf8());
            const QJsonObject object = doc.object();
            if (object.value(QStringLiteral("type")).toString() == QStringLiteral("tracks")
                && object.value(QStringLiteral("mode")).toString() == mode
                && object.value(QStringLiteral("revision")).toVariant().toLongLong() == revision) {
                return object;
            }
        }
        return {};
    }

private slots:
    void newClientGetsSnapshotThenOnlyDeltas()
    {
        web::WebService service;
        const auto started = service.start(0, 0);
        QVERIFY(started.ok);
        QVERIFY(started.wsPort != 0);

        TrackDisplayWebBridge bridge(&service);
        bridge.start();

        TrackDisplayUpdate initial;
        initial.mode = TrackDisplayUpdateMode::Snapshot;
        initial.revision = 1;
        initial.timestamp = 1000;
        for (uint32_t i = 0; i < 600; ++i)
            initial.upserts.append(point(i));
        QVERIFY(publishUpdate(initial));

        QWebSocket first;
        QSignalSpy firstConnected(&first, &QWebSocket::connected);
        QSignalSpy firstMessages(&first, &QWebSocket::textMessageReceived);
        first.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(started.wsPort)));
        QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findMessage(firstMessages, QStringLiteral("snapshot"), 1).isEmpty(), 3000);

        const QJsonObject snapshot = findMessage(firstMessages, QStringLiteral("snapshot"), 1);
        QCOMPARE(snapshot.value(QStringLiteral("tracks")).toArray().size(), 600);
        const int snapshotBytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact).size();
        QVERIFY(snapshotBytes > 48 * 1024);

        // A client joining later receives the bridge's current state, not a future periodic full frame.
        QWebSocket second;
        QSignalSpy secondConnected(&second, &QWebSocket::connected);
        QSignalSpy secondMessages(&second, &QWebSocket::textMessageReceived);
        second.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(started.wsPort)));
        QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findMessage(secondMessages, QStringLiteral("snapshot"), 1).isEmpty(), 3000);

        TrackDisplayUpdate delta;
        delta.mode = TrackDisplayUpdateMode::Delta;
        delta.revision = 2;
        delta.timestamp = 2000;
        delta.removals.append({QStringLiteral("radar-1"), 77, 0});
        delta.upserts.append(point(600));
        QVERIFY(publishUpdate(delta));
        QTRY_VERIFY_WITH_TIMEOUT(!findMessage(firstMessages, QStringLiteral("delta"), 2).isEmpty(), 3000);

        const QJsonObject deltaJson = findMessage(firstMessages, QStringLiteral("delta"), 2);
        QCOMPARE(deltaJson.value(QStringLiteral("upserts")).toArray().size(), 1);
        QCOMPARE(deltaJson.value(QStringLiteral("removals")).toArray().size(), 1);
        const int deltaBytes = QJsonDocument(deltaJson).toJson(QJsonDocument::Compact).size();
        QVERIFY(deltaBytes < snapshotBytes / 10);
        QVERIFY(deltaBytes < 4 * 1024);

        TrackDisplayUpdate heartbeat;
        heartbeat.mode = TrackDisplayUpdateMode::Delta;
        heartbeat.revision = 3;
        heartbeat.timestamp = 3000;
        QVERIFY(publishUpdate(heartbeat));
        QTRY_VERIFY_WITH_TIMEOUT(!findMessage(firstMessages, QStringLiteral("delta"), 3).isEmpty(), 3000);
        const int heartbeatBytes = QJsonDocument(
            findMessage(firstMessages, QStringLiteral("delta"), 3))
            .toJson(QJsonDocument::Compact).size();
        QVERIFY(heartbeatBytes < 256);

        first.close();
        second.close();
        bridge.stop();
        service.stop();
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TrackDisplayWebDeltaTest)
#include "tst_TrackDisplayWebDelta.moc"
