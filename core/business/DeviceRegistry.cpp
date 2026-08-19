#include "DeviceRegistry.h"
#include "DataStore.h"
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

static const QString kTable = QStringLiteral("devices");

/**
 * @brief 查询设备当前参与的活动任务 ID，供端点迁移事件关联任务日志。
 */
static QString activeTaskIdForDevice(DataStore* store, const QString& deviceId)
{
    if (!store || deviceId.isEmpty()
        || !store->tableExists(QStringLiteral("tasks"))
        || !store->tableExists(QStringLiteral("task_devices"))) {
        return {};
    }
    const auto rows = store->query(
        QStringLiteral("SELECT t.id FROM task_devices td "
                       "JOIN tasks t ON t.id = td.task_id "
                       "WHERE td.device_id = ? AND t.status = 'Active' LIMIT 1"),
        {deviceId});
    return rows.isEmpty() ? QString() : rows.first().value(QStringLiteral("id")).toString();
}

static bool migrateEndpointBinding(DataStore* store, const QString& endpointId, const QString& keepDeviceId,
                                   QStringList* migratedOut = nullptr,
                                   QString* errorOut = nullptr)
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
        const QString activeTaskId = activeTaskIdForDevice(store, id);
        if (!activeTaskId.isEmpty()) {
            if (migratedOut) migratedOut->clear();
            if (errorOut) {
                *errorOut = QStringLiteral("端点 %1 正被活动任务 %2 的设备 %3 使用，不能迁移")
                                .arg(endpointId, activeTaskId, id);
            }
            return false;
        }
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

/**
 * @brief 迁移历史库中一个端点绑定多台雷达的脏数据，只保留确定的主记录。
 * @details 解除的设备会写系统事件并返回 ID，保证新唯一索引创建前数据满足约束。
 */
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
        auto allBound = store->query(
            QStringLiteral("SELECT id, kind FROM devices WHERE endpoint_id = ? "
                           "ORDER BY kind ASC, updated_at DESC, created_at ASC"),
            {endpointId});

        QString keepId;
        QStringList toClear;
        QVector<QVariantMap> radars;
        for (const auto& dev : allBound) {
            if (dev.value(QStringLiteral("kind")).toInt() == static_cast<int>(DeviceKind::Radar))
                radars.append(dev);
            else
                toClear.append(dev.value(QStringLiteral("id")).toString());
        }

        if (!radars.isEmpty()) {
            int keepIndex = 0;
            for (int i = 0; i < radars.size(); ++i) {
                const QString candidateId = radars[i].value(QStringLiteral("id")).toString();
                if (!activeTaskIdForDevice(store, candidateId).isEmpty()) {
                    keepIndex = i;
                    break;
                }
            }
            keepId = radars[keepIndex].value(QStringLiteral("id")).toString();
            for (int i = 0; i < radars.size(); ++i) {
                if (i != keepIndex)
                    toClear.append(radars[i].value(QStringLiteral("id")).toString());
            }
        } else {
            for (const auto& dev : allBound)
                toClear.append(dev.value(QStringLiteral("id")).toString());
        }

        for (const QString& deviceId : toClear) {
            if (deviceId == keepId)
                continue;
            qWarning() << "[DeviceRegistry] 历史重复 endpoint" << endpointId
                       << "解除设备" << deviceId << "的绑定"
                       << (keepId.isEmpty() ? QStringLiteral("(无保留设备)")
                                            : QStringLiteral("(保留 %1)").arg(keepId));
            if (!store->execute(
                    QStringLiteral("UPDATE devices SET endpoint_id = '', updated_at = ? WHERE id = ?"),
                    {now, deviceId}))
                return false;
        }
    }
    return true;
}

/**
 * @brief 把设备全部持久字段转换为数据库列名对应的 QVariantMap。
 */
