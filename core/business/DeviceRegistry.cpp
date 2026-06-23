#include "DeviceRegistry.h"
#include "DataStore.h"
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

static const QString kTable = QStringLiteral("devices");

static bool migrateEndpointBinding(DataStore* store, const QString& endpointId, const QString& keepDeviceId,
                                   QStringList* migratedOut = nullptr)
{
    QStringList migrated;
    if (!store || endpointId.isEmpty()) {
        if (migratedOut) *migratedOut = migrated;
        return true;
    }
    auto rows = store->query(
        QStringLiteral("SELECT id FROM devices WHERE endpoint_id = ? AND id != ?"),
        {endpointId, keepDeviceId});
    for (const auto& row : rows) {
        const QString id = row.value(QStringLiteral("id")).toString();
        migrated.append(id);
        qDebug() << "[DeviceRegistry] endpointId" << endpointId
                 << "从设备" << id << "迁移到" << keepDeviceId;
    }
    if (migrated.isEmpty()) {
        if (migratedOut) *migratedOut = migrated;
        return true;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!store->execute(
            QStringLiteral("UPDATE devices SET endpoint_id = '', updated_at = ? WHERE endpoint_id = ? AND id != ?"),
            {now, endpointId, keepDeviceId})) {
        if (migratedOut) migratedOut->clear();
        return false;
    }
    if (migratedOut) *migratedOut = migrated;
    return true;
}

static bool dedupeLegacyEndpointBindings(DataStore* store)
{
    if (!store) return false;
    auto dups = store->query(
        QStringLiteral("SELECT endpoint_id FROM devices WHERE endpoint_id != '' "
                       "GROUP BY endpoint_id HAVING COUNT(*) > 1"));
    if (dups.isEmpty())
        return true;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& row : dups) {
        const QString endpointId = row.value(QStringLiteral("endpoint_id")).toString();
        auto bound = store->query(
            QStringLiteral("SELECT id FROM devices WHERE endpoint_id = ? AND kind = ? "
                           "ORDER BY updated_at DESC, created_at ASC"),
            {endpointId, static_cast<int>(DeviceKind::Radar)});
        for (int i = 1; i < bound.size(); ++i) {
            const QString deviceId = bound[i].value(QStringLiteral("id")).toString();
            qWarning() << "[DeviceRegistry] 历史重复 endpoint" << endpointId
                       << "解除设备" << deviceId << "的绑定（保留"
                       << bound.first().value(QStringLiteral("id")).toString() << "）";
            if (!store->execute(
                    QStringLiteral("UPDATE devices SET endpoint_id = '', updated_at = ? WHERE id = ?"),
                    {now, deviceId}))
                return false;
        }
    }
    return true;
}

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

QString DeviceRegistry::generateId(DeviceKind kind)
{
    const QString prefix = kind == DeviceKind::Radar ? QStringLiteral("radar_")
        : kind == DeviceKind::Opto ? QStringLiteral("opto_")
        : kind == DeviceKind::Level ? QStringLiteral("level_")
        : QStringLiteral("gps_");
    qint64 ms = QDateTime::currentMSecsSinceEpoch();
    uint32_t rand = QRandomGenerator::global()->bounded(10000u);
    return prefix + QString::number(ms) + QStringLiteral("_") + QString::number(rand);
}

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
    if (!m_store->execute(sql))
        return false;
    if (!dedupeLegacyEndpointBindings(m_store))
        return false;
    return m_store->execute(
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_devices_endpoint_id_unique "
                       "ON devices(endpoint_id) WHERE endpoint_id != ''"));
}

