/**
 * @file TrackWebSocketService.h
 * @brief 定义从共享 TrackTopic 主动读取当前采集会话航迹的 WebReader。
 * @details 上游是 CoreDataDomain::trackTopic 的 Doorbell 与 readAfter()；下游是浏览器的初始 snapshot 和批次
 * delta。服务只保存 cursor、固定 ScratchBuffer 及每个活跃目标的近期显示点，不保存第二份 Topic 历史，
 * 不解析 UDP、不处理 Command、不为每个浏览器建立业务消息队列；新人应从 consumeOneBatch() 阅读数据流。
 */

#pragma once

#include "core/datadomain/HistoryTopic.h"
#include "core/datadomain/LatestState.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/TrackPacket.h"
#include "core/model/RuntimeState.h"
#include "core/model/StableIdRegistry.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <array>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

class WebSocketGateway;

/**
 * @brief 将当前采集会话观测折叠为浏览器近期显示态，并通过唯一 Gateway 发送快照或增量。
 * @warning 除只执行Lane原子置位的onTrackTopicAdvanced外，方法均须在所属线程调用；依赖必须覆盖本对象。
 * @note 最多8连接；增量发送积压超过4MiB断开慢连接，重连后取快照。
 */
class TrackWebSocketService final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建不拥有网络端口的WebReader。
     * @param trackTopic 数据域唯一航迹历史。
     * @param deviceState 设备 Owner 写入的采集会话状态；只读取，不修改命令或设备状态。
     * @param executionLane Web/主事件循环共享的合并执行通道。
     * @param gateway 页面实时接口的唯一连接owner。
     * @param stableIds 把热路径紧凑句柄解析为跨重启 UUID 的只读注册表。
     * @param maxRecordsPerTurn 每次调度最多读取的 Topic 记录数，必须大于零。
     * @param parent Qt 生命周期父对象。
     */
    TrackWebSocketService(
        const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
        const core::domain::LatestState<core::model::DeviceState> &deviceState,
        core::execution::ExecutionLane &executionLane,
        WebSocketGateway &gateway,
        const core::model::StableIdRegistry &stableIds,
        qsizetype maxRecordsPerTurn,
        QObject *parent = nullptr);

    /** @brief 断开借用依赖并释放ExecutionLane slot；必须在所属线程析构。 */
    ~TrackWebSocketService() override;

    /** @brief 返回已经成功折叠的最后一个 TrackTopic sequence。 */
    core::domain::TopicSequence cursor() const noexcept;

public Q_SLOTS:
    /**
     * @brief 接收航迹 Topic 或 DeviceState 只含水位的 Doorbell，并向共享 ExecutionLane 置位。
     * @param newestSequence 提示水位；真实航迹顺序与 gap 始终由 readAfter() 判定。
     */
    void onTrackTopicAdvanced(quint64 newestSequence);

    /** @brief 在启动场景主动检查 Topic，并安排一次有界读取。 */
    void refresh();

private Q_SLOTS:
    /** @brief 响应共享 Lane 位集合，仅在命中本 Reader 时读取一个批次。 */
    void onExecutionRequested(quint64 pendingMask);

