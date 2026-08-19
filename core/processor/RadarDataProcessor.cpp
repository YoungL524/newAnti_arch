#include "RadarDataProcessor.h"
#include "CommunicationManager.h"
#include "DeviceRegistry.h"
#include "TaskService.h"
#include "SystemBus.h"
#include "log/LogContext.h"
#include "metrics/MonotonicClock.h"
#include "metrics/PipelineLatencyMonitor.h"
#include "TrackPacketMapper.h"

#include <QDateTime>
#include <QDebug>
#include <cstring>

/**
 * @brief 绑定通信层的原始报文信号，使解析工作在本对象所在线程串行执行。
 * @details Qt 自动连接会根据两个 QObject 的线程亲和性选择直接或队列投递；构造时不打开端点。
 */
RadarDataProcessor::RadarDataProcessor(CommunicationManager* commManager,
                                       QObject* parent)
    : QObject(parent)
    , m_commManager(commManager)
{
    Q_ASSERT(m_commManager != nullptr);

    // 订阅通信层的接收信号
    connect(m_commManager, &CommunicationManager::dataReceived,
            this, &RadarDataProcessor::onCommDataReceived);

    qDebug() << QStringLiteral("[RadarDataProcessor] 已创建，订阅 CommunicationManager::dataReceived");
}

/**
 * @brief 注入端点到业务设备的热路径映射服务，不转移所有权。
 */
void RadarDataProcessor::setDeviceRegistry(DeviceRegistry* reg)
{
    m_deviceReg = reg;
}

/**
 * @brief 注入接收时设备到活动任务的归属解析服务，不转移所有权。
 */
void RadarDataProcessor::setTaskService(TaskService* taskService)
{
    m_taskService = taskService;
}

/**
 * @brief 注入可选分段延时采集器；为空时关闭精细阶段指标但不影响处理链路。
 */
void RadarDataProcessor::setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor)
{
    m_latencyMonitor = latencyMonitor;
}

/** @brief 输出生命周期累计处理量；依赖服务由 AppController 负责在本对象之后释放。 */
RadarDataProcessor::~RadarDataProcessor()
{
    qDebug() << QStringLiteral("[RadarDataProcessor] 销毁：共处理 GeneralPackage=%1")
                .arg(m_generalPackageCount);
}

/**
 * @brief 建立协议端点到 DeviceType 的显式来源映射，供 DataPacket.sourceType 使用。
 * @details 同一 endpointId 重复登记会覆盖旧值；业务 deviceId 由 DeviceRegistry 独立解析。
 */
void RadarDataProcessor::registerEndpointSource(const QString& endpointId,
                                                uint16_t sourceType)
{
    m_endpointSourceMap.insert(endpointId, sourceType);
    qDebug() << QStringLiteral("[RadarDataProcessor] 注册端点映射: '%1' → sourceType=%2")
                .arg(endpointId).arg(sourceType);
}

/**
 * @brief 查询端点的协议来源类型；未登记端点明确返回 DEVICE_UNKNOWN。
 */
uint16_t RadarDataProcessor::resolveSourceType(const QString& endpointId) const
{
    const auto it = m_endpointSourceMap.find(endpointId);
    if (it == m_endpointSourceMap.end()) {
        return DEVICE_UNKNOWN;
    }
    return it.value();
}

/**
 * @brief 以固定协议结构尺寸识别完整 UDP 报文，并统计合法包的入口处理延时。
 * @details 只接受与 GeneralPackage 或 workStandby 完全等长的报文，避免部分包和版本漂移被误解析；
 *          未知长度给出与已知结构的近似诊断。只有已识别包进入延时统计。
 */
