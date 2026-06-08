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
    /// 确保至少有一条可控雷达设备（id=radar_cpu, endpointId=radar_cpu）
    bool ensureDefaultControllableRadar();
    QVector<DeviceInfo> list() const;
    std::optional<DeviceInfo> get(const QString& id) const;
    bool create(const DeviceInfo& dev);
    bool update(const DeviceInfo& dev);
    bool remove(const QString& id);

private:
    DataStore* m_store = nullptr;
};
