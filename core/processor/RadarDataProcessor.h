#pragma once

#include <QObject>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QVariantMap>
#include <cstdint>
#include "StructureReceive.h"
#include "GenericPackets.h"
#include "TrackTypes.h"
#include "LatencyStats.h"

class CommunicationManager;
class DataStore;
class DeviceRegistry;
class TaskService;
class PipelineLatencyMonitor;

/**
 * @file RadarDataProcessor.h
 * @brief 雷达数据处理器（正向流：通信层 → 业务 / 总线 / 存储）
 * @details 阶段 A 重构的核心组件，负责正向数据流职责：
 *  1. 订阅 `CommunicationManager::dataReceived`，按 endpointId 识别数据源设备；
 *  2. 依据包大小（必要时后续扩展为协议头字段）解析 `GeneralPackage` / `workStandby` 等；
 *  3. 发布统一的 `DataPacket` 到 `SystemBus`（载荷类型见 GenericPackets.h）；
 *  4. 业务级存储：航迹数据写入 `tracks` 表（带 target_id / range / azimuth 等字段）。
 *
 * 本类不继承 QThread —— 完全运行在 Qt 事件循环中。若未来某个解析步骤耗时 >10ms，
 * 再单独把本 QObject `moveToThread` 到专属工作线程即可，**无需**改业务代码。
 */
class RadarDataProcessor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param commManager 通信管理器（生命周期由 AppController 管理，不为空）
     * @param dataStore   数据存储（可为 nullptr；为空或未连接时跳过写库）
     * @param parent      Qt parent
     */
    RadarDataProcessor(CommunicationManager* commManager,
                       DataStore* dataStore,
                       QObject* parent = nullptr);

    ~RadarDataProcessor() override;

    /**
     * @brief 关联 DeviceRegistry（用于 endpointId → 业务 deviceId 映射）
     */
    void setDeviceRegistry(DeviceRegistry* reg);

    void setTaskService(TaskService* taskService);
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
     * @brief 获取累计写入 tracks 表的记录数
     */
    uint64_t trackInsertCount() const { return m_trackInsertCount; }

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

    /**
     * @brief 输出解析后的业务航迹点（含 deviceId）
     * @param pt 业务航迹点
     */
    void trackPointReady(const TrackPoint& pt);

public slots:
    /**
     * @brief 处理来自通信层的原始数据
     * @param endpointId 端点 ID
     * @param data       原始字节流
     * @param receivedMs 数据到达时间戳（由 UdpEndpoint 在 readDatagram 后捕获）
     * @details 按数据大小分发到相应解析路径。解析失败或未知大小的包会记录警告。
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

    /**
     * @brief 将 TargetType 追加到写缓冲
     */
    void bufferTrack(qint64 tsMs,
                     const QString& endpointId,
                     const QString& deviceId,
                     const QString& taskId,
                     const TargetType& tgt,
                     uint32_t sceneNum);

    /**
     * @brief 将缓冲中的航迹批量写入数据库（事务内）
     */
    void flushTracks();

private:
    CommunicationManager* m_commManager = nullptr;   ///< 通信管理器（非持有）
    DataStore* m_dataStore = nullptr;                ///< 数据存储（非持有）
    DeviceRegistry* m_deviceReg = nullptr;            ///< 设备台账（非持有，用于 endpoint→deviceId 映射）
    TaskService* m_taskService = nullptr;              ///< 任务服务（非持有，用于航迹归属）
    PipelineLatencyMonitor* m_latencyMonitor = nullptr; ///< 非持有，进程级时延聚合器

    QMap<QString, uint16_t> m_endpointSourceMap;     ///< 端点ID → DeviceType 映射
    uint64_t m_trackArchiveSkipCount = 0;            ///< 未归属当前任务而跳过归档的计数
    uint64_t m_trackArchiveCount = 0;                ///< 已缓冲待写入的任务归属航迹计数
    uint32_t m_sequence = 0;                         ///< DataPacket 发布序列号
    uint64_t m_traceSequence = 0;                    ///< 航迹链路 trace id
    uint64_t m_generalPackageCount = 0;              ///< 已处理的 GeneralPackage 计数
    uint64_t m_trackInsertCount = 0;                 ///< 已写入 tracks 表的记录数

    // ---- 批量写缓冲 ----
    QVector<QVariantMap> m_trackBuffer;              ///< 航迹写缓冲，攒批 flush
    QTimer* m_flushTimer = nullptr;                  ///< 定期 flush 定时器（防饥饿）
    static constexpr int kFlushThreshold = 20;       ///< 缓冲阈值（条），达到后自动 flush
    static constexpr int kFlushIntervalMs = 200;     ///< 最大 flush 间隔（ms）

    // ---- 延时监测 ----
    LatencyStats m_uplinkLatency;                    ///< 上行链路延时统计（UDP 接收到 → Processor 处理完成）
};
