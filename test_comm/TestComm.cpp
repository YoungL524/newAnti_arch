#include "TestComm.h"
#include <QDebug>
#include <QDateTime>
#include <cstring>
#include <cmath>

TestComm::TestComm(QObject* parent)
    : QObject(parent)
{
    // 定时器默认不启动，收到 work 命令时才启动
    m_trackTimer.setInterval(m_trackIntervalMs);
    connect(&m_trackTimer, &QTimer::timeout, this, &TestComm::onTrackReportTick);
}

TestComm::~TestComm()
{
    m_trackTimer.stop();
    if (m_socketWorkmode) m_socketWorkmode->close();
    if (m_socketDph)      m_socketDph->close();
}

bool TestComm::initialize(quint16 workmodePort, quint16 dphPort)
{
    // socket 1：workmode 专用（也用于向 core 上报航迹）
    m_socketWorkmode = new QUdpSocket(this);
    if (!m_socketWorkmode->bind(QHostAddress::Any, workmodePort)) {
        qCritical() << QStringLiteral("[TestComm] 绑定 workmode 端口 %1 失败: %2")
                       .arg(workmodePort)
                       .arg(m_socketWorkmode->errorString());
        return false;
    }
    connect(m_socketWorkmode, &QUdpSocket::readyRead,
            this, &TestComm::onWorkmodeReadyRead);

    // socket 2：DPH 专用
    m_socketDph = new QUdpSocket(this);
    if (!m_socketDph->bind(QHostAddress::Any, dphPort)) {
        qCritical() << QStringLiteral("[TestComm] 绑定 DPH 端口 %1 失败: %2")
                       .arg(dphPort)
                       .arg(m_socketDph->errorString());
        return false;
    }
    connect(m_socketDph, &QUdpSocket::readyRead,
            this, &TestComm::onDphReadyRead);

    m_statsTimer.start();

    qDebug() << QStringLiteral("[TestComm] 已启动，双端口监听：");
    qDebug() << QStringLiteral("  workmode 端口: %1 (接收 core 命令 + 上报航迹)")
                .arg(workmodePort);
    qDebug() << QStringLiteral("  DPH 端口:      %1 (仅接收 core 的包头)")
                .arg(dphPort);
    qDebug() << QStringLiteral("[TestComm] 上报目标: %1:%2")
                .arg(m_targetAddress.toString())
                .arg(m_targetPort);
    qDebug() << QStringLiteral("[TestComm] 当前状态: 待机（未收到 work 命令前不上报航迹）");

    return true;
}

void TestComm::setTarget(const QString& address, quint16 port)
{
    m_targetAddress = QHostAddress(address);
    m_targetPort = port;
}

void TestComm::setTrackReportInterval(int intervalMs)
{
    if (intervalMs < 10) intervalMs = 10;
    m_trackIntervalMs = intervalMs;
    m_trackTimer.setInterval(m_trackIntervalMs);
}

// ========== 查询 / 显示 ==========

void TestComm::printDphStats()
{
    double elapsedSec = m_statsTimer.elapsed() / 1000.0;
    double rate = (elapsedSec > 0) ? (m_dphRecvCount / elapsedSec) : 0;

    qDebug() << QStringLiteral("===== DPH 接收统计 =====");
    qDebug() << QStringLiteral("  收到DPH总数: %1").arg(m_dphRecvCount);
    qDebug() << QStringLiteral("  运行时间: %1 秒").arg(elapsedSec, 0, 'f', 1);
    qDebug() << QStringLiteral("  接收频率: %1 包/秒 (期望20包/秒=50ms间隔)").arg(rate, 0, 'f', 1);
    qDebug() << QStringLiteral("  最后景幅序号: %1").arg(m_lastSceneNum);
    qDebug() << QStringLiteral("  详细打印: %1").arg(m_verbose ? QStringLiteral("开启") : QStringLiteral("关闭"));
    qDebug() << QStringLiteral("========================");
}