void RadarDataProcessor::onCommDataReceived(const QString& endpointId,
                                            const QByteArray& data,
                                            qint64 receivedMs,
                                            qint64 receivedSteadyNs)
{
    // 按大小分发
    const size_t sz = static_cast<size_t>(data.size());
    bool handled = false;

    if (sz == sizeof(GeneralPackage)) {
        GeneralPackage gp;
        memcpy(&gp, data.constData(), sizeof(GeneralPackage));
        handleGeneralPackage(endpointId, gp, receivedMs, receivedSteadyNs);
        handled = true;
    } else if (sz == sizeof(workStandby)) {
        workStandby cmd;
        memcpy(&cmd, data.constData(), sizeof(workStandby));
        handleWorkStandby(endpointId, cmd, receivedMs);
        handled = true;
    } else {
        qWarning() << QStringLiteral("[RadarDataProcessor] 未识别的数据长度: %1 字节, 端点='%2'")
                      .arg(sz).arg(endpointId);
        // 近匹配诊断：协议结构体改动后 sizeof 可能微变
        auto near = [sz](int expected) {
            return std::abs(static_cast<int>(sz) - expected) < 32;
        };
        if (near(sizeof(GeneralPackage)))
            qWarning() << "  → 接近 GeneralPackage(" << sizeof(GeneralPackage) << ")，检查协议结构体是否被修改";
        if (near(sizeof(workStandby)))
            qWarning() << "  → 接近 workStandby(" << sizeof(workStandby) << ")，检查协议结构体是否被修改";
    }

    // 记录上行处理延时（仅对已识别的合法包统计）
    if (handled) {
        if (m_latencyMonitor) {
            m_latencyMonitor->recordNs(PipelineLatencyStage::ProcessorTotal,
                                       monotonicNowNs() - receivedSteadyNs);
        }
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

/**
 * @brief 把厂家总包转换为带设备/任务归属的已识别权威航迹，并只发布一次。
 * @details 先从内存缓存解析归属并记录 Decode 延时，再生成 traceId 和标准字段；识别失败时不发布。
 *          成功后使用 shared_ptr<const TrackPacket> 让显示与存储分支共享同一不可变事实。
 */
void RadarDataProcessor::handleGeneralPackage(const QString& endpointId,
                                              const GeneralPackage& gp,
                                              qint64 receivedMs,
                                              qint64 receivedSteadyNs)
{
    m_generalPackageCount++;

    const TargetType& tgt = gp.h_c.TrackInfo;
    const uint32_t sceneNum = gp.datapackageheader.F_UI_Scene_Num;

    QString deviceId;
    if (m_deviceReg)
        deviceId = m_deviceReg->cachedDeviceIdForEndpoint(endpointId);

    QString taskId;
    if (m_taskService && !deviceId.isEmpty())
        taskId = m_taskService->resolveTaskIdForDevice(deviceId);

    const qint64 normalizedSteadyNs = monotonicNowNs();
    if (m_latencyMonitor) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::Decode,
                                   normalizedSteadyNs - receivedSteadyNs);
    }

    LogContext::Scope logScope(deviceId, taskId);

    // 厂家协议直接映射成标准航迹，完成同步识别后只发布一次。
    TrackPacketSourceContext context;
    context.traceId = ++m_traceSequence;
    context.endpointId = endpointId;
    context.deviceId = deviceId;
    context.taskId = taskId;
    context.receivedAtMs = receivedMs;
    context.ingressSteadyNs = receivedSteadyNs;
    context.normalizedSteadyNs = normalizedSteadyNs;
    context.sceneNumber = sceneNum;

    TrackPacket track = mapGeneralPackageToTrackPacket(gp, context);
    if (!m_trackRecognizer.recognize(track)) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 航迹识别失败, traceId=%1 trackId=%2")
                      .arg(track.traceId).arg(track.trackId);
        return;
    }

    auto sharedTrack = std::make_shared<const TrackPacket>(std::move(track));
    DataPacket packet;
    packet.sourceType     = resolveSourceType(endpointId);
    packet.dispatchTarget = DISPATCH_TRACK_DISPLAY_MANAGER;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(receivedMs);
    packet.traceId        = sharedTrack->traceId;
    packet.command        = DataCommand::Track;
    packet.payload        = std::move(sharedTrack);

    if (!SystemBus::getInstance().publish(std::move(packet))) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 发布标准航迹到总线失败, seq=%1")
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

/**
 * @brief 同时把雷达工作模式回显送入状态机和 SystemBus 诊断链路。
 * @details radarWorkModeEcho 用于结束命令等待状态；DataPacket 允许其他订阅者观察同一回显。
 */
void RadarDataProcessor::handleWorkStandby(const QString& endpointId,
                                           const workStandby& cmd,
                                           qint64 receivedMs)
{
    qDebug() << QStringLiteral("[RadarDataProcessor] 收到 workStandby: 端点='%1', 模式=%2")
                .arg(endpointId).arg(cmd.Work_Standby_Mode);

    // 转发到总线，同时通知状态机进行回显验证
    uint16_t devId = resolveSourceType(endpointId);
    emit radarWorkModeEcho(devId, cmd.Work_Standby_Mode);

    DataPacket packet;
    packet.sourceType     = devId;
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++m_sequence;
    packet.timeStamp      = static_cast<uint64_t>(receivedMs);
    packDataToPayload(packet, cmd);

    if (!SystemBus::getInstance().publish(std::move(packet))) {
        qWarning() << QStringLiteral("[RadarDataProcessor] 发布 workStandby 到总线失败, seq=%1")
                      .arg(m_sequence);
    }
}
