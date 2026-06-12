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
    void setInteraction(const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);

signals:
    void snapshotReady(const TrackDisplaySnapshot& snapshot);

public:
    void start();
    void stop();
    void clear();

private:
    void applyStyle(TrackDisplayPoint& dp, uint32_t pointIndex,
                    const TrackDisplayPoint::Interaction& interaction) const;

    struct CachedPoint {
        TrackPoint point;
        uint32_t pointIndex = 0;
    };
    struct TrackCache {
        QVector<CachedPoint> points;
        uint32_t nextPointIndex = 0;
    };

    QHash<TrackKey, TrackCache> m_caches;
    QHash<TrackKey, TrackDisplayPoint::Interaction> m_interactions;
    QTimer* m_timer = nullptr;
};
