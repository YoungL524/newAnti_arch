#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <cstdint>
#include "TrackTypes.h"

struct BusEnvelope;
class TaskService;

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

private:
    void syncActiveTaskContext();
    bool isPointVisible(const TrackPoint& pt) const;
    void setInteraction(const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);
    void onInteractionCommand(const BusEnvelope& env);
    void applyStyle(TrackDisplayPoint& dp, uint32_t pointIndex,
                    const TrackDisplayPoint::Interaction& interaction) const;
    void pruneStaleTracks(qint64 nowMs);
    void deliverSnapshot(TrackDisplaySnapshot&& snap);

    struct CachedPoint {
        TrackPoint point;
        uint32_t pointIndex = 0;
    };
    struct TrackCache {
        QVector<CachedPoint> points;
        uint32_t nextPointIndex = 0;
        qint64 lastUpdateMs = 0;
    };

    QHash<TrackKey, TrackCache> m_caches;
    QHash<TrackKey, TrackDisplayPoint::Interaction> m_interactions;
    QTimer* m_timer = nullptr;
    uint32_t m_cmdSubId = 0;
    std::atomic_bool m_acceptCommands{false};
    TaskService* m_taskService = nullptr;
    QString m_activeTaskId;
    QSet<QString> m_activeDeviceIds;
};