bool DeviceRegistry::ensureDefaultControllableRadar() {
    for (const auto& d : list()) {
        if (d.id == QStringLiteral("radar_cpu") || d.endpointId == QStringLiteral("radar_cpu")) {
            if (d.id == QStringLiteral("radar_cpu") && d.endpointId.isEmpty()) {
                DeviceInfo patched = d;
                patched.endpointId = QStringLiteral("radar_cpu");
                if (update(patched))
                    qDebug() << "[DeviceRegistry] 已补全历史设备 endpointId: id=radar_cpu";
            } else if (d.id == QStringLiteral("radar_cpu")) {
                qWarning() << "[DeviceRegistry] 默认可控设备使用历史 id=radar_cpu (业务设备 id 与端点 id 相同)";
            } else {
                qDebug() << "[DeviceRegistry] 默认可控设备: id=" << d.id << "endpointId=" << d.endpointId;
            }
            return true;
        }
    }
    DeviceInfo dev;
    dev.id = QStringLiteral("radar_001");
    dev.name = QStringLiteral("NODE_ALPHA_01");
    dev.brand = QStringLiteral("YHF");
    dev.kind = DeviceKind::Radar;
    dev.lng = 118.65309;
    dev.lat = 31.93502;
    dev.maxRange = 5;
    dev.facesJson = QStringLiteral(R"([{"label":"A","boresightAzimuth":0,"scanAngle":45,"active":true}])");
    dev.endpointId = QStringLiteral("radar_cpu");
    dev.coordinateSystem = QStringLiteral("WGS-84");
    if (create(dev)) {
        qDebug() << "[DeviceRegistry] 创建默认可控设备: id=" << dev.id << "endpointId=" << dev.endpointId;
        return true;
    }
    return false;
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

std::optional<DeviceInfo> DeviceRegistry::findRadarByEndpointId(const QString& endpointId) const {
    const QString deviceId = cachedDeviceIdForEndpoint(endpointId);
    if (deviceId.isEmpty())
        return std::nullopt;
    return get(deviceId);
}

QString DeviceRegistry::cachedDeviceIdForEndpoint(const QString& endpointId) const
{
    if (endpointId.isEmpty())
        return {};
    return m_endpointToDeviceId.value(endpointId);
}

void DeviceRegistry::refreshEndpointCache()
{
    m_endpointToDeviceId.clear();
    if (!m_store)
        return;

    const QVector<DeviceInfo> devices = list();
    QHash<QString, int> endpointCounts;
    for (const auto& d : devices) {
        if (d.kind != DeviceKind::Radar || d.endpointId.isEmpty())
            continue;
        endpointCounts[d.endpointId]++;
    }

    for (const auto& d : devices) {
        if (d.kind != DeviceKind::Radar || d.endpointId.isEmpty())
            continue;
        if (endpointCounts.value(d.endpointId) == 1)
            m_endpointToDeviceId.insert(d.endpointId, d.id);
        else if (endpointCounts.value(d.endpointId) > 1) {
            qWarning() << "[DeviceRegistry] endpointId" << d.endpointId
                       << "绑定了" << endpointCounts.value(d.endpointId)
                       << "个雷达设备，缓存中跳过";
        }
    }

    for (const auto& d : devices) {
        if (d.kind != DeviceKind::Radar || !d.endpointId.isEmpty())
            continue;
        if (!m_endpointToDeviceId.contains(d.id))
            m_endpointToDeviceId.insert(d.id, d.id);
    }
}

QStringList DeviceRegistry::takeLastMigratedDeviceIds()
{
    const QStringList result = m_lastMigratedDeviceIds;
    m_lastMigratedDeviceIds.clear();
    return result;
}

QVector<DeviceInfo> DeviceRegistry::findRadarDevicesByEndpoint(const QString& endpointId) const {
    if (!m_store) return {};
    auto rows = m_store->query(
        QStringLiteral("SELECT * FROM devices WHERE endpoint_id = ? AND kind = ?"),
        {endpointId, static_cast<int>(DeviceKind::Radar)});
    QVector<DeviceInfo> result;
    for (const auto& row : rows)
        result.append(DeviceInfo::fromVariantMap(row));
    return result;
}

bool DeviceRegistry::create(const DeviceInfo& dev) {
    if (!m_store) return false;
    m_lastMigratedDeviceIds.clear();
    if (!m_store->beginTransaction())
        return false;

    bool ok = true;
    if (!dev.endpointId.isEmpty() && dev.kind == DeviceKind::Radar)
        ok = migrateEndpointBinding(m_store, dev.endpointId, dev.id, &m_lastMigratedDeviceIds);

    DeviceInfo d = dev;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    d.createdAt = now;
    d.updatedAt = now;
    if (ok)
        ok = m_store->insert(kTable, d.toVariantMap());

    if (ok && m_store->commitTransaction()) {
        refreshEndpointCache();
        return true;
    }
    m_store->rollbackTransaction();
    return false;
}

std::optional<DeviceInfo> DeviceRegistry::createAssigningId(DeviceInfo dev)
{
    if (dev.id.isEmpty()) dev.id = generateId(dev.kind);
    // Only retry on id collision (max 5 attempts)
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) {
            if (!get(dev.id).has_value()) break;  // not a collision, stop retry
            dev.id = generateId(dev.kind);
        }
        if (create(dev)) return get(dev.id);
    }
    qWarning() << "[DeviceRegistry] 创建设备失败，重试耗尽: id=" << dev.id;
    return std::nullopt;
}

bool DeviceRegistry::update(const DeviceInfo& dev) {
    if (!m_store) return false;
    if (!get(dev.id).has_value()) return false;
    m_lastMigratedDeviceIds.clear();
    if (!m_store->beginTransaction())
        return false;

    bool ok = true;
    if (!dev.endpointId.isEmpty() && dev.kind == DeviceKind::Radar)
        ok = migrateEndpointBinding(m_store, dev.endpointId, dev.id, &m_lastMigratedDeviceIds);

    DeviceInfo d = dev;
    d.updatedAt = QDateTime::currentMSecsSinceEpoch();
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
    if (ok)
        ok = m_store->execute(sql, bindValues);

    if (ok && m_store->commitTransaction()) {
        refreshEndpointCache();
        return true;
    }
    m_store->rollbackTransaction();
    return false;
}

bool DeviceRegistry::remove(const QString& id) {
    if (!m_store) return false;
    if (!get(id).has_value()) return false;  // device not found
    if (!m_store->beginTransaction())
        return false;
    bool ok = m_store->execute(
                  QStringLiteral("DELETE FROM task_devices WHERE device_id = ?"), {id})
              && m_store->execute(
                  QStringLiteral("DELETE FROM devices WHERE id = ?"), {id});
    if (ok) {
        if (!m_store->commitTransaction()) {
            m_store->rollbackTransaction();
            return false;
        }
        refreshEndpointCache();
        return true;
    }
    m_store->rollbackTransaction();
    return false;
}
