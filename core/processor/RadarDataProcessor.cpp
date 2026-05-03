#include "RadarDataProcessor.h"
#include "CommunicationManager.h"
#include "DataStore.h"
#include "SystemBus.h"

#include <QDateTime>
#include <QDebug>
#include <QVariantMap>
#include <cstring>

RadarDataProcessor::RadarDataProcessor(CommunicationManager* commManager,
                                       DataStore* dataStore,
                                       QObject* parent)
    : QObject(parent)
    , m_commManager(commManager)
    , m_dataStore(dataStore)
{
    Q_ASSERT(m_commManager != nullptr);

    // 订阅通信层的接收信号
    connect(m_commManager, &CommunicationManager::dataReceived,
            this, &RadarDataProcessor::onCommDataReceived);

    // 定期 flush 定时器：即使数据流低于阈值也不会饿死
    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &RadarDataProcessor::flushTracks);
    m_flushTimer->start(kFlushIntervalMs);

    qDebug() << QStringLiteral("[RadarDataProcessor] 已创建，订阅 CommunicationManager::dataReceived");
}

RadarDataProcessor::~RadarDataProcessor()
{
    // 析构前确保缓冲数据落盘
    if (m_flushTimer) {
        m_flushTimer->stop();
    }
    flushTracks();

    qDebug() << QStringLiteral("[RadarDataProcessor] 销毁：共处理 GeneralPackage=%1, 写入 tracks=%2")
                .arg(m_generalPackageCount)
                .arg(m_trackInsertCount);
}

void RadarDataProcessor::registerEndpointSource(const QString& endpointId,
                                                uint16_t sourceType)
{
    m_endpointSourceMap.insert(endpointId, sourceType);
    qDebug() << QStringLiteral("[RadarDataProcessor] 注册端点映射: '%1' → sourceType=%2")
                .arg(endpointId).arg(sourceType);
}

uint16_t RadarDataProcessor::resolveSourceType(const QString& endpointId) const
{
    const auto it = m_endpointSourceMap.find(endpointId);
    if (it == m_endpointSourceMap.end()) {
        return DEVICE_UNKNOWN;
    }
    return it.value();
}

void RadarDataProcessor::onCommDataReceived(const QString& endpointId,
                                            const QByteArray& data,
                                            qint64 receivedMs)
{
    // 按大小分发
    const size_t sz = static_cast<size_t>(data.size());
    bool handled = false;

    if (sz == sizeof(GeneralPackage)) {
        GeneralPackage gp;
        memcpy(&gp, data.constData(), sizeof(GeneralPackage));
        handleGeneralPackage(endpointId, gp, receivedMs);
        handled = true;
    } else if (sz == sizeof(workStandby)) {
        workStandby cmd;
        memcpy(&cmd, data.constData(), sizeof(workStandby));
        handleWorkStandby(endpointId, cmd, receivedMs);
        handled = true;
    } else {
        qWarning() << QStringLiteral("[RadarDataProcessor] 未识别的数据长度: %1 字节, 端点='%2'")
                      .arg(sz).arg(endpointId);
    }

    // 记录上行处理延时（仅对已识别的合法包统计）
    if (handled) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_uplinkLatency.addSample(nowMs - receivedMs);

        // 每 200 包打印一次延时概况
        if (m_uplinkLatency.sampleCount % 200 == 0) {
            qDebug() << QStringLiteral("[RadarDataProcessor] 上行延时: avg=%1ms min=%2ms max=%3ms [%4 samples]")
                        .arg(m_uplinkLatency.avgMs)
                        .arg(m_uplinkLatency.minMs)
                        .arg(m_uplinkLatency.maxMs)
                        .arg(m_uplinkLatency.sampleCount);
        }
    }
}

