#include "TestComm.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <cstring>
#include <cmath>

TestComm::TestComm(QObject* parent)
    : QObject(parent)
{
    m_trackTimer.setSingleShot(true);  // 动态调整间隔，每次只发一条
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
    m_socketWorkmode = new QUdpSocket(this);
    if (!m_socketWorkmode->bind(QHostAddress::Any, workmodePort)) {
        qCritical() << QStringLiteral("[TestComm] 绑定 workmode 端口 %1 失败: %2")
                       .arg(workmodePort)
                       .arg(m_socketWorkmode->errorString());
        return false;
    }
    connect(m_socketWorkmode, &QUdpSocket::readyRead,
            this, &TestComm::onWorkmodeReadyRead);

    m_socketDph = new QUdpSocket(this);
    if (!m_socketDph->bind(QHostAddress::Any, dphPort)) {
        qCritical() << QStringLiteral("[TestComm] 绑定 DPH 端口 %1 失败: %2")
                       .arg(dphPort)
                       .arg(m_socketDph->errorString());
        return false;
    }
    connect(m_socketDph, &QUdpSocket::readyRead,
            this, &TestComm::onDphReadyRead);

    // 尝试加载 CSV
    bool csvFound = false;
    if (!m_csvPath.isEmpty()) {
        // 用户通过命令行指定了 CSV 路径
        csvFound = loadCsv(m_csvPath);
        if (!csvFound) {
            qWarning() << QStringLiteral("[TestComm] 指定的 CSV 路径无效: %1，尝试自动搜索").arg(m_csvPath);
        }
    }
    if (!csvFound) {
        // 自动搜索多个路径
        QStringList csvSearchPaths = {
            QStringLiteral("target_data-4.csv"),
            QStringLiteral("test_comm/target_data-4.csv"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/target_data-4.csv"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../test_comm/target_data-4.csv"),
            QStringLiteral("../test_comm/target_data-4.csv"),                              // build/bin → 源码
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../test_comm/target_data-4.csv"),
        };
        for (const auto& p : csvSearchPaths) {
            if (QFile::exists(p)) {
                if (loadCsv(p)) {
                    csvFound = true;
                    break;
                }
            }
        }
    }
    if (!csvFound) {
        qWarning() << QStringLiteral("[TestComm] ★ 未找到 target_data-4.csv，回退到简易合成航迹 ★");
    }

    m_statsTimer.start();

    qDebug() << QStringLiteral("[TestComm] 已启动，双端口监听：");
    qDebug() << QStringLiteral("  workmode 端口: %1").arg(workmodePort);
    qDebug() << QStringLiteral("  DPH 端口:      %1").arg(dphPort);
    qDebug() << QStringLiteral("[TestComm] 上报目标: %1:%2")
                .arg(m_targetAddress.toString())
                .arg(m_targetPort);
    qDebug() << QStringLiteral("[TestComm] 数据源: %1")
                .arg(m_csvLoaded ? QStringLiteral("CSV 回放 (%1 条)").arg(m_csvTracks.size())
                                 : QStringLiteral("合成数据"));

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
}

// ========== CSV 加载 ==========