QVariantMap DeviceInfo::toVariantMap() const {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("brand"), brand},
        {QStringLiteral("kind"), static_cast<int>(kind)},
        {QStringLiteral("lng"), lng},
        {QStringLiteral("lat"), lat},
        {QStringLiteral("altitude"), altitude},
        {QStringLiteral("pitch"), pitch},
        {QStringLiteral("roll"), roll},
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

/**
 * @brief 从查询行恢复设备字段，并兼容旧 schema 缺失姿态或阵面列的情况。
 */
DeviceInfo DeviceInfo::fromVariantMap(const QVariantMap& m) {
    DeviceInfo d;
    d.id                = m.value(QStringLiteral("id")).toString();
    d.name              = m.value(QStringLiteral("name")).toString();
    d.brand             = m.value(QStringLiteral("brand")).toString();
    d.kind              = static_cast<DeviceKind>(m.value(QStringLiteral("kind")).toInt());
    d.lng               = m.value(QStringLiteral("lng")).toDouble();
    d.lat               = m.value(QStringLiteral("lat")).toDouble();
    d.altitude          = m.value(QStringLiteral("altitude")).toDouble();
    d.pitch             = m.value(QStringLiteral("pitch")).toDouble();
    d.roll              = m.value(QStringLiteral("roll")).toDouble();
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

/**
 * @brief 保存非拥有 DataStore；台账所有查询和写入必须在该连接所属线程执行。
 */
DeviceRegistry::DeviceRegistry(DataStore* store)
    : m_store(store) {}

/**
 * @brief 按设备类别前缀和随机 UUID 生成不依赖数据库序列的业务 ID。
 */
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

/**
 * @brief 创建 devices 表、补齐历史字段、清理重复端点绑定并建立唯一索引。
 */
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
        "  pitch REAL DEFAULT 0,"
        "  roll REAL DEFAULT 0,"
        "  max_range REAL DEFAULT 0,"
        "  faces_json TEXT DEFAULT '[]',"
        "  endpoint_id TEXT DEFAULT '',"
        "  linked_radar_id TEXT DEFAULT '',"
        "  mount_offset_azimuth REAL DEFAULT 0,"
        "  coordinate_system TEXT DEFAULT 'WGS-84',"
        "  status INTEGER DEFAULT 0,"
        "  created_at BIGINT NOT NULL,"
        "  updated_at BIGINT NOT NULL"
        ")");
    if (!m_store->execute(sql))
        return false;
    if (m_store->database().driverName().startsWith(QStringLiteral("QPSQL"))
        && !m_store->execute(QStringLiteral(
            "ALTER TABLE devices ALTER COLUMN created_at TYPE BIGINT, "
            "ALTER COLUMN updated_at TYPE BIGINT"))) {
        return false;
    }
    const bool postgres = m_store->database().driverName().startsWith(QStringLiteral("QPSQL"));
    auto ensurePoseColumn = [this, postgres](const QString& column) {
        const auto columns = postgres
            ? m_store->query(QStringLiteral(
                  "SELECT column_name AS name FROM information_schema.columns "
                  "WHERE table_schema = current_schema() AND table_name = 'devices'"))
            : m_store->query(QStringLiteral("PRAGMA table_info(devices)"));
        for (const auto& existing : columns) {
            if (existing.value(QStringLiteral("name")).toString() == column)
                return true;
        }
        return m_store->execute(
            QStringLiteral("ALTER TABLE devices ADD COLUMN %1 REAL DEFAULT 0").arg(column));
    };
    if (!ensurePoseColumn(QStringLiteral("pitch"))
        || !ensurePoseColumn(QStringLiteral("roll"))) {
        return false;
    }
    if (!dedupeLegacyEndpointBindings(m_store))
        return false;
    return m_store->execute(
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_devices_endpoint_id_unique "
                       "ON devices(endpoint_id) WHERE endpoint_id != ''"));
}

/**
 * @brief 新库没有可控雷达时创建绑定 radar_cpu 的默认 radar_001，并兼容 legacy 记录。
 */
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

