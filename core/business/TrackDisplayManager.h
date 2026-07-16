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

struct BusEnvelope;
class TaskService;
class PipelineLatencyMonitor;

class TrackDisplayManager : public QObject {
    Q_OBJECT
public:
    explicit TrackDisplayManager(QObject* parent = nullptr);

    void addPoint(const TrackPoint& pt);
    TrackDisplaySnapshot snapshot() const;
    /// 显式传时间戳（timer 内部调用，避免两次 syscall）
    TrackDisplaySnapshot snapshot(qint64 nowMs) const;
    void start();
    void stop();
    void clear();
    void setTaskService(TaskService* taskService);
    void setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor);

private:
    void syncActiveTaskContext();
    bool isPointVisible(const TrackPoint& pt) const;
    void setInteraction(const QString& taskId, const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);
    void onInteractionCommand(const BusEnvelope& env);
    void applyStyle(TrackDisplayPoint& dp, uint32_t pointIndex,
                    const TrackDisplayPoint::Interaction& interaction) const;
    void pruneStaleTracks(qint64 nowMs);
    void enforceGlobalCacheBudget();
    void resetCacheState();
    void deliverSnapshot(TrackDisplaySnapshot&& snap);

    struct CachedPoint {
        TrackPoint point;
        uint32_t pointIndex = 0;
        uint64_t sequence = 0;
    };
    struct TrackCache {
        std::deque<CachedPoint> points;
        uint32_t nextPointIndex = 0;
        qint64 lastUpdateMs = 0;
    };
    struct PointOrderEntry {
        TrackKey key;
        uint64_t sequence = 0;
    };

    QHash<TrackKey, TrackCache> m_caches;
    QHash<TrackKey, TrackDisplayPoint::Interaction> m_interactions;
    std::deque<PointOrderEntry> m_pointOrder;
    uint64_t m_nextPointSequence = 0;
    int m_cachedPointCount = 0;
    QTimer* m_timer = nullptr;
    uint32_t m_cmdSubId = 0;
    std::atomic_bool m_acceptCommands{false};
    TaskService* m_taskService = nullptr;
    PipelineLatencyMonitor* m_latencyMonitor = nullptr;
    QString m_activeTaskId;
    QSet<QString> m_activeDeviceIds;
};
