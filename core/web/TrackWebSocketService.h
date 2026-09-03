/**
 * @file TrackWebSocketService.h
 * @brief 定义从共享 TrackTopic 主动读取并输出有界 WebSocket 航迹当前态的 WebReader。
 * @details 上游是 CoreDataDomain::trackTopic 的 Doorbell 与 readAfter()；下游是浏览器的初始 snapshot 和批次
 * delta。服务只保存 cursor、固定 ScratchBuffer 及地图绘制必需的有界最终状态，不保存第二份 Topic 历史，
 * 不解析 UDP、不处理 Command、不为每个浏览器建立业务消息队列；新人应从 consumeOneBatch() 阅读数据流。
 */

#pragma once

#include "core/datadomain/HistoryTopic.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/TrackPacket.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QWebSocketServer>

#include <array>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

/**
 * @brief 将 TrackTopic 折叠为有界浏览器显示态，并向所有已连接客户端发送快照或增量。
 * @warning 所有公开方法和 Qt 槽必须在对象所属线程调用；Topic 与 ExecutionLane 生命周期必须覆盖本对象。
 */
class TrackWebSocketService final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建尚未监听端口的 WebReader。
     * @param trackTopic 数据域唯一航迹历史。
     * @param executionLane Web/主事件循环共享的合并执行通道。
     * @param maxRecordsPerTurn 每次调度最多读取的 Topic 记录数，必须大于零。
     * @param maxTargets Web 最终状态最多保留的不同 TrackKey 数量，必须大于零。
     * @param parent Qt 生命周期父对象。
     */
    TrackWebSocketService(
        const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
        core::execution::ExecutionLane &executionLane,
        qsizetype maxRecordsPerTurn,
        qsizetype maxTargets,
        QObject *parent = nullptr);

    /** @brief 停止监听并释放 ExecutionLane slot；必须在所属线程析构。 */
    ~TrackWebSocketService() override;

    /**
     * @brief 在所有本机/网络接口监听 WebSocket 端口。
     * @param port 浏览器连接端口；零可用于自动化测试的临时端口。
     * @return 监听成功返回 true，重复启动或端口占用返回 false。
     */
    bool start(quint16 port);

    /** @brief 关闭监听和现有客户端连接，可重复调用。 */
    void stop();

    /** @brief 返回操作系统实际分配的 WebSocket 端口，未监听时为零。 */
    quint16 serverPort() const noexcept;

    /** @brief 返回已经成功折叠的最后一个 TrackTopic sequence。 */
    core::domain::TopicSequence cursor() const noexcept;

    /** @brief 返回当前连接的浏览器客户端数。 */
    qsizetype clientCount() const noexcept;

public Q_SLOTS:
    /**
     * @brief 接收只含水位的 Topic Doorbell，并向共享 ExecutionLane 置位。
     * @param newestSequence 提示水位；真实顺序与 gap 始终由 readAfter() 判定。
     */
    void onTrackTopicAdvanced(quint64 newestSequence);

    /** @brief 在启动或连接补偿场景主动检查 Topic，并安排一次有界读取。 */
    void refresh();

private Q_SLOTS:
    /** @brief 接纳一个 WebSocket 客户端并立即发送当前完整快照。 */
    void onNewConnection();

    /** @brief 响应共享 Lane 位集合，仅在命中本 Reader 时读取一个批次。 */
    void onExecutionRequested(quint64 pendingMask);

private:
    /** @brief Web 序列化边界保存的一条最终地图点，不携带 Topic 元数据或动态字符串。 */
    struct DisplayPoint
    {
        core::model::AcquisitionId acquisitionId; ///< 隔离重启后 trackId 复用的采集会话。
        core::model::DeviceId deviceId; ///< 用于在 Web 边界解析为稳定设备标识。
        core::model::TaskId taskId; ///< 用于 UI 当前任务过滤；零表示无任务。
        quint32 trackId = 0; ///< 厂家航迹号。
        quint32 pointIndex = 0; ///< 当前航迹内单调显示点序号。
        qint64 timestampUtcMs = 0; ///< socket 接收时 UTC 毫秒。
        double rangeM = 0.0; ///< 斜距，单位 m。
        double azimuthRad = 0.0; ///< 局部方位角，单位 rad。
        double elevationRad = 0.0; ///< 局部俯仰角，单位 rad。
        double radialVelocityMps = 0.0; ///< 抵近为正、远离为负，单位 m/s。
        double amplitude = 0.0; ///< 厂家未标定幅值原值。
    };

    /** @brief 每条航迹最多保留固定 20 点的最终 Web 绘制状态。 */
    struct TrackTrail
    {
        std::array<DisplayPoint, 20> points{}; ///< 固定槽位，不随航迹持续时间增长。
        qsizetype size = 0; ///< 已填充槽位数，范围 [0,20]。
        qsizetype nextSlot = 0; ///< 下一次覆盖槽位，范围 [0,19]。
        quint32 nextPointIndex = 0; ///< 下一个浏览器排序序号；溢出时本航迹重建。
        core::domain::TopicSequence lastSequence = 0; ///< 全局容量淘汰使用的最近访问水位。
    };

    /** @brief 从 Topic 读取一个固定容量批次，返回仍有未读取记录时为 true。 */
    bool consumeOneBatch();

    /**
     * @brief 折叠一条权威航迹并向 upserts/removals 追加本批 Web 变化。
     * @param record 当前按 sequence 排序的 Topic 记录。
     * @param upserts 本批新增或覆盖的最终显示点 JSON 数组。
     * @param removals 本批被固定尾迹或目标容量淘汰的显示键 JSON 数组。
     */
    void applyRecord(
        const core::domain::TopicRecord<core::model::TrackPacket> &record,
        QJsonArray &upserts,
        QJsonArray &removals);

    /** @brief 在达到 maxTargets 时淘汰最久未更新航迹，并追加其全部删除键。 */
    void evictOldestTrack(QJsonArray &removals);

    /** @brief 把定长最终显示点转换为浏览器跨进程 JSON 合同。 */
    static QJsonObject pointJson(const DisplayPoint &point);

    /** @brief 只序列化删除所需身份字段，避免 removal 携带业务值。 */
    static QJsonObject pointKeyJson(const DisplayPoint &point);

    /** @brief 向一个新客户端发送当前有界显示态的完整 snapshot。 */
    void sendSnapshot(QWebSocket &client) const;

    /** @brief 向所有当前客户端发送一个批次 delta；无客户端时不序列化。 */
    void broadcastDelta(const QJsonArray &upserts, const QJsonArray &removals);

    const core::domain::HistoryTopic<core::model::TrackPacket> &m_trackTopic; ///< 借用的唯一航迹历史。
    core::execution::ExecutionLane &m_executionLane; ///< 借用的 Web/主循环合并执行通道。
    core::domain::TopicScratchBuffer<core::model::TrackPacket> m_scratch; ///< 固定可复用读取缓冲。
    const core::execution::ExecutionSlot m_executionSlot; ///< 本 Reader 在 Lane 中的唯一 pending 位。
    const qsizetype m_maxTargets; ///< 有界最终状态目标数上限。
    QWebSocketServer m_server; ///< WebSocket 网络边界；不拥有 Topic 数据。
    QSet<QWebSocket *> m_clients; ///< 当前连接对象集合，不含业务消息队列。
    QHash<core::model::TrackKey, TrackTrail> m_trails; ///< 地图绘制所需的有界最终派生状态。
    core::domain::TopicSequence m_cursor = 0; ///< 本 Consumer 独立读取水位。
    quint64 m_revision = 0; ///< Web snapshot/delta 单调修订号。
};

} // namespace core::web
