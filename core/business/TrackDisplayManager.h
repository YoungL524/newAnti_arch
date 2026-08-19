/**
 * @file TrackDisplayManager.h
 * @brief 声明活动任务范围内的有界航迹显示投影和修订增量发布器。
 * @details 本类消费不可变 TrackPacket，按活动任务/设备过滤后维护有限尾迹，并发布 TrackDisplayUpdate。
 *          它不解析厂家协议、不写数据库、不调用 Web/UI；Web 桥只消费其快照和增量。
 * @par 线程模型
 * SystemBus 回调在总线线程，只复制消息并排队回 QObject 线程；全部缓存只在 QObject 线程修改。
 */

#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <deque>
#include "TrackTypes.h"

struct DataPacket;
class TaskService;
class PipelineLatencyMonitor;

/**
 * @brief 显示窗口状态的唯一拥有者，负责容量、过期、交互和修订连续性。
 */
class TrackDisplayManager : public QObject {
    Q_OBJECT
public:
    /** @brief 创建空投影；缓存所属线程由 QObject 亲和性决定。 */
    explicit TrackDisplayManager(QObject* parent = nullptr);

    /** @brief 订阅权威航迹和交互命令，并启动一秒一次的显示刷新。 */
    void start();
    /** @brief 阻止新回调、取消两项总线订阅并停止刷新定时器。 */
    void stop();
    /** @brief 清空显示缓存和交互状态，并发布空的权威快照。 */
    void clear();
    /** @brief 强制发布完整快照，用于新 Web 连接或修订跳号后的重新同步。 */
    void publishReset();
    /**
 * @brief 注入活动任务和设备成员关系的热路径解析器。
 * @param[in] taskService 非拥有指针，必须比本对象存活更久。
 */
    void setTaskService(TaskService* taskService);
    /**
 * @brief 注入可选的分段延时监控器。
 * @param[in] latencyMonitor 非拥有指针；为空时不记录精细显示阶段指标。
 */
    void setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor);

private:
    /** @brief 刷新活动任务/设备集合；上下文变化时清空旧任务投影并要求快照。 */
    void syncActiveTaskContext();
    /**
 * @brief 判断航迹是否已识别且属于当前活动任务授权设备。
 */
    bool isTrackVisible(const TrackPacket& track) const;
    /**
 * @brief 追加一个显示点、分配稳定 pointIndex/全局序号，并立即执行容量约束。
 */
    void addTrack(const TrackPacket& track);
    /**
 * @brief 校验 Track DataPacket 后，把不可变载荷排队到缓存所属线程。
 */
    void onTrackPacket(const DataPacket& packet);
    /**
 * @brief 更新指定航迹的选中/跟踪状态；任务不匹配时拒绝跨任务修改。
 */
    void setInteraction(const QString& taskId, const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);
    /**
 * @brief 校验并排队处理 Web API 发布的 TrackInteractionCommand。
 */
    void onInteractionCommand(const DataPacket& packet);
    /**
 * @brief 根据点龄和交互状态派生颜色、大小、透明度，不修改权威航迹。
 */
    void applyStyle(TrackDisplayPoint& dp, uint32_t pointIndex,
                    const TrackDisplayPoint::Interaction& interaction) const;
    /**
 * @brief 删除最后更新时间超过显示过期阈值的整条航迹及其交互状态。
 */
    void pruneStaleTracks(qint64 nowMs);
    /**
 * @brief 按全局 FIFO 淘汰最旧显示点，直到所有设备总点数回到固定上限。
 */
    void enforceGlobalCacheBudget();
    /**
 * @brief 清空任务相关缓存，但保留已发布窗口直到重置快照成功送达。
 */
    void resetCacheState();
    /**
 * @brief 构建下一修订并发布；总线拒绝时保持旧基线并强制下次使用快照恢复。
 */
    void publishCurrent(bool forceSnapshot = false);
    /**
 * @brief 构建完整窗口或相对上次成功发布窗口的 upsert/removal 差异。
 * @param[in] nowMs 输出事件的 Unix 毫秒时间。
 * @param[in] forceSnapshot 为 true 时发送全部可见点且不生成 removals。
 * @param[out] nextPublished 接收本次更新所代表的精确窗口，发布成功后才成为新基线。
 * @return 尚未分配 revision 的显示更新。
 */
    TrackDisplayUpdate buildUpdate(
        qint64 nowMs, bool forceSnapshot,
        QHash<TrackDisplayPointKey, TrackDisplayPoint>& nextPublished) const;
    /**
 * @brief 比较全部 Web 可见字段，避免重发没有可观察变化的点。
 */
    static bool sameDisplayPoint(const TrackDisplayPoint& left,
                                 const TrackDisplayPoint& right);

    /**
 * @brief 权威航迹与显示排序、就绪时间组成的缓存节点。
 */
    struct CachedPoint {
        TrackPacket track;               ///< 复制到缓存线程的不可变业务字段。
        uint32_t pointIndex = 0;         ///< 单航迹绝对点序号，FIFO 淘汰后不复用。
        uint64_t sequence = 0;           ///< 全设备范围的插入顺序，用于公平淘汰。
        qint64 displayReadySteadyNs = 0; ///< 完成显示处理的单调纳秒。
    };
    /**
 * @brief 单设备/航迹的有序尾迹及其下一点号。
 */
    struct TrackCache {
        std::deque<CachedPoint> points; ///< 从旧到新的保留尾迹。
        uint32_t nextPointIndex = 0;   ///< 下一绝对点序号。
        qint64 lastUpdateMs = 0;       ///< Unix 毫秒，仅用于过期判断。
    };
    /**
 * @brief 全局 FIFO 的轻量索引项，用 sequence 检测已失效条目。
 */
    struct PointOrderEntry {
        TrackKey key;          ///< 该点所属缓存键。
        uint64_t sequence = 0; ///< 预期位于该缓存队首的全局序号。
    };

    QHash<TrackKey, TrackCache> m_caches; ///< 当前任务各航迹尾迹。
    QHash<TrackKey, TrackDisplayPoint::Interaction> m_interactions; ///< UI 交互权威状态。
    QHash<TrackDisplayPointKey, TrackDisplayPoint> m_publishedPoints; ///< 上次成功发布的窗口基线。
    std::deque<PointOrderEntry> m_pointOrder; ///< 全局从旧到新的淘汰索引。
    uint64_t m_nextPointSequence = 0; ///< 下一全局插入序号。
    int m_cachedPointCount = 0; ///< 所有尾迹总点数，受实现上限约束。
    uint64_t m_displayRevision = 0; ///< 上次成功发布的修订号。
    bool m_forceSnapshot = true; ///< 重置或发布失败后的恢复闩锁。
    QTimer* m_timer = nullptr; ///< 本对象拥有的一秒刷新定时器。
    uint32_t m_cmdSubId = 0; ///< 交互命令订阅 ID。
    uint32_t m_trackSubId = 0; ///< 权威航迹订阅 ID。
    std::atomic_bool m_acceptCommands{false}; ///< 跨线程关闭闩锁，阻止已复制回调继续排队。
    TaskService* m_taskService = nullptr; ///< 非拥有活动任务解析器。
    PipelineLatencyMonitor* m_latencyMonitor = nullptr; ///< 非拥有可选延时采集器。
    QString m_activeTaskId; ///< 当前投影所属任务 ID。
    QSet<QString> m_activeDeviceIds; ///< 当前任务允许显示的设备集合。
};
