#include "core/app/SystemStateOwner.h"
#include "core/producer/RuntimeStateGate.h"
#include <QDateTime>
#include <QMutexLocker>
#include <chrono>
#include <utility>

namespace core::app {
/**
 * @brief 只绑定唯一状态存储，首次快照由组合根显式报告，避免构造期间推断服务就绪。
 */
SystemStateOwner::SystemStateOwner(core::domain::LatestState<core::model::SystemState> &state)
    : m_state(state)
{
}

/**
 * @brief 从唯一 State 借用旧值形成短期完整副本，保留启动时间；校验成功后才提交新 revision。
 * @details 生命周期与健康度同时更新，停止清零就绪位由门禁强制校验；拒绝值不会污染旧快照。
 * 相同阶段及位图不重复发布，UTC 使用实际墙钟，超时调度不依赖这里的时间。
 */
bool SystemStateOwner::setLifecycle(core::model::ApplicationPhase phase,
                                    core::model::SystemReadinessFlags readinessFlags)
{
    QMutexLocker lock(&m_mutex);
    const auto previous = m_state.latest();
    if (previous && previous->sample->value.phase == phase
        && previous->sample->value.readinessFlags == readinessFlags) return true;
    core::model::SystemState value = previous
        ? previous->sample->value : core::model::SystemState{};
    value.phase = phase;
    value.readinessFlags = readinessFlags;
    return store(value);
}

/**
 * @brief 合并存储当前可用性和不可逆历史缺口；不会让归档恢复覆盖进程 Faulted 阶段。
 */
bool SystemStateOwner::reportArchive(core::model::SystemDegradationFlags flags)
{
    if ((flags & ~quint32(7)) != 0) return false;
    QMutexLocker lock(&m_mutex);
    const auto previous = m_state.latest();
    if (!previous) return false;
    auto value = previous->sample->value;
    const auto merged = (value.degradationFlags & ~quint32(1)) | flags;
    if (merged == value.degradationFlags) return true;
    value.degradationFlags = merged;
    return store(value);
}

/**
 * @brief 在 Owner 短临界区内生成经校验的完整快照；不执行任何外部 I/O。
 */
bool SystemStateOwner::store(core::model::SystemState value)
{
    core::model::Sample<core::model::SystemState> sample;
    sample.value = value;
    sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    sample.meta.receiveTimeSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (sample.value.processStartedUtcMs == 0) sample.value.processStartedUtcMs = sample.meta.receiveTimeUtcMs;
    sample.value.health = value.phase == core::model::ApplicationPhase::Faulted
        ? core::model::SystemHealth::Faulted : value.degradationFlags != 0
            ? core::model::SystemHealth::Degraded : core::model::SystemHealth::Healthy;
    const auto rejection = core::producer::RuntimeStateGate::validateSystemState(sample);
    if (rejection != core::producer::RuntimeStateRejection::None) {
        return false;
    }
    m_state.set(std::move(sample));
    return true;
}
} // namespace core::app
