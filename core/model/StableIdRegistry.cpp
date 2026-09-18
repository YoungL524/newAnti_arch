#include "core/model/StableIdRegistry.h"

#include <QReadLocker>
#include <QWriteLocker>

namespace core::model {
namespace {

/**
 * @brief 在写锁已持有时登记不可变映射，拒绝同一紧凑句柄被解释成另一稳定实体。
 */
template<typename Key>
bool registerImmutable(QHash<Key, QUuid> &registry, Key key, const QUuid &uuid)
{
    if (key == Key{} || uuid.isNull()) return false;
    const auto found = registry.constFind(key);
    if (found != registry.cend()) return *found == uuid;
    registry.insert(key, uuid);
    return true;
}

} // namespace

/**
 * @brief 以写锁原子登记设备身份，避免 WebReader 观察到半更新关系。
 */
bool StableIdRegistry::registerDevice(DeviceId deviceId, const QUuid &deviceUuid)
{
    QWriteLocker locker(&m_lock);
    return registerImmutable(m_devices, deviceId.value(), deviceUuid);
}

/**
 * @brief 以写锁原子登记任务身份，句柄在本次进程中禁止重绑定。
 */
bool StableIdRegistry::registerTask(TaskId taskId, const QUuid &taskUuid)
{
    QWriteLocker locker(&m_lock);
    return registerImmutable(m_tasks, taskId.value(), taskUuid);
}

/**
 * @brief 以写锁原子登记采集会话身份，停止后仍保留映射供晚到 Topic 批次解析。
 */
bool StableIdRegistry::registerAcquisition(
    AcquisitionId acquisitionId, const QUuid &sessionUuid)
{
    QWriteLocker locker(&m_lock);
    return registerImmutable(m_acquisitions, acquisitionId.value(), sessionUuid);
}

/**
 * @brief 在短读锁内取得设备 UUID 值副本，锁外由调用方完成字符串序列化。
 */
QUuid StableIdRegistry::resolveDevice(DeviceId deviceId) const
{
    QReadLocker locker(&m_lock);
    return m_devices.value(deviceId.value());
}

/**
 * @brief 在短读锁内取得任务 UUID 值副本，不访问 SQLite。
 */
QUuid StableIdRegistry::resolveTask(TaskId taskId) const
{
    QReadLocker locker(&m_lock);
    return m_tasks.value(taskId.value());
}

/**
 * @brief 在短读锁内取得采集会话 UUID 值副本，停止会话不会删除该关系。
 */
QUuid StableIdRegistry::resolveAcquisition(AcquisitionId acquisitionId) const
{
    QReadLocker locker(&m_lock);
    return m_acquisitions.value(acquisitionId.value());
}

/**
 * @brief 取得独占锁后清空本次运行映射，防止进程内句柄跨 application run 复用。
 */
void StableIdRegistry::clear()
{
    QWriteLocker locker(&m_lock);
    m_devices.clear();
    m_tasks.clear();
    m_acquisitions.clear();
}

} // namespace core::model