private:
    /** @brief Web 序列化边界保存的一条最终地图点，不携带 Topic 元数据或动态字符串。 */
    struct DisplayPoint
    {
        core::model::AcquisitionId acquisitionId; ///< 隔离重启后 trackId 复用的采集会话。
        core::model::DeviceId deviceId; ///< 用于在 Web 边界解析为稳定设备标识。
        quint32 trackId = 0; ///< 厂家航迹号。
        quint8 faceIndex = 0; ///< 完整雷达内的阵面索引，范围0～3。
        quint32 pointIndex = 0; ///< 当前航迹内单调显示点序号。
        qint64 timestampUtcMs = 0; ///< socket 接收时 UTC 毫秒。
        double rangeM = 0.0; ///< 斜距，单位 m。
        double azimuthRad = 0.0; ///< 局部方位角，单位 rad。
        double elevationRad = 0.0; ///< 局部俯仰角，单位 rad。
        double radialVelocityMps = 0.0; ///< 抵近为正、远离为负，单位 m/s。
        double amplitude = 0.0; ///< 厂家未标定幅值原值。
    };

    /** @brief 每条活跃航迹只保留最近 20 点，旧路径由浏览器简化为显示形状。 */
    struct TrackTrail
    {
        std::array<DisplayPoint, 20> points{}; ///< 固定槽位，不随航迹持续时间增长。
        qsizetype size = 0; ///< 已填充槽位数，范围 [0,20]。
        qsizetype nextSlot = 0; ///< 下一次覆盖槽位，范围 [0,19]。
        quint32 nextPointIndex = 0; ///< 下一个浏览器排序序号；溢出时本航迹重建。
    };

    /** @brief 从 Topic 读取一个固定容量批次，返回仍有未读取记录时为 true。 */
    bool consumeOneBatch();

    /**
     * @brief 折叠一条权威航迹并向 upserts/removals 追加本批 Web 变化。
     * @param record 当前按 sequence 排序的 Topic 记录。
     * @param upserts 本批新增或覆盖的最终显示点 JSON 数组。
     * @param removals 本批被固定尾迹覆盖或目标消亡的显示键 JSON 数组。
     * @param retirements 本批 packFlag=0 目标键，供浏览器清除已压缩的旧路径。
     */
    void applyRecord(
        const core::domain::TopicRecord<core::model::TrackPacket> &record,
        QJsonArray &upserts,
        QJsonArray &removals,
        QJsonArray &retirements);

    /** @brief 把定长最终显示点及其稳定身份转换为浏览器跨进程 JSON 合同。 */
    QJsonObject pointJson(const DisplayPoint &point) const;

    /** @brief 只序列化删除所需稳定身份字段，避免 removal 携带业务值。 */
    QJsonObject pointKeyJson(const DisplayPoint &point) const;

    /** @brief 检查航迹的设备和采集会话均已由 SQLite Owner 登记稳定 UUID。 */
    bool hasStableIdentity(const core::model::TrackPacket &track) const;

    /** @brief 向一个新客户端发送当前有界显示态的完整 snapshot。 */
    void sendSnapshot(QWebSocket &client) const;

    /** @brief 向所有当前客户端发送一个批次 delta，并区分尾迹覆盖与目标消亡。 */
    void broadcastDelta(const QJsonArray &upserts, const QJsonArray &removals,
                        const QJsonArray &retirements);

    const core::domain::HistoryTopic<core::model::TrackPacket> &m_trackTopic; ///< 借用的唯一航迹历史。
    const core::domain::LatestState<core::model::DeviceState> &m_deviceState; ///< 借用采集会话状态，不参与命令控制。
    core::execution::ExecutionLane &m_executionLane; ///< 借用的 Web/主循环合并执行通道。
    WebSocketGateway &m_gateway; ///< 借用唯一WebSocket连接owner，不保存业务历史。
    const core::model::StableIdRegistry &m_stableIds; ///< 借用稳定身份注册表，不访问 SQLite 或缓存字符串。
    core::domain::TopicScratchBuffer<core::model::TrackPacket> m_scratch; ///< 固定可复用读取缓冲。
    const core::execution::ExecutionSlot m_executionSlot; ///< 本 Reader 在 Lane 中的唯一 pending 位。
    core::model::DeviceId m_currentDeviceId; ///< 当前采集雷达；零表示待机。
    std::array<core::model::AcquisitionId, 4> m_currentAcquisitionIds {}; ///< 当前各阵面会话；变化时清除旧显示态。
    QHash<core::model::TrackKey, TrackTrail> m_trails; ///< 每目标近期 20 点；目标消亡或采集停止时删除，不预设目标数上限。
    core::domain::TopicSequence m_cursor = 0; ///< 本 Consumer 独立读取水位。
    quint64 m_revision = 0; ///< Web snapshot/delta 单调修订号。
};

} // namespace core::web
