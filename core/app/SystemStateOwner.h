/**
 * @file SystemStateOwner.h
 * @brief 接收组合根已确认的生命周期事实，写入唯一 LatestState<SystemState>。
 * @details 从 setLifecycle/reportArchive 阅读；上游为组合根和存储运行时，下游为数据域和只读 WebReader。
 * 本类不控制服务启停、不读取航迹、不创建线程或计时器，也不保存第二份完整状态。
 */
#pragma once

#include "core/datadomain/LatestState.h"
#include "core/model/RuntimeState.h"
#include <QMutex>

namespace core::app {

/** @brief 系统状态唯一写入者；组合根持有，内存更新线程安全，析构前必须停止存储线程。 */
class SystemStateOwner final
{
public:
    /** @brief 借用生命周期长于 Owner 的系统状态；构造不伪造任何服务已就绪。 */
    explicit SystemStateOwner(core::domain::LatestState<core::model::SystemState> &state);
    /**
     * @brief 将组合根确认的阶段和服务位转换为完整快照；Faulted 阶段同步设置 Faulted 健康度。
     * @param phase 已发生的生命周期阶段，服务启停仍由组合根负责。
     * @param readinessFlags 已确认完成初始化的 Core 能力；停止及回滚后必须为零。
     * @return 非法组合或 UTC 回拨导致校验失败时返回 false、保留旧值，由调用者报告错误；同值返回 true 不递增 revision。
     * @note 保留归档降级与历史缺口；本方法由主线程组合根调用。
     */
    bool setLifecycle(core::model::ApplicationPhase phase,
                      core::model::SystemReadinessFlags readinessFlags);
    /**
     * @brief 接受存储线程报告的归档三位状态，合并历史缺口并保留生命周期。
     * @param flags 仅允许 ArchiveUnavailable、ArchiveGapDetected、EventGapDetected；缺口位只增不清。
     * @return 未初始化、未知位或门禁拒绝返回 false；相同结果不递增 revision。
     * @note 线程安全；锁内不访问数据库、不等待主线程，仅写入唯一 State。
     */
    bool reportArchive(core::model::SystemDegradationFlags flags);
private:
    /** @brief 调用者持有 m_mutex；补充当前时间与健康度，门禁通过才替换 State。 */
    bool store(core::model::SystemState value);
    core::domain::LatestState<core::model::SystemState> &m_state; ///< 借用唯一事实存储；生命周期覆盖全部报告者。
    QMutex m_mutex; ///< 串行化两个报告源的读改写，只保护低频内存操作。
};
} // namespace core::app