/**
 * @brief 查询全部设备并逐行转换为 DeviceInfo；数据库不可用时返回空集合。
 */
QVector<DeviceInfo> DeviceRegistry::list() const {
    QVector<DeviceInfo> result;
    if (!m_store) return result;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM devices ORDER BY created_at"));
    for (const auto& row : rows)
        result.append(DeviceInfo::fromVariantMap(row));
    return result;
}

/**
 * @brief 按主键读取唯一设备；无结果与查询失败均返回空 optional，并由 DataStore 记录错误。
 */
std::optional<DeviceInfo> DeviceRegistry::get(const QString& id) const {
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(
        QStringLiteral("SELECT * FROM devices WHERE id = ?"),
        {id});
    if (rows.isEmpty()) return std::nullopt;
    return DeviceInfo::fromVariantMap(rows.first());
}

/**
 * @brief 仅当端点恰好绑定一台雷达时返回记录；冲突时返回空以避免错误归属航迹。
 */
std::optional<DeviceInfo> DeviceRegistry::findRadarByEndpointId(const QString& endpointId) const {
    const QString deviceId = cachedDeviceIdForEndpoint(endpointId);
    if (deviceId.isEmpty())
        return std::nullopt;
    return get(deviceId);
}

/**
 * @brief 从内存映射解析端点，不访问 SQLite，供每包接收热路径调用。
 */
QString DeviceRegistry::cachedDeviceIdForEndpoint(const QString& endpointId) const
{
    if (endpointId.isEmpty())
        return {};
    return m_endpointToDeviceId.value(endpointId);
}

/**
 * @brief 全量扫描有绑定的雷达并重建唯一映射；检测到冲突时移除该端点而不是任选一台。
 */
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

/**
 * @brief 返回并清空最近写操作迁移出的设备 ID，避免同一迁移事件被重复通知。
 */
QStringList DeviceRegistry::takeLastMigratedDeviceIds()
{
    const QStringList result = m_lastMigratedDeviceIds;
    m_lastMigratedDeviceIds.clear();
    return result;
}

/**
 * @brief 查询端点当前绑定的全部雷达，供写事务决定是否需要迁移旧绑定。
 */
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

/**
 * @brief 拒绝空 ID/名称及超出经纬度、姿态和距离合理范围的设备写入。
 */
bool DeviceRegistry::validateDeviceWrite(const DeviceInfo& dev)
{
    if (dev.kind != DeviceKind::Radar && !dev.endpointId.isEmpty()) {
        qWarning() << "[DeviceRegistry] 非雷达设备不允许绑定 endpoint:" << dev.id
                   << "endpointId=" << dev.endpointId;
        return false;
    }
    return true;
}

/**
 * @brief 在事务中迁移端点旧绑定并插入新设备，成功后刷新热路径缓存。
 */
bool DeviceRegistry::create(const DeviceInfo& dev) {
    m_lastError.clear();
    if (!m_store) {
        m_lastError = QStringLiteral("设备存储未初始化");
        return false;
    }
    if (!validateDeviceWrite(dev)) {
        m_lastError = QStringLiteral("设备字段不符合写入约束");
        return false;
    }
    m_lastMigratedDeviceIds.clear();
    if (!m_store->beginTransaction())
        return false;

    bool ok = true;
    if (!dev.endpointId.isEmpty() && dev.kind == DeviceKind::Radar)
        ok = migrateEndpointBinding(m_store, dev.endpointId, dev.id,
                                    &m_lastMigratedDeviceIds, &m_lastError);

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
    if (m_lastError.isEmpty())
        m_lastError = QStringLiteral("创建设备失败");
    return false;
}

/**
 * @brief 为空 ID 分配类别前缀 ID，并在极低概率冲突时重新生成后重试创建。
 */
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

/**
 * @brief 在同一事务迁移端点绑定并更新设备全部可变字段，失败时回滚两部分变化。
 */
