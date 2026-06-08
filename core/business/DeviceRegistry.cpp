#include "DeviceRegistry.h"
#include "DataStore.h"
#include <QDateTime>
#include <QDebug>

static const QString kTable = QStringLiteral("devices");

QVariantMap DeviceInfo::toVariantMap() const {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("brand"), brand},
        {QStringLiteral("kind"), static_cast<int>(kind)},
        {QStringLiteral("lng"), lng},
        {QStringLiteral("lat"), lat},
        {QStringLiteral("altitude"), altitude},
        {QStringLiteral("max_range"), maxRange},
        {QStringLiteral("faces_json"), facesJson},
        {QStringLiteral("endpoint_id"), endpointId},
        {QStringLiteral("linked_radar_id"), linkedRadarId},
        {QStringLiteral("mount_offset_azimuth"), mountOffsetAzimuth},
        {QStringLiteral("coordinate_system"), coordinateSystem},
        {QStringLiteral("status"), static_cast<int>(status)},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("updated_at"), updatedAt},
    };
}

DeviceInfo DeviceInfo::fromVariantMap(const QVariantMap& m) {
    DeviceInfo d;
    d.id                = m.value(QStringLiteral("id")).toString();
    d.name              = m.value(QStringLiteral("name")).toString();
    d.brand             = m.value(QStringLiteral("brand")).toString();
    d.kind              = static_cast<DeviceKind>(m.value(QStringLiteral("kind")).toInt());
    d.lng               = m.value(QStringLiteral("lng")).toDouble();
    d.lat               = m.value(QStringLiteral("lat")).toDouble();
    d.altitude          = m.value(QStringLiteral("altitude")).toDouble();
    d.maxRange          = m.value(QStringLiteral("max_range")).toDouble();
    d.facesJson         = m.value(QStringLiteral("faces_json")).toString();
    d.endpointId        = m.value(QStringLiteral("endpoint_id")).toString();
    d.linkedRadarId     = m.value(QStringLiteral("linked_radar_id")).toString();
    d.mountOffsetAzimuth = m.value(QStringLiteral("mount_offset_azimuth")).toDouble();
    d.coordinateSystem  = m.value(QStringLiteral("coordinate_system")).toString();
    d.status            = static_cast<DeviceStatus>(m.value(QStringLiteral("status")).toInt());
    d.createdAt         = m.value(QStringLiteral("created_at")).toLongLong();
    d.updatedAt         = m.value(QStringLiteral("updated_at")).toLongLong();
    return d;
}

DeviceRegistry::DeviceRegistry(DataStore* store)
    : m_store(store) {}

bool DeviceRegistry::ensureSchema() {
    if (!m_store) return false;
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS devices ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  brand TEXT,"
        "  kind INTEGER DEFAULT 0,"
        "  lng REAL DEFAULT 0,"
        "  lat REAL DEFAULT 0,"
        "  altitude REAL DEFAULT 0,"
        "  max_range REAL DEFAULT 0,"
        "  faces_json TEXT DEFAULT '[]',"
        "  endpoint_id TEXT DEFAULT '',"
        "  linked_radar_id TEXT DEFAULT '',"
        "  mount_offset_azimuth REAL DEFAULT 0,"
        "  coordinate_system TEXT DEFAULT 'WGS-84',"
        "  status INTEGER DEFAULT 0,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ")");
    return m_store->execute(sql);
}

bool DeviceRegistry::ensureDefaultControllableRadar() {
    // Check if any existing device already has radar_cpu binding
    for (const auto& d : list()) {
        if (d.id == QStringLiteral("radar_cpu") || d.endpointId == QStringLiteral("radar_cpu"))
            return true;  // already controllable
    }
    // Create one
    DeviceInfo dev;
    dev.id = QStringLiteral("radar_001");
    dev.name = QStringLiteral("NODE_ALPHA_01");
    dev.brand = QStringLiteral("cetc38");
    dev.kind = DeviceKind::Radar;
    dev.lng = 118.65309;
    dev.lat = 31.93502;
    dev.maxRange = 5;
    dev.facesJson = QStringLiteral(R"([{"label":"A","boresightAzimuth":0,"scanAngle":45,"active":true}])");
    dev.endpointId = QStringLiteral("radar_cpu");
    dev.coordinateSystem = QStringLiteral("WGS-84");
    qDebug() << "[DeviceRegistry] 创建默认设备: id=radar_001, endpointId=radar_cpu";
    return create(dev);
}

QVector<DeviceInfo> DeviceRegistry::list() const {
    QVector<DeviceInfo> result;
    if (!m_store) return result;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM devices ORDER BY created_at"));
    for (const auto& row : rows)
        result.append(DeviceInfo::fromVariantMap(row));
    return result;
}

std::optional<DeviceInfo> DeviceRegistry::get(const QString& id) const {
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(
        QStringLiteral("SELECT * FROM devices WHERE id = ?"),
        {id});
    if (rows.isEmpty()) return std::nullopt;
    return DeviceInfo::fromVariantMap(rows.first());
}

bool DeviceRegistry::create(const DeviceInfo& dev) {
    if (!m_store) return false;
    DeviceInfo d = dev;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    d.createdAt = now;
    d.updatedAt = now;
    return m_store->insert(kTable, d.toVariantMap());
}

bool DeviceRegistry::update(const DeviceInfo& dev) {
    if (!m_store) return false;
    if (!get(dev.id).has_value()) return false;  // device not found
    DeviceInfo d = dev;
    d.updatedAt = QDateTime::currentMSecsSinceEpoch();
    // Build SET clause dynamically
    auto vm = d.toVariantMap();
    QStringList setClauses;
    QVariantList bindValues;
    for (auto it = vm.begin(); it != vm.end(); ++it) {
        if (it.key() == QStringLiteral("id")) continue;
        setClauses.append(it.key() + QStringLiteral(" = ?"));
        bindValues.append(it.value());
    }
    bindValues.append(d.id);
    QString sql = QStringLiteral("UPDATE devices SET ")
                  + setClauses.join(QStringLiteral(", "))
                  + QStringLiteral(" WHERE id = ?");
    return m_store->execute(sql, bindValues);
}

bool DeviceRegistry::remove(const QString& id) {
    if (!m_store) return false;
    if (!get(id).has_value()) return false;  // device not found
    return m_store->execute(
        QStringLiteral("DELETE FROM devices WHERE id = ?"), {id});
}
