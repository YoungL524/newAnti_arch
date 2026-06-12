#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>
#include <optional>

class DataStore;

enum class DeviceKind : int {
    Radar = 0,
    Opto = 1,
    Level = 2,
    Gps = 3,
};

enum class DeviceStatus : int {
    Offline = 0,
    Standby = 1,
    Working = 2,
    Fault = 3,
};

struct DeviceInfo {
    QString id;
    QString name;
    QString brand;
    DeviceKind kind = DeviceKind::Radar;
    double lng = 0;
    double lat = 0;
    double altitude = 0;
    double maxRange = 0;
    QString facesJson = QStringLiteral("[]");
    QString endpointId;
    QString linkedRadarId;
    double mountOffsetAzimuth = 0;
    QString coordinateSystem = QStringLiteral("WGS-84");
    DeviceStatus status = DeviceStatus::Offline;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;

    QVariantMap toVariantMap() const;
    static DeviceInfo fromVariantMap(const QVariantMap& m);
};

class DeviceRegistry {
public:
    explicit DeviceRegistry(DataStore* store);

    bool ensureSchema();
    /// 为指定设备类型生成稳定唯一的业务 id
    static QString generateId(DeviceKind kind);
    /// 确保至少有一条绑定 radar_cpu 端点的可控雷达业务设备。
    /// 新库默认创建 id=radar_001, endpointId=radar_cpu。
    /// 历史库可能存在 id=radar_cpu 的 legacy 设备。
    bool ensureDefaultControllableRadar();
    QVector<DeviceInfo> list() const;
    std::optional<DeviceInfo> get(const QString& id) const;
    /// 按 endpointId 查找绑定的 Radar 业务设备（用于航迹归属；多个时返回空，避免错误映射）
    std::optional<DeviceInfo> findRadarByEndpointId(const QString& endpointId) const;
    bool create(const DeviceInfo& dev);
    /// 创建设备，id 为空时自动生成（含冲突重试）
    std::optional<DeviceInfo> createAssigningId(DeviceInfo dev);
    bool update(const DeviceInfo& dev);
    bool remove(const QString& id);

private:
    /// 查询所有绑定该 endpoint 的 Radar 设备（写端冲突检查用）
    QVector<DeviceInfo> findRadarDevicesByEndpoint(const QString& endpointId) const;
    DataStore* m_store = nullptr;
};
