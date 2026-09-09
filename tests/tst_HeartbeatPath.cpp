#include "core/datadomain/CoreDataDomain.h"
#include "core/app/ApplicationRuntime.h"
#include "core/comm/UdpEndpoint.h"
#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/producer/HeartbeatStateOwner.h"
#include "core/web/StateWebSocketService.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
/** @brief 验收真实UDP经Adapter与状态域到Web，无直连业务消息捷径。 */
class HeartbeatPathTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    /** @brief 验收独立计时、精确2/5秒边界、恢复及同值不增加revision。 */
    void deadlines();
    /** @brief 通过真实UDP和WebSocket验收初始灰、DP更新及SP独立性。 */
    void network();
    /** @brief 验证Web绑定失败回滚UDP、重复启动保持服务及停止后重新启动。 */
    void lifecycle();
};
/**
 * @brief 显式单调时钟消除等待误差，不依赖真实定时器运行5秒。
 */
void HeartbeatPathTest::deadlines()
{
    core::domain::CoreDataDomain domain(2,2);
    core::producer::HeartbeatStateOwner owner(domain.deviceState());
    owner.accept(false,1,5,100,1000000000LL);
    const auto revision=domain.deviceState().revision();
    owner.accept(false,1,5,100,1000000000LL);
    QCOMPARE(domain.deviceState().revision(),revision);
    owner.check(101,2999999999LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(1));
    owner.check(102,3000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(2));
    owner.check(105,6000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(0));
    QCOMPARE(domain.deviceState().latest()->sample->value.fpOnlineMask,quint16(0));
    owner.accept(true,1,15,106,7000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.spHeartbeat,quint8(1));
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(0));
    owner.accept(false,1,1,107,8000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(1));
    owner.accept(false,0,0,108,9000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(2));
    const auto beforeInvalid = domain.deviceState().revision();
    owner.accept(false,2,0,109,10000000000LL);
    QCOMPARE(domain.deviceState().revision(),beforeInvalid);
    owner.check(113,14000000000LL);
    QCOMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(0));
}
/**
 * @brief 手写黄金报文确保测试不与生产解码器共享编码错误；所有端口由OS临时分配。
 */
void HeartbeatPathTest::network()
{
    core::domain::CoreDataDomain domain(2,2);
    core::execution::ExecutionLane lane;
    core::producer::HeartbeatStateOwner owner(domain.deviceState());
    owner.start();
    core::protocol::DcmpProtocolAdapter adapter(
        [&owner](const auto&,const auto& p,qint64 u,qint64 s){owner.accept(false,p.dpOnline,p.fpOnlineMask,u,s);},
        [&owner](const auto&,const auto& p,qint64 u,qint64 s){owner.accept(true,p.spOnline,p.fpDataMask,u,s);});
    core::comm::UdpEndpoint endpoint([&adapter](const char* p,qint64 n,const QHostAddress&,quint16,qint64 u,qint64 s){adapter.onDatagram(p,n,u,s);});
    QVERIFY(endpoint.start(QHostAddress::LocalHost,0));
    core::web::StateWebSocketService web(domain.deviceState(),lane);
    QVERIFY(web.start(0));
    QWebSocket client;
    QSignalSpy messages(&client,&QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(web.port())));
    QTRY_VERIFY(messages.count()>0);
    QCOMPARE(QJsonDocument::fromJson(messages.last().at(0).toString().toUtf8()).object()["dpHeartbeat"].toInt(),0);
    QByteArray bytes=QByteArray::fromHex("1a1aa1a178000000010100002c283501");
    bytes.append(QByteArray(28,0)); bytes.append(QByteArray::fromHex("01000500")); bytes.append(QByteArray(80,0)); bytes.append(QByteArray::fromHex("a2a1b2b1"));
    QUdpSocket sender;
    QCOMPARE(sender.writeDatagram(bytes,QHostAddress::LocalHost,endpoint.localPort()),qint64(132));
    QTRY_COMPARE(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(1));
    QTRY_VERIFY(messages.count()>1);
    const auto json=QJsonDocument::fromJson(messages.last().at(0).toString().toUtf8()).object();
    QCOMPARE(json["fpOnlineMask"].toInt(),5);
    QCOMPARE(json["spHeartbeat"].toInt(),0);
    bytes[8]=3; bytes[9]=3;
    QCOMPARE(sender.writeDatagram(bytes,QHostAddress::LocalHost,endpoint.localPort()),qint64(132));
    QTRY_COMPARE(domain.deviceState().latest()->sample->value.spHeartbeat,quint8(1));
    // 实际owner定时器应在5秒期限后的一个检查周期内发出灰色状态，额外容差用于CI调度。
    QTRY_COMPARE_WITH_TIMEOUT(domain.deviceState().latest()->sample->value.dpHeartbeat,quint8(0),6500);
    QTRY_COMPARE_WITH_TIMEOUT(domain.deviceState().latest()->sample->value.spHeartbeat,quint8(0),1500);
    QTRY_COMPARE(QJsonDocument::fromJson(messages.last().at(0).toString().toUtf8()).object()["spHeartbeat"].toInt(),0);
}
/**
 * @brief 用端口冲突验收组合根全有或全无启动，并验证停止与重启不泄漏绑定。
 */
void HeartbeatPathTest::lifecycle()
{
    QWebSocketServer blocker(QStringLiteral("blocker"),QWebSocketServer::NonSecureMode);
    QVERIFY(blocker.listen(QHostAddress::LocalHost,0));
    core::app::ApplicationRuntime runtime;
    QVERIFY(!runtime.start(0,blocker.serverPort()));
    QCOMPARE(runtime.internalPort(),quint16(0));
    QVERIFY(!runtime.lastError().isEmpty());
    QVERIFY(runtime.start(0,0));
    const auto port=runtime.internalPort();
    QVERIFY(!runtime.start(0,0));
    QCOMPARE(runtime.internalPort(),port);
    runtime.stop(); runtime.stop();
    QCOMPARE(runtime.internalPort(),quint16(0));
    QVERIFY(runtime.start(port,0));
}
/**
 * @brief 在Qt事件循环中运行真实网络与确定性期限验收。
 */
QTEST_GUILESS_MAIN(HeartbeatPathTest)
#include "tst_HeartbeatPath.moc"
