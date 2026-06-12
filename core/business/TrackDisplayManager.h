#pragma once

#include <QObject>
#include <QHash>
#include <QVector>
#include "TrackTypes.h"

/**
 * @brief 航迹显示数据管理器
 * @details 接收 RadarDataProcessor 输出的 TrackPoint，
 *          维护显示缓存和 interaction 状态，每 1s 输出 TrackDisplaySnapshot。
 *
 * 非职责：
 * - 不查询 DeviceRegistry
 * - 不关心 endpointId
 * - 不判断航迹起批、消失、融合
 */
class TrackDisplayManager : public QObject {
    Q_OBJECT
public:
    explicit TrackDisplayManager(QObject* parent = nullptr);

    /// 接收一个航迹点
    void addPoint(const TrackPoint& pt);

    /// 获取当前全量 snapshot
    TrackDisplaySnapshot snapshot() const;

    /// 更新 interaction 状态
    void setInteraction(const QString& deviceId, uint32_t trackId,
                        bool selected, bool trackingEnabled);

signals:
    /// snapshot 就绪（可由定时器或外部触发）
    void snapshotReady(const TrackDisplaySnapshot& snapshot);

private:
    struct TrackCache {
        QVector<TrackDisplayPoint> points;  // 最多 500 点
        uint32_t nextPointIndex = 0;        // 绝对递增序号
    };

    QHash<TrackKey, TrackCache> m_caches;
    QHash<TrackKey, TrackDisplayPoint::Interaction> m_interactions;
};
