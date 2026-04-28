#pragma once

#include <QObject>
#include <QMap>
#include <cstdint>
#include "StructureReceive.h"
#include "GenericPackets.h"

class CommunicationManager;
class DataStore;

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

public slots:
    /**
     * @brief 处理来自通信层的原始数据
     * @param endpointId 端点 ID
     * @param data       原始字节流
     * @details 按数据大小分发到相应解析路径。解析失败或未知大小的包会记录警告。
     */
    void onCommDataReceived(const QString& endpointId, const QByteArray& data);

private:
    /**
     * @brief 处理 GeneralPackage（完整业务数据包）
     * @details 写入 tracks 表 + 发布到 SystemBus
     */
    void handleGeneralPackage(const QString& endpointId, const GeneralPackage& gp);

    /**
     * @brief 处理独立的 workStandby（通常是对端回显；核心场景下不常见）
     */
    void handleWorkStandby(const QString& endpointId, const workStandby& cmd);

    /**
     * @brief 查询端点对应的 sourceType
     */
    uint16_t resolveSourceType(const QString& endpointId) const;

    /**
     * @brief 将 TargetType 写入 tracks 表
     * @param tsMs       记录时间戳（毫秒）
     * @param endpointId 来源端点 ID（存入表的 source 字段）
     * @param tgt        航迹数据
     * @param sceneNum   景幅号（来自 DPH 或 0）
     */
    void insertTrack(qint64 tsMs,
                     const QString& endpointId,
                     const TargetType& tgt,
                     uint32_t sceneNum);

private:
    CommunicationManager* m_commManager = nullptr;   ///< 通信管理器（非持有）
    DataStore* m_dataStore = nullptr;                ///< 数据存储（非持有）

    QMap<QString, uint16_t> m_endpointSourceMap;     ///< 端点ID → DeviceType 映射
    uint32_t m_sequence = 0;                         ///< DataPacket 发布序列号
    uint64_t m_generalPackageCount = 0;              ///< 已处理的 GeneralPackage 计数
    uint64_t m_trackInsertCount = 0;                 ///< 已写入 tracks 表的记录数
};
