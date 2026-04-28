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

    // 订阅通信层的接收信号（直接连接；若未来此处耗时过长再 moveToThread）
    connect(m_commManager, &CommunicationManager::dataReceived,
            this, &RadarDataProcessor::onCommDataReceived);

    qDebug() << QStringLiteral("[RadarDataProcessor] 已创建，订阅 CommunicationManager::dataReceived");
}

RadarDataProcessor::~RadarDataProcessor()
{
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
                                            const QByteArray& data)
{
    // 按大小分发：当前协议的业务包有两种固定长度。
    // 后续多协议时，这里可升级为"读协议头字段再分发"。
    const size_t sz = static_cast<size_t>(data.size());

    if (sz == sizeof(GeneralPackage)) {
        GeneralPackage gp;
        memcpy(&gp, data.constData(), sizeof(GeneralPackage));
        handleGeneralPackage(endpointId, gp);
        return;
    }

    if (sz == sizeof(workStandby)) {
        workStandby cmd;
        memcpy(&cmd, data.constData(), sizeof(workStandby));
        handleWorkStandby(endpointId, cmd);
        return;
    }

    // 未知大小：打印一次警告，不写库（避免噪声）
    qWarning() << QStringLiteral("[RadarDataProcessor] 未识别的数据长度: %1 字节, 端点='%2'")
                  .arg(sz).arg(endpointId);
}

void RadarDataProcessor::handleGeneralPackage(const QString& endpointId,
                                              const GeneralPackage& gp)
{
    m_generalPackageCount++;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const TargetType& tgt = gp.h_c.TrackInfo;
    const uint32_t sceneNum = gp.datapackageheader.F_UI_Scene_Num;

    // 1) 写入 tracks 业务表
    if (m_dataStore && m_dataStore->isConnected()) {
        insertTrack(nowMs, endpointId, tgt, sceneNum);
    }

    // 2) 发布到 SystemBus，供 UI 等订阅
    DataPacket packet;
    packet.sourceType     = resolveSourceType(endpointId);
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(nowMs);
    packDataToPayload(packet, gp);  // 模板函数：填 payloadType=PAYLOAD_TYPE_GENERAL_PACKAGE

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
                                           const workStandby& cmd)
{
    qDebug() << QStringLiteral("[RadarDataProcessor] 收到 workStandby: 端点='%1', 模式=%2")
                .arg(endpointId).arg(cmd.Work_Standby_Mode);

    // 纯转发到总线，作为状态事件（无业务解析）
    DataPacket packet;
    packet.sourceType     = resolveSourceType(endpointId);
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    packDataToPayload(packet, cmd);

    if (!SystemBus::getInstance().publish(std::move(packet))) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 发布 workStandby 到总线失败, seq=%1")
                      .arg(m_sequence);
    }
}

void RadarDataProcessor::insertTrack(qint64 tsMs,
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

    if (m_dataStore->insert(QStringLiteral("tracks"), row)) {
        m_trackInsertCount++;
    }
}
