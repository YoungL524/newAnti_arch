#pragma once

#include <QObject>
#include <QHash>
#include <QTimer>
#include <QVector>
#include "TrackTypes.h"

class TrackDisplayManager : public QObject {
    Q_OBJECT
public:
    explicit TrackDisplayManager(QObject* parent = nullptr);

    void addPoint(const TrackPoint& pt);
    TrackDisplaySnapshot snapshot() const;
    /// 显式传时间戳（timer 内部调用，避免两次 syscall）
    TrackDisplaySnapshot snapshot(qint64 nowMs) const;
    void setInteraction(const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);
    void start();
    void stop();
    void clear();

signals:
    void snapshotReady(const TrackDisplaySnapshot& snapshot);

private:
    void applyStyle(TrackDisplayPoint& dp, uint32_t pointIndex,
                    const TrackDisplayPoint::Interaction& interaction) const;
    /// 航迹无新点超过该时长后整条移除（ms）
    void pruneStaleTracks(qint64 nowMs);

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
};
