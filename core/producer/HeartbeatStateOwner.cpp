#include "HeartbeatStateOwner.h"
#include "RuntimeStateGate.h"
#include <QDateTime>
#include <QThread>
#include <chrono>
namespace core::producer {
/**
 * @brief 重启时不继承旧在线状态，保留其他设备字段。
 */
void HeartbeatStateOwner::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_last = {};
    auto sample = *m_state.latest()->sample;
    sample.value.dpHeartbeat = sample.value.spHeartbeat = 0;
    sample.value.fpOnlineMask = sample.value.fpDataMask = 0;
    publish(std::move(sample), QDateTime::currentMSecsSinceEpoch(), std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    m_timer.start(100);
}
/**
 * @brief 停止期限调度，不产生伪造离线报文。
 */
void HeartbeatStateOwner::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_timer.stop();
}
/**
 * @brief 初始化状态并使用单调时钟驱动期限，系统时间调整不改变掉线判定。
 */
HeartbeatStateOwner::HeartbeatStateOwner(core::domain::LatestState<core::model::DeviceState>& state)
    : m_state(state), m_timer(this)
{
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    publish({}, QDateTime::currentMSecsSinceEpoch(), now);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        check(QDateTime::currentMSecsSinceEpoch(), std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    });
}
/**
 * @brief 先验证字段及接收顺序，再更新对应来源；同值心跳只续期，不制造revision。
 */
void HeartbeatStateOwner::accept(bool sp, quint16 online, quint16 mask, qint64 utcMs, qint64 steadyNs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto index = sp ? 1U : 0U;
    if (online > 1 || (mask & 0xFFF0U) || utcMs <= 0 || steadyNs <= 0 || steadyNs < m_last[index]) return;
    m_last[index] = steadyNs;
    auto sample = *m_state.latest()->sample;
    auto& status = sp ? sample.value.spHeartbeat : sample.value.dpHeartbeat;
    auto& bits = sp ? sample.value.fpDataMask : sample.value.fpOnlineMask;
    const quint8 next = online ? 1 : 2;
    if (status == next && bits == mask) return;
    status = next;
    bits = mask;
    publish(std::move(sample), utcMs, steadyNs);
}
/**
 * @brief 分别检查两个来源；超时清位图，避免把陈旧FP信息当作当前在线。
 */
void HeartbeatStateOwner::check(qint64 utcMs, qint64 steadyNs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto sample = *m_state.latest()->sample;
    bool changed = false;
    for (unsigned i = 0; i < 2; ++i) {
        if (!m_last[i] || steadyNs < m_last[i]) continue;
        auto& status = i ? sample.value.spHeartbeat : sample.value.dpHeartbeat;
        auto& bits = i ? sample.value.fpDataMask : sample.value.fpOnlineMask;
        const qint64 age = steadyNs - m_last[i];
        const quint8 next = age >= 5000000000LL ? 0 : age >= 2000000000LL ? 2 : status;
        if (next != status) { status = next; if (!next) bits = 0; changed = true; }
    }
    if (changed) publish(std::move(sample), utcMs, steadyNs);
}
/**
 * @brief 门禁验证完整值；失败不改变数据域水位。
 */
void HeartbeatStateOwner::publish(core::model::Sample<core::model::DeviceState> sample, qint64 utc, qint64 steady)
{
    sample.meta.receiveTimeUtcMs = utc;
    sample.meta.receiveTimeSteadyNs = steady;
    if (RuntimeStateGate::validateDeviceState(sample, 4) == RuntimeStateRejection::None) m_state.set(std::move(sample));
}
}