void RadarDataProcessor::handleGeneralPackage(const QString& endpointId,
                                              const GeneralPackage& gp,
                                              qint64 receivedMs)
{
    m_generalPackageCount++;

    const TargetType& tgt = gp.h_c.TrackInfo;
    const uint32_t sceneNum = gp.datapackageheader.F_UI_Scene_Num;

    // 1) 缓冲航迹，攒批写入（避免每包 fsync）
    if (m_dataStore && m_dataStore->isConnected()) {
        bufferTrack(receivedMs, endpointId, tgt, sceneNum);
    }

    // 2) 发布到 SystemBus，供 UI 等订阅
    DataPacket packet;
    packet.sourceType     = resolveSourceType(endpointId);
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(receivedMs);
    packDataToPayload(packet, gp);

    if (!SystemBus::getInstance().publish(std::move(packet))) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 发布 GeneralPackage 到总线失败, seq=%1")
                      .arg(m_sequence);
    }

    // 低频采样打印，避免刷屏
    if (m_generalPackageCount == 1 || m_generalPackageCount % 50 == 0) {
        qDebug() << QStringLiteral("[RadarDataProcessor] 已处理 GeneralPackage=%1, "
                                   "最新 TargetID=%2, Range=%3m, 景幅=%4")
                    .arg(m_generalPackageCount)
                    .arg(tgt.TargetID)
                    .arg(tgt.Range, 0, 'f', 1)
                    .arg(sceneNum);
    }
}

void RadarDataProcessor::handleWorkStandby(const QString& endpointId,
                                           const workStandby& cmd,
                                           qint64 receivedMs)
{
    qDebug() << QStringLiteral("[RadarDataProcessor] 收到 workStandby: 端点='%1', 模式=%2")
                .arg(endpointId).arg(cmd.Work_Standby_Mode);

    // 纯转发到总线，作为状态事件（无业务解析）
    DataPacket packet;
    packet.sourceType     = resolveSourceType(endpointId);
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(receivedMs);
    packDataToPayload(packet, cmd);

    if (!SystemBus::getInstance().publish(std::move(packet))) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 发布 workStandby 到总线失败, seq=%1")
                      .arg(m_sequence);
    }
}

void RadarDataProcessor::bufferTrack(qint64 tsMs,
                                     const QString& endpointId,
                                     const TargetType& tgt,
                                     uint32_t sceneNum)
{
    QVariantMap row;
    row.insert(QStringLiteral("ts_ms"),        tsMs);
    row.insert(QStringLiteral("source"),       endpointId);
    row.insert(QStringLiteral("scene_num"),    sceneNum);
    row.insert(QStringLiteral("target_id"),    tgt.TargetID);
    row.insert(QStringLiteral("range_m"),      tgt.Range);
    row.insert(QStringLiteral("azimuth_rad"),  tgt.Azimuth);
    row.insert(QStringLiteral("elevation_rad"),tgt.Elevation);
    row.insert(QStringLiteral("radial_speed"), tgt.RadialSpeed);
    row.insert(QStringLiteral("altitude_m"),   tgt.Altitude);
    row.insert(QStringLiteral("x"),            tgt.x);
    row.insert(QStringLiteral("y"),            tgt.y);
    row.insert(QStringLiteral("z"),            tgt.z);

    m_trackBuffer.append(std::move(row));

    // 达到阈值 → 立即 flush
    if (m_trackBuffer.size() >= kFlushThreshold) {
        flushTracks();
    }
}

void RadarDataProcessor::flushTracks()
{
    if (m_trackBuffer.isEmpty() || !m_dataStore || !m_dataStore->isConnected()) {
        return;
    }

    const int n = m_trackBuffer.size();
    if (m_dataStore->insertBatch(QStringLiteral("tracks"), m_trackBuffer)) {
        m_trackInsertCount += n;
        qDebug() << QStringLiteral("[RadarDataProcessor] 批量写入 tracks: %1 条").arg(n);
    } else {
        qWarning() << QStringLiteral("[RadarDataProcessor] 批量写入 tracks 失败: %1 条").arg(n);
    }
    m_trackBuffer.clear();
}