void TestComm::printStatus()
{
    qDebug() << QStringLiteral("===== TestComm 状态 =====");
    qDebug() << QStringLiteral("  工作状态: %1")
                .arg(m_working ? QStringLiteral("工作中（上报航迹）") : QStringLiteral("待机（不上报）"));
    qDebug() << QStringLiteral("  上报周期: %1 ms").arg(m_trackIntervalMs);
    qDebug() << QStringLiteral("  已上报航迹数: %1").arg(m_trackSentCount);
    qDebug() << QStringLiteral("  下一个目标 ID: %1").arg(m_nextTargetId);
    qDebug() << QStringLiteral("========================");
}

void TestComm::toggleVerbose()
{
    m_verbose = !m_verbose;
    qDebug() << QStringLiteral("[TestComm] DPH 详细打印: %1")
                .arg(m_verbose ? QStringLiteral("开启（注意：50ms一个包，会大量输出）")
                               : QStringLiteral("关闭"));
}

// ========== UDP 接收 ==========

void TestComm::onWorkmodeReadyRead()
{
    drainSocket(m_socketWorkmode, &TestComm::handleWorkmodePacket);
}

void TestComm::onDphReadyRead()
{
    drainSocket(m_socketDph, &TestComm::handleDphPacket);
}

void TestComm::drainSocket(QUdpSocket* sock,
                           void (TestComm::*handler)(const QByteArray&,
                                                     const QHostAddress&, quint16))
{
    while (sock->hasPendingDatagrams()) {
        const qint64 pending = sock->pendingDatagramSize();
        if (pending <= 0) {
            // Windows loopback ICMP 不可达幻影包——读空跳过
            char dummy;
            sock->readDatagram(&dummy, 0);
            continue;
        }

        QByteArray datagram;
        datagram.resize(static_cast<int>(pending));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 actual = sock->readDatagram(datagram.data(), datagram.size(),
                                                 &sender, &senderPort);
        if (actual <= 0) continue;
        datagram.resize(static_cast<int>(actual));
        (this->*handler)(datagram, sender, senderPort);
    }
}

void TestComm::handleWorkmodePacket(const QByteArray& data,
                                    const QHostAddress& sender, quint16 senderPort)
{
    if (static_cast<size_t>(data.size()) == sizeof(workStandby)) {
        workStandby cmd;
        memcpy(&cmd, data.constData(), sizeof(workStandby));
        handleWorkStandby(cmd);
        return;
    }

    qDebug() << QStringLiteral("[workmode端口] 收到未知数据 (%1字节) 来自 %2:%3")
                .arg(data.size()).arg(sender.toString()).arg(senderPort);
}

void TestComm::handleDphPacket(const QByteArray& data,
                               const QHostAddress& sender, quint16 senderPort)
{
    if (static_cast<size_t>(data.size()) < sizeof(DataPackageHeader)) {
        qDebug() << QStringLiteral("[DPH端口] 包太短 (%1字节) 来自 %2:%3")
                    .arg(data.size()).arg(sender.toString()).arg(senderPort);
        return;
    }

    DataPackageHeader dph;
    memcpy(&dph, data.constData(), sizeof(DataPackageHeader));

    if (dph.F_UI_Package_Head != 0xA1A1A1A1) {
        qDebug() << QStringLiteral("[DPH端口] 包头标志不匹配 (0x%1)")
                    .arg(dph.F_UI_Package_Head, 0, 16);
        return;
    }

    m_dphRecvCount++;
    m_lastSceneNum = dph.F_UI_Scene_Num;

    if (m_verbose) {
        qDebug() << QStringLiteral("[收到DPH] 景幅=%1, 时间码=%2, 工作模式=%3, 大小=%4字节")
                    .arg(dph.F_UI_Scene_Num)
                    .arg(dph.F_UI_Time_Code)
                    .arg(dph.F_UI_Work_Mode)
                    .arg(data.size());
    } else if (m_dphRecvCount % 100 == 0) {
        const double elapsedSec = m_statsTimer.elapsed() / 1000.0;
        const double rate = (elapsedSec > 0) ? (m_dphRecvCount / elapsedSec) : 0;
        qDebug() << QStringLiteral("[DPH] 已收到 %1 包, 频率 %2 包/秒, 景幅=%3")
                    .arg(m_dphRecvCount)
                    .arg(rate, 0, 'f', 1)
                    .arg(dph.F_UI_Scene_Num);
    }
}

