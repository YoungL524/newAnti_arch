/**
 * @file TaskDefinition.h
 * @brief 定义 UI 任务创建边界与 SQLite tasks 记录共用的低频任务元数据。
 * @details 上游 UI/API 创建任务时填写名称、操作员和说明，TaskService 持久化后分配紧凑 TaskId，下游采集
 * 会话和 TrackPacket 只引用该句柄。本类型不表示运行状态、不进入 Topic，也不复制航迹或设备配置。
 */

#pragma once

#include "core/config/DeviceConfiguration.h"
#include "core/task/RadarControlSettings.h"

#include <QString>
#include <QUuid>
#include <QVector>

namespace core::task {

/** @brief 记录任务对一台稳定设备及其启用阵面的持久化选择。 */
struct TaskDeviceBinding
{
    QUuid deviceUuid; ///< 设备台账稳定 UUID，不保存进程内 DeviceId。
    quint8 faceMask = 0; ///< bit0～bit3 对应1～4号阵面，必须是设备已启用阵面的非空子集。
};

/** @brief 保存一项可持久化任务的稳定身份和用户元数据。 */
struct TaskDefinition
{
    QUuid taskUuid; ///< 跨重启稳定任务身份；创建时允许为空并由 TaskService 生成。
    QString taskName; ///< 用户可见任务名称，不能为空但不要求全库唯一。
    QString operatorName; ///< 创建或负责该任务的操作员展示名称。
    QString description; ///< 任务目的与现场备注，允许为空。
    core::config::RadarArrayType radarArrayType = core::config::RadarArrayType::SingleFace; ///< 任务要求的整机阵面形态。
    QVector<TaskDeviceBinding> deviceBindings; ///< 本任务选中的设备及阵面；创建后由 SQLite 持久化。
    QUuid sourceTemplateUuid; ///< 创建任务时选用的模板UUID；只作来源追溯，后续模板修改不影响本任务。
    RadarControlSettings controlSettings; ///< 创建时从模板复制的独立参数快照，任务运行修改不回写共享模板。
    qint64 createdUtcMs = 0; ///< SQLite 提交时刻，Unix epoch UTC 毫秒；创建请求不得自行指定。
    qint64 completedUtcMs = 0; ///< 用户完成任务的 Unix epoch UTC 毫秒；零表示仍可被选择执行。
    qint64 archivedUtcMs = 0; ///< 用户归档已完成任务的 Unix epoch UTC 毫秒；零表示尚未归档，历史事实仍留在原表。
};

} // namespace core::task
