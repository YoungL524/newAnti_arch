/**
 * @file RadarDataProcessor.h
 * @brief 声明雷达上行链路中从 UDP 报文到权威 TrackPacket 的同步处理器。
 * @details 输入是 CommunicationManager 汇聚的完整 UDP 数据报和接收时间；输出是发布到 SystemBus 的
 *          DataPacket::Track，另有 workStandby 回显信号。处理器通过 DeviceRegistry 和 TaskService
 *          在入口处确定设备/任务归属，之后映射、识别、显示、存储都沿用同一上下文。
 */

#pragma once

#include <QObject>
#include <QMap>
#include <cstdint>
#include "StructureReceive.h"
#include "GenericPackets.h"
#include "TrackTypes.h"
#include "LatencyStats.h"
#include "TrackRecognizer.h"

class CommunicationManager;
class DeviceRegistry;
class TaskService;
class PipelineLatencyMonitor;

/**
 * @class RadarDataProcessor
 * @brief 雷达数据处理器（正向流：通信层 → 标准航迹 → 总线）
 * @details 阶段 A 重构的核心组件，负责正向数据流职责：
 *  1. 订阅 `CommunicationManager::dataReceived`，按 endpointId 识别数据源设备；
 *  2. 依据包大小解析厂家 `GeneralPackage` / `workStandby`；
 *  3. 将厂家航迹直接映射、识别并发布 `DataPacket{DataCommand::Track}`；
 *  4. 不执行数据库 I/O；归档由总线后的独立 TrackStorage 消费者完成。
 *
 * 本类不继承 QThread —— 完全运行在 Qt 事件循环中。若未来某个解析步骤耗时 >10ms，
 * 再单独把本 QObject `moveToThread` 到专属工作线程即可，**无需**改业务代码。
 */
class RadarDataProcessor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建处理器并订阅 CommunicationManager::dataReceived。
     * @param[in] commManager 非拥有通信门面，不能为空且必须比本对象存活更久。
     * @param[in] parent Qt 父对象，决定本处理器及其槽函数的线程亲和性。
     */
    explicit RadarDataProcessor(CommunicationManager* commManager,
                                QObject* parent = nullptr);

    /** @brief 断开由 Qt 管理的连接；不关闭通信端点或 SystemBus。 */
    ~RadarDataProcessor() override;

    /**
     * @brief 关联 DeviceRegistry（用于 endpointId → 业务 deviceId 映射）
     */
    void setDeviceRegistry(DeviceRegistry* reg);

    /** @brief 设置非拥有任务服务，用于把 deviceId 解析为接收时的活动任务。 */
    void setTaskService(TaskService* taskService);
    /** @brief 设置可选的进程级分段延时聚合器；为空时仍保留本地上行统计。 */
    void setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor);

    /**
     * @brief 注册端点到设备源类型的映射
     * @param endpointId 端点唯一标识（与 endpoints.json 中的 id 对应）
     * @param sourceType DeviceType 枚举值（见 GenericPackets.h::DeviceType）
     * @details 调用前所有端点的 sourceType 默认为 DEVICE_UNKNOWN。
     *          新增厂家时在 AppController 里注册一次即可，本类保持通用。
     */
    void registerEndpointSource(const QString& endpointId, uint16_t sourceType);

    /**
     * @brief 获取累计处理的 GeneralPackage 数（用于诊断/状态显示）
     */
    uint64_t generalPackageCount() const { return m_generalPackageCount; }

    /**
     * @brief 上行链路延时统计（UDP 接收到 → Processor 处理完成）
     */
    const LatencyStats& uplinkLatency() const { return m_uplinkLatency; }

signals:
    /**
     * @brief 收到雷达回显的 workStandby 模式
     * @param deviceId 设备 ID
     * @param mode     回显的 Work_Standby_Mode (1=工作, 0=待机)
     */
    void radarWorkModeEcho(uint16_t deviceId, uint32_t mode);

public slots:
    /**
     * @brief 处理来自通信层的原始数据
     * @param endpointId 端点 ID
     * @param data       原始字节流
     * @param receivedMs 数据到达时间戳（由 UdpEndpoint 在 readDatagram 后捕获）
     * @param[in] receivedSteadyNs readDatagram 后捕获的单调时钟纳秒值，仅用于进程内耗时。
     * @details 以协议结构尺寸作为第一层分派条件，再用 memcpy 解析固定布局。未知尺寸不会截断或猜测协议，
     *          而是记录告警后丢弃，从而避免错误字节污染统一数据模型。
     */
    void onCommDataReceived(const QString& endpointId, const QByteArray& data,
                            qint64 receivedMs, qint64 receivedSteadyNs);

private:
    /**
     * @brief 处理 GeneralPackage（完整业务数据包）
     * @param endpointId 来源端点 ID
     * @param gp         协议包
     * @param receivedMs 数据到达时间戳（用于 latency 基准 + DataPacket.timeStamp）
     */
    void handleGeneralPackage(const QString& endpointId, const GeneralPackage& gp,
                              qint64 receivedMs, qint64 receivedSteadyNs);

    /**
     * @brief 处理独立的 workStandby（通常是对端回显；核心场景下不常见）
     */
    void handleWorkStandby(const QString& endpointId, const workStandby& cmd, qint64 receivedMs);

    /**
     * @brief 查询端点对应的 sourceType
     */
    uint16_t resolveSourceType(const QString& endpointId) const;

private:
    CommunicationManager* m_commManager = nullptr;   ///< 通信管理器（非持有）
    DeviceRegistry* m_deviceReg = nullptr;            ///< 设备台账（非持有，用于 endpoint→deviceId 映射）
    TaskService* m_taskService = nullptr;              ///< 任务服务（非持有，用于航迹归属）
    PipelineLatencyMonitor* m_latencyMonitor = nullptr; ///< 非持有，进程级时延聚合器
    TrackRecognizer m_trackRecognizer;                  ///< 最小同步识别器，发布前必经

    QMap<QString, uint16_t> m_endpointSourceMap;     ///< 端点ID → DeviceType 映射
    uint32_t m_sequence = 0;                         ///< 本处理器发布 DataPacket 的递增序号，允许自然回绕。
    uint64_t m_traceSequence = 0;                    ///< 为每条标准航迹分配的进程内递增 traceId。
    uint64_t m_generalPackageCount = 0;              ///< 成功进入 GeneralPackage 处理路径的累计报文数。
    // ---- 延时监测 ----
    LatencyStats m_uplinkLatency;                    ///< 上行链路延时统计（UDP 接收到 → Processor 处理完成）
};
