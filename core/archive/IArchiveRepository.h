/**
 * @file IArchiveRepository.h
 * @brief 定义归档 Consumer 与具体数据库实现之间的强类型事务提交边界。
 * @details 上游 ArchiveReader 把 TrackTopic 或 EventTopic 的固定 ScratchBuffer 批次传入，下游 SQLite/文件
 * Repository 完成类型映射、事务写入和提交。本接口不维护 Topic cursor、不订阅 Doorbell、不接收 DataPacket
 * 或 variant；新人应把返回 true 理解为“本批已经提交”，而不是“已加入另一个内存队列”。
 */

#pragma once

#include "core/datadomain/TopicTypes.h"
#include "core/model/SystemEvent.h"
#include "core/model/TrackPacket.h"

#include <QtGlobal>

namespace core::archive {

/**
 * @brief 为不同强类型事实提供独立批量提交入口的持久化端口。
 * @details 实现对象由存储组合根拥有，并与 ArchiveReader 在同一存储线程使用；方法不得把 records 指针保存
 * 到返回之后。失败必须返回 false 且回滚整批，禁止部分提交后仍报告失败。
 */
class IArchiveRepository
{
public:
    /** @brief 允许通过接口安全销毁具体数据库 Repository。 */
    virtual ~IArchiveRepository() = default;

    /**
     * @brief 在一个事务中持久化连续航迹记录并完成提交。
     * @param records ArchiveReader ScratchBuffer 中按 Topic sequence 升序的首地址。
     * @param count 本批有效记录数，必须大于零；records 只在调用期间有效。
     * @return 整批持久化并提交成功返回 true；失败或回滚返回 false。
     */
    virtual bool appendTrackBatch(
        const core::domain::TopicRecord<core::model::TrackPacket> *records,
        qsizetype count) = 0;

    /**
     * @brief 在一个事务中持久化连续系统事件记录并完成提交。
     * @param records ArchiveReader ScratchBuffer 中按 Topic sequence 升序的首地址。
     * @param count 本批有效记录数，必须大于零；records 只在调用期间有效。
     * @return 整批持久化并提交成功返回 true；失败或回滚返回 false。
     */
    virtual bool appendEventBatch(
        const core::domain::TopicRecord<core::model::SystemEvent> *records,
        qsizetype count) = 0;
};

} // namespace core::archive