bool DeviceRegistry::update(const DeviceInfo& dev) {
    m_lastError.clear();
    if (!m_store) {
        m_lastError = QStringLiteral("设备存储未初始化");
        return false;
    }
    const auto existing = get(dev.id);
    if (!existing.has_value()) {
        m_lastError = QStringLiteral("设备不存在");
        return false;
    }
    if (!validateDeviceWrite(dev)) {
        m_lastError = QStringLiteral("设备字段不符合写入约束");
        return false;
    }
    if (existing->kind == DeviceKind::Radar && !existing->endpointId.isEmpty()
        && dev.endpointId.isEmpty()) {
        const QString activeTaskId = activeTaskIdForDevice(m_store, dev.id);
        if (!activeTaskId.isEmpty()) {
            m_lastError = QStringLiteral("设备正被活动任务 %1 使用，不能解除通信端点")
                              .arg(activeTaskId);
            return false;
        }
    }
    m_lastMigratedDeviceIds.clear();
    if (!m_store->beginTransaction())
        return false;

    bool ok = true;
    if (!dev.endpointId.isEmpty() && dev.kind == DeviceKind::Radar)
        ok = migrateEndpointBinding(m_store, dev.endpointId, dev.id,
                                    &m_lastMigratedDeviceIds, &m_lastError);

    DeviceInfo d = dev;
    d.createdAt = existing->createdAt;
    d.status = existing->status;
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
    if (m_lastError.isEmpty())
        m_lastError = QStringLiteral("更新设备失败");
    return false;
}

/**
 * @brief 按 ID 删除设备；成功后刷新端点缓存，避免接收热路径继续归属到已删除设备。
 */
bool DeviceRegistry::remove(const QString& id) {
    m_lastError.clear();
    if (!m_store) {
        m_lastError = QStringLiteral("设备存储未初始化");
        return false;
    }
    const auto existing = get(id);
    if (!existing.has_value()) {
        m_lastError = QStringLiteral("设备不存在");
        return false;
    }

    if (existing->kind == DeviceKind::Radar) {
        const auto linked = m_store->query(
            QStringLiteral("SELECT id FROM devices WHERE linked_radar_id = ? LIMIT 1"), {id});
        if (!linked.isEmpty()) {
            m_lastError = QStringLiteral("雷达仍有关联子设备 %1，请先解除或删除子设备")
                              .arg(linked.first().value(QStringLiteral("id")).toString());
            return false;
        }
    }

    if (m_store->tableExists(QStringLiteral("tasks"))
        && m_store->tableExists(QStringLiteral("task_devices"))) {
        const auto references = m_store->query(
            QStringLiteral("SELECT t.id, t.status FROM task_devices td "
                           "JOIN tasks t ON t.id = td.task_id WHERE td.device_id = ?"),
            {id});
        for (const auto& reference : references) {
            const QString status = reference.value(QStringLiteral("status")).toString();
            if (status != QStringLiteral("Draft")) {
                m_lastError = QStringLiteral("设备已被任务 %1 引用（状态 %2），不能硬删除")
                                  .arg(reference.value(QStringLiteral("id")).toString(), status);
                return false;
            }
        }
    }

    if (!m_store->beginTransaction()) {
        m_lastError = QStringLiteral("无法开始删除事务");
        return false;
    }
    bool ok = m_store->execute(
                  QStringLiteral("DELETE FROM task_devices WHERE device_id = ?"), {id})
              && m_store->execute(
                  QStringLiteral("DELETE FROM devices WHERE id = ?"), {id});
    if (ok) {
        if (!m_store->commitTransaction()) {
            m_store->rollbackTransaction();
            m_lastError = QStringLiteral("删除事务提交失败");
            return false;
        }
        refreshEndpointCache();
        return true;
    }
    m_store->rollbackTransaction();
    m_lastError = QStringLiteral("删除设备失败");
    return false;
}