void TestComm::handleWorkStandby(const workStandby& cmd)
{
    const uint32_t mode = cmd.Work_Standby_Mode;
    qDebug() << QStringLiteral("[收到] workStandby 命令: 模式=%1(%2), 位置=(%3, %4, %5)")
                .arg(mode)
                .arg(mode == 1 ? QStringLiteral("工作") : QStringLiteral("待机"))
                .arg(cmd.Lon_Coordinates, 0, 'f', 6)
                .arg(cmd.Lat_Coordinates, 0, 'f', 6)
                .arg(cmd.Altitude, 0, 'f', 2);

    if (mode == 1) {
        if (!m_working) {
            m_working = true;
            m_trackTimer.start();
            qDebug() << QStringLiteral("[TestComm] 进入工作模式：开始每 %1ms 上报航迹")
                        .arg(m_trackIntervalMs);
        } else {
            qDebug() << QStringLiteral("[TestComm] 已处于工作模式，命令忽略");
        }
    } else {
        if (m_working) {
            m_working = false;
            m_trackTimer.stop();
            qDebug() << QStringLiteral("[TestComm] 进入待机模式：停止上报（已上报 %1 条）")
                        .arg(m_trackSentCount);
        } else {
            qDebug() << QStringLiteral("[TestComm] 已处于待机模式，命令忽略");
        }
    }
}

// ========== 航迹上报 ==========

void TestComm::onTrackReportTick()
{
    // 构造一个完整的 GeneralPackage（核心业务包）
    GeneralPackage gp;
    memset(&gp, 0, sizeof(GeneralPackage));

    // 随机/合成的模拟航迹数据：一个绕圆周慢速运动的目标
    const double t = static_cast<double>(m_trackSentCount) * 0.1;  // 秒
    const double azDeg   = 45.0 + 20.0 * std::sin(t * 0.5);
    const double elDeg   = 30.0 + 5.0  * std::cos(t * 0.3);
    const double rangeM  = 5000.0 + 500.0 * std::sin(t * 0.2);

    TargetType& tgt = gp.h_c.TrackInfo;
    tgt.TargetID    = m_nextTargetId;
    tgt.Range       = rangeM;
    tgt.Azimuth     = azDeg  * M_PI / 180.0;
    tgt.Elevation   = elDeg  * M_PI / 180.0;
    tgt.RadialSpeed = 20.0;
    tgt.Altitude    = 200.0;
    tgt.x = rangeM * std::cos(tgt.Azimuth);
    tgt.y = rangeM * std::sin(tgt.Azimuth);
    tgt.z = tgt.Altitude;

    gp.h_c.Update_Flag = true;
    gp.h_c.view = 1;

    QByteArray data(reinterpret_cast<const char*>(&gp), sizeof(GeneralPackage));
    // 上报复用 workmode socket（命令-响应同链路）
    qint64 sent = m_socketWorkmode->writeDatagram(data, m_targetAddress, m_targetPort);

    if (sent > 0) {
        m_trackSentCount++;
        m_nextTargetId++;
        if (m_trackSentCount == 1 || m_trackSentCount % 50 == 0) {
            qDebug() << QStringLiteral("[上报] 已上报 %1 条, 最新 TargetID=%2, 方位=%3°, 距离=%4m")
                        .arg(m_trackSentCount)
                        .arg(tgt.TargetID)
                        .arg(azDeg, 0, 'f', 2)
                        .arg(rangeM, 0, 'f', 0);
        }
    } else {
        qWarning() << QStringLiteral("[上报失败] %1").arg(m_socketWorkmode->errorString());
    }
}