bool TestComm::loadCsv(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << QStringLiteral("[TestComm] 无法打开 CSV 文件: %1").arg(path);
        return false;
    }

    m_csvTracks.clear();

    QTextStream stream(&file);
    QString header = stream.readLine();  // 跳过表头
    Q_UNUSED(header);

    QDateTime prevTime;
    bool first = true;

    int lineNum = 1;
    while (!stream.atEnd()) {
        lineNum++;
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList cols = line.split(',');
        if (cols.size() < 18) {
            qWarning() << QStringLiteral("[TestComm] CSV 第 %1 行列数不足 (%2), 跳过")
                          .arg(lineNum).arg(cols.size());
            continue;
        }

        bool ok = true;
        QDateTime t = QDateTime::fromString(cols[0], QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        if (!t.isValid()) {
            t = QDateTime::fromString(cols[0], QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        if (!t.isValid()) { ok = false; }

        // 解析字段
        int id          = cols[1].toInt(&ok);
        double x        = cols[2].toDouble(&ok);
        double y        = cols[3].toDouble(&ok);
        double z        = cols[4].toDouble(&ok);
        double rangeM   = cols[5].toDouble(&ok);
        double azimuth  = cols[6].toDouble(&ok);   // 度
        double elevation = cols[7].toDouble(&ok);   // 度
        double vx       = cols[8].toDouble(&ok);
        double vy       = cols[9].toDouble(&ok);
        double vz       = cols[10].toDouble(&ok);
        double speed    = cols[11].toDouble(&ok);
        double rcs      = cols[12].toDouble(&ok);
        // int targetType  = cols[13].toInt(&ok);   // 暂未使用

        if (!ok) {
            qWarning() << QStringLiteral("[TestComm] CSV 第 %1 行解析失败").arg(lineNum);
            continue;
        }

        // 构造 GeneralPackage
        GeneralPackage gp;
        memset(&gp, 0, sizeof(GeneralPackage));

        TargetType& tgt = gp.h_c.TrackInfo;
        tgt.TargetID    = static_cast<uint32_t>(id);
        tgt.Range       = rangeM;
        tgt.Azimuth     = azimuth  * M_PI / 180.0;
        tgt.Elevation   = elevation * M_PI / 180.0;
        tgt.RadialSpeed = speed;
        tgt.RCS         = rcs;
        tgt.Altitude    = z;
        tgt.x = x;
        tgt.y = y;
        tgt.z = z;
        tgt.vx = vx;
        tgt.vy = vy;
        tgt.vz = vz;

        gp.h_c.Update_Flag = true;
        gp.h_c.view = 1;

        // 计算与上一条的时间差
        int deltaMs = m_trackIntervalMs;
        if (t.isValid()) {
            if (first) {
                deltaMs = m_trackIntervalMs;
                first = false;
            } else {
                deltaMs = static_cast<int>(prevTime.msecsTo(t));
                if (deltaMs <= 0) deltaMs = m_trackIntervalMs;
                if (deltaMs > 5000) deltaMs = m_trackIntervalMs;  // 间隔过大时用默认值
            }
            prevTime = t;
        }

        // 预序列化
        QByteArray payload(reinterpret_cast<const char*>(&gp), sizeof(GeneralPackage));
        m_csvTracks.append({ payload, deltaMs });
    }

    file.close();

    if (m_csvTracks.isEmpty()) {
        qWarning() << QStringLiteral("[TestComm] CSV 解析后无有效数据");
        return false;
    }

    m_csvLoaded = true;
    qDebug() << QStringLiteral("[TestComm] CSV 加载完成: %1 条航迹记录").arg(m_csvTracks.size());
    return true;
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
    qDebug() << QStringLiteral("  数据源: %1").arg(m_csvLoaded ? QStringLiteral("CSV 回放") : QStringLiteral("合成"));
    qDebug() << QStringLiteral("========================");
}

void TestComm::printStatus()
{
    qDebug() << QStringLiteral("===== TestComm 状态 =====");
    qDebug() << QStringLiteral("  工作状态: %1")
                .arg(m_working ? QStringLiteral("工作中（上报航迹）") : QStringLiteral("待机（不上报）"));
    qDebug() << QStringLiteral("  已上报航迹数: %1").arg(m_trackSentCount);
    qDebug() << QStringLiteral("  数据源: %1").arg(m_csvLoaded
        ? QStringLiteral("CSV 回放 (%1/%2)").arg(m_csvPlayIndex).arg(m_csvTracks.size())
        : QStringLiteral("合成数据"));
    qDebug() << QStringLiteral("========================");
}

void TestComm::toggleVerbose()
{
    m_verbose = !m_verbose;
    qDebug() << QStringLiteral("[TestComm] DPH 详细打印: %1")
                .arg(m_verbose ? QStringLiteral("开启") : QStringLiteral("关闭"));
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
    qDebug() << QStringLiteral("[收到] workStandby 命令: 模式=%1(%2)")
                .arg(mode)
                .arg(mode == 1 ? QStringLiteral("工作") : QStringLiteral("待机"));

    if (mode == 1) {
        if (!m_working) {
            m_working = true;
            m_csvPlayIndex = 0;
            m_trackSentCount = 0;
            if (m_csvLoaded && !m_csvTracks.isEmpty()) {
                // 第一条立即发送，后续按 CSV 时间戳调度
                sendTrackFromCsv(m_csvPlayIndex);
                m_csvPlayIndex++;
                if (m_csvPlayIndex >= m_csvTracks.size()) m_csvPlayIndex = 0;
                // 启动定时器：下一条按 deltaMs 延迟
                const CsvTrackRecord& nextRec = m_csvTracks[m_csvPlayIndex];
                m_trackTimer.setInterval(nextRec.deltaMs);
                m_trackTimer.setSingleShot(true);
                m_trackTimer.start();
            } else {
                m_trackTimer.setInterval(m_trackIntervalMs);
                m_trackTimer.setSingleShot(false);
                m_trackTimer.start();
            }
            qDebug() << QStringLiteral("[TestComm] 进入工作模式，开始上报航迹");
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

void TestComm::sendTrackFromCsv(int index)
{
    if (index < 0 || index >= m_csvTracks.size()) return;

    const CsvTrackRecord& rec = m_csvTracks[index];
    qint64 sent = m_socketWorkmode->writeDatagram(rec.payload, m_targetAddress, m_targetPort);

    if (sent > 0) {
        m_trackSentCount++;
        if (m_trackSentCount == 1 || m_trackSentCount % 50 == 0) {
            qDebug() << QStringLiteral("[上报] 已上报 %1 条, CSV位置=%2/%3")
                        .arg(m_trackSentCount)
                        .arg(index)
                        .arg(m_csvTracks.size());
        }
    } else {
        qWarning() << QStringLiteral("[上报失败] %1").arg(m_socketWorkmode->errorString());
    }
}

void TestComm::onTrackReportTick()
{
    if (!m_working) return;

    if (m_csvLoaded && !m_csvTracks.isEmpty()) {
        // CSV 回放模式
        sendTrackFromCsv(m_csvPlayIndex);

        m_csvPlayIndex++;
        if (m_csvPlayIndex >= m_csvTracks.size()) {
            m_csvPlayIndex = 0;
            qDebug() << QStringLiteral("[CSV回放] 一轮完成，从头循环");
        }

        const CsvTrackRecord& nextRec = m_csvTracks[m_csvPlayIndex];
        m_trackTimer.setInterval(nextRec.deltaMs);
        m_trackTimer.setSingleShot(true);
        m_trackTimer.start();
    } else {
        // 简易合成航迹：单目标匀速圆周运动
        GeneralPackage gp;
        memset(&gp, 0, sizeof(GeneralPackage));

        const double t = static_cast<double>(m_trackSentCount) * 0.1;
        TargetType& tgt = gp.h_c.TrackInfo;
        tgt.TargetID    = static_cast<uint32_t>((m_trackSentCount % 16) + 1);
        tgt.Range       = 5000.0 + 500.0 * std::sin(t * 0.2);
        tgt.Azimuth     = (45.0 + 20.0 * std::sin(t * 0.5)) * M_PI / 180.0;
        tgt.Elevation   = (30.0 + 5.0  * std::cos(t * 0.3)) * M_PI / 180.0;
        tgt.RadialSpeed = 20.0;
        tgt.RCS         = 1000.0;
        tgt.Altitude    = 200.0;
        tgt.x = tgt.Range * std::cos(tgt.Elevation) * std::cos(tgt.Azimuth);
        tgt.y = tgt.Range * std::cos(tgt.Elevation) * std::sin(tgt.Azimuth);
        tgt.z = tgt.Range * std::sin(tgt.Elevation);
        gp.h_c.Update_Flag = true;
        gp.h_c.view = 1;

        QByteArray payload(reinterpret_cast<const char*>(&gp), sizeof(GeneralPackage));
        qint64 sent = m_socketWorkmode->writeDatagram(payload, m_targetAddress, m_targetPort);

        if (sent > 0) {
            m_trackSentCount++;
            if (m_trackSentCount == 1 || m_trackSentCount % 50 == 0) {
                qDebug() << QStringLiteral("[上报] 合成航迹已上报 %1 条").arg(m_trackSentCount);
            }
        } else {
            qWarning() << QStringLiteral("[上报失败] %1").arg(m_socketWorkmode->errorString());
        }
    }
}
