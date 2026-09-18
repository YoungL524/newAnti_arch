/**
 * @file StableIdRegistry.h
 * @brief 定义进程内紧凑句柄到跨重启 UUID 的线程安全只读解析边界。
 * @details 上游 DeviceConfigService 与 TaskService 在 SQLite 提交并分配句柄后登记映射，下游 WebReader 在
 * 序列化边界解析稳定身份。本类不分配句柄、不访问数据库、不保存业务数据，也不参与 Topic retention；
 * 新人应从 registerDevice() 与 resolveDevice() 阅读唯一写入和跨线程读取约束。
 */
#pragma once

#include "core/model/EntityIds.h"

#include <QHash>
#include <QReadWriteLock>
#include <QUuid>

namespace core::model {

/**
 * @brief 保存当前 application run 内 CompactId 到稳定 UUID 的唯一注册关系。
 * @note 登记发生在低频 SQLite owner 线程，解析可发生在 Web/GUI 线程；锁内只访问定长键和值。
 */
class StableIdRegistry final
{
public:
    /** @brief 登记非零 DeviceId 与非空设备 UUID；重复登记相同关系幂等成功，冲突返回 false。 */
    bool registerDevice(DeviceId deviceId, const QUuid &deviceUuid);

    /** @brief 登记非零 TaskId 与非空任务 UUID；重复登记相同关系幂等成功，冲突返回 false。 */
    bool registerTask(TaskId taskId, const QUuid &taskUuid);

    /** @brief 登记非零 AcquisitionId 与非空采集会话 UUID；冲突或无效参数返回 false。 */
    bool registerAcquisition(AcquisitionId acquisitionId, const QUuid &sessionUuid);

    /** @brief 解析设备稳定 UUID；未知句柄返回空 UUID。 */
    QUuid resolveDevice(DeviceId deviceId) const;

    /** @brief 解析任务稳定 UUID；未知句柄返回空 UUID。 */
    QUuid resolveTask(TaskId taskId) const;

    /** @brief 解析采集会话稳定 UUID；未知句柄返回空 UUID。 */
    QUuid resolveAcquisition(AcquisitionId acquisitionId) const;

    /**
     * @brief 在运行时完全停止且所有 Reader 不再访问后清空本次进程映射。
     * @warning 不得与登记操作并发；并发只读会由写锁等待并安全看到空注册域。
     */
    void clear();

private:
    mutable QReadWriteLock m_lock; ///< 保护三个只增注册表及停机清理，不覆盖数据库或 JSON 工作。
    QHash<quint32, QUuid> m_devices; ///< DeviceId 原值到 devices 稳定 UUID。
    QHash<quint32, QUuid> m_tasks; ///< TaskId 原值到 tasks 稳定 UUID。
    QHash<quint64, QUuid> m_acquisitions; ///< AcquisitionId 原值到 collection_sessions 稳定 UUID。
};

} // namespace core::model
