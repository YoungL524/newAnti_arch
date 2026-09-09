#include "core/comm/UdpEndpoint.h"
#include <QtTest>
#include <QUdpSocket>

/** @brief 验收真实回环UDP的数据报边界、显式发送、独占绑定及重新启动，不旁路正式端点。 */
class UdpEndpointTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 空处理函数不能启动且不占用端口；无需依赖Debug断言或异常捕获。 */
    void missingHandler();
    /** @brief 发送允许边界大小，超限应在进入socket前拒绝。 */
    void sendLimit();
    /** @brief 真实socket发送的数据必须原样进入唯一同步回调，含正确来源与接收时间。 */
    void receiveAndSend();
    /** @brief 端口被占用必须明确失败，停止后可重绑，不能静默共享端口。 */
    void conflictAndRestart();
    /** @brief 超过一轮64包的突发输入能够通过续读耗尽，回调内停止后不再交付数据。 */
    void boundedDrainAndStop();
};

/**
 * @brief 验证错误配置可由调用方正常处理，不会在构造期中断进程。
 */
void UdpEndpointTest::missingHandler()
{
    core::comm::UdpEndpoint endpoint(core::comm::UdpEndpoint::ReceiveHandler{});
    QVERIFY(!endpoint.start(QHostAddress::LocalHost, 0));
    QVERIFY(!endpoint.lastError().isEmpty());
    QCOMPARE(endpoint.localPort(), quint16(0));
}

/**
 * @brief 在回环地址验证最大载荷及超限拒绝，接收方使用独立socket避免自收混淆。
 */
void UdpEndpointTest::sendLimit()
{
    core::comm::UdpEndpoint endpoint([](const char *, qint64, const QHostAddress &, quint16, qint64, qint64) {});
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    const QByteArray maximum(65507, 'x');
    QVERIFY(endpoint.send(maximum, QHostAddress::LocalHost, peer.localPort()));
    QTRY_VERIFY(peer.hasPendingDatagrams());
    QByteArray received(65507, '\0');
    QCOMPARE(peer.readDatagram(received.data(), received.size()), qint64(65507));
    QCOMPARE(received, maximum);
    QVERIFY(!endpoint.send(QByteArray(65508, 'x'), QHostAddress::LocalHost, peer.localPort()));
    QVERIFY(!endpoint.lastError().isEmpty());
}

/**
 * @brief 使用独立发送socket验证端点传输边界，包括带零字节的数据报及显式回复目标。
 */
void UdpEndpointTest::receiveAndSend()
{
    QByteArray received;
    quint16 senderPort = 0;
    qint64 receivedUtc = 0;
    qint64 receivedSteady = 0;
    core::comm::UdpEndpoint endpoint([&](const char *data, qint64 size, const QHostAddress &address,
                                       quint16 port, qint64 utc, qint64 steady) {
        QCOMPARE(address, QHostAddress(QHostAddress::LocalHost));
        received = QByteArray(data, int(size));
        senderPort = port;
        receivedUtc = utc;
        receivedSteady = steady;
    });
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    const QByteArray bytes("a\0b", 3);
    QCOMPARE(peer.writeDatagram(bytes, QHostAddress::LocalHost, endpoint.localPort()), qint64(3));
    QTRY_COMPARE(received, bytes);
    QCOMPARE(senderPort, peer.localPort());
    QVERIFY(receivedUtc > 0);
    QVERIFY(receivedSteady > 0);
    QVERIFY(endpoint.send(bytes, QHostAddress::LocalHost, peer.localPort()));
    QTRY_VERIFY(peer.hasPendingDatagrams());
    char reply[3];
    QCOMPARE(peer.readDatagram(reply, 3), qint64(3));
    QCOMPARE(QByteArray(reply, 3), bytes);
    endpoint.stop();
    QVERIFY(!endpoint.send(bytes, QHostAddress::LocalHost, peer.localPort()));
}

/**
 * @brief 同地址端口只能由一个端点拥有；失败端点和正常端点的生命周期彼此独立。
 */
void UdpEndpointTest::conflictAndRestart()
{
    const auto sink = [](const char *, qint64, const QHostAddress &, quint16, qint64, qint64) {};
    core::comm::UdpEndpoint first(sink);
    core::comm::UdpEndpoint second(sink);
    QVERIFY(first.start(QHostAddress::LocalHost, 0));
    const quint16 port = first.localPort();
    QVERIFY(!first.start(QHostAddress::LocalHost, 0));
    QCOMPARE(first.localPort(), port);
    QVERIFY(!second.start(QHostAddress::LocalHost, port));
    QVERIFY(!second.lastError().isEmpty());
    first.stop();
    first.stop();
    QCOMPARE(first.localPort(), quint16(0));
    QVERIFY(second.start(QHostAddress::LocalHost, port));
}

/**
 * @brief 在事件循环启动前发送80个小包跨越64包预算，避免只测首轮掩盖续读遗漏。
 */
void UdpEndpointTest::boundedDrainAndStop()
{
    int count = 0;
    core::comm::UdpEndpoint *active = nullptr;
    core::comm::UdpEndpoint endpoint([&](const char *, qint64, const QHostAddress &, quint16, qint64, qint64) {
        ++count;
        if (count == 80) {
            active->stop();
        }
    });
    active = &endpoint;
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));
    QUdpSocket peer;
    for (int i = 0; i < 80; ++i) {
        QCOMPARE(peer.writeDatagram(QByteArray(1, 'x'), QHostAddress::LocalHost, endpoint.localPort()), qint64(1));
    }
    QTRY_COMPARE(count, 80);
    QCOMPARE(endpoint.localPort(), quint16(0));
    QVERIFY(endpoint.start(QHostAddress::LocalHost, 0));
    QCOMPARE(peer.writeDatagram(QByteArray(1, 'y'), QHostAddress::LocalHost, endpoint.localPort()), qint64(1));
    QTRY_COMPARE(count, 81);
}

/**
 * @brief 启动真实socket所需的QCoreApplication事件循环，不启动设备或GUI。
 */
QTEST_GUILESS_MAIN(UdpEndpointTest)
#include "tst_UdpEndpoint.moc"
