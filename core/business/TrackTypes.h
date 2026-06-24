#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

/**
 * @brief 内部航迹点（RadarDataProcessor 输出，TrackDisplayManager 输入）
 */
struct TrackPoint {
    QString   deviceId;          // DeviceRegistry 业务设备 id
    QString   taskId;            // 当前 active task id（未归属时为空）
    uint32_t  trackId = 0;
    qint64    timestampMs = 0;
    double    range = 0;
    double    azimuth = 0;       // rad
    double    elevation = 0;     // rad
    double    radialSpeed = 0;
    double    rcs = 0;
    double    altitude = 0;
    double    x = 0, y = 0, z = 0;
    uint32_t  sceneNumber = 0;
};

/**
 * @brief 复合 key（内部 interaction map 及缓存索引）
 */
struct TrackKey {
    QString   deviceId;
    uint32_t  trackId = 0;

    bool operator==(const TrackKey& o) const {
        return deviceId == o.deviceId && trackId == o.trackId;
    }
};

inline uint qHash(const TrackKey& k, uint seed = 0) {
    return qHash(k.deviceId, seed) ^ qHash(k.trackId);
}

/**
 * @brief Web 显示协议点（全量 snapshot 中的单个点）
 */
struct TrackDisplayPoint {
    uint32_t  trackId = 0;
    QString   deviceId;
    qint64    timestamp = 0;
    double    range = 0;
    double    azimuth = 0;
    double    elevation = 0;
    double    altitude = 0;
    double    x = 0, y = 0, z = 0;
    double    radialSpeed = 0;
    double    rcs = 0;
    uint32_t  pointIndex = 0;    // 绝对递增序号

    // 显示状态
    struct Style {
        QString color  = QStringLiteral("#22c55e");
        double  size   = 3.0;
        double  opacity = 0.85;
    } style;

    struct Interaction {
        bool selected = false;
        bool trackingEnabled = false;
    } interaction;
};

/**
 * @brief 全量 snapshot（TrackDisplayManager 输出）
 */
struct TrackDisplaySnapshot {
    qint64 timestamp = 0;
    QVector<TrackDisplayPoint> tracks;
};

/**
 * @brief UI 航迹交互命令（WebApiController 发布，TrackDisplayManager 订阅）
 */
struct TrackInteractionCommand {
    QString deviceId;
    uint32_t trackId = 0;
    bool selected = false;
    bool trackingEnabled = false;
    qint64 timestamp = 0;
};
