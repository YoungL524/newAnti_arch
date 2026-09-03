/**
 * @file TaskDefinition.h
 * @brief 定义 UI 任务创建边界与 SQLite tasks 记录共用的低频任务元数据。
 * @details 上游 UI/API 创建任务时填写名称、操作员和说明，TaskService 持久化后分配紧凑 TaskId，下游采集
 * 会话和 TrackPacket 只引用该句柄。本类型不表示运行状态、不进入 Topic，也不复制航迹或设备配置。
 */

#pragma once

#include <QString>
#include <QUuid>

namespace core::task {

/** @brief 保存一项可持久化任务的稳定身份和用户元数据。 */
struct TaskDefinition
{
    QUuid taskUuid; ///< 跨重启稳定任务身份；创建时允许为空并由 TaskService 生成。
    QString taskName; ///< 用户可见任务名称，不能为空但不要求全库唯一。
    QString operatorName; ///< 创建或负责该任务的操作员展示名称。
    QString description; ///< 任务目的与现场备注，允许为空。
};

} // namespace core::task
